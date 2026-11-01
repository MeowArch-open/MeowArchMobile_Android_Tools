#!/usr/bin/env python3
"""test_flash_x.py — end-to-end checks for `flash-x PARTITION PAYLOAD [NAME]`.

Every case drives the real binary over TCP against mock_device.py, which
records what fastboot actually transmitted; the extracted bytes are then
compared against the partition content make_payload.py put in the payload. That
covers the whole chain — header parse, manifest decode, xz/bzip2/raw
decompression, extent placement, SHA-256 verification, the temp-file handoff to
FlashTask and the download itself — not just the parser.

Usage: test_flash_x.py <fastboot> [--port N] [--runner "wine"]

--runner prefixes every invocation, which is how the wine suite drives the
Windows exe through this same set of checks.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MOCK = os.path.join(HERE, "mock_device.py")
MAKE = os.path.join(HERE, "make_payload.py")


class Harness:
    def __init__(self, fb: str, port: int, runner=None):
        self.fb = fb
        self.port = port
        self.runner = runner or []
        self.tmp = tempfile.mkdtemp(prefix="flashx-")
        # Where the extraction temp image goes (libbase honors TMPDIR).
        self.tmpdir = os.path.join(self.tmp, "tmp")
        os.makedirs(self.tmpdir)

    def close(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def path(self, name: str) -> str:
        return os.path.join(self.tmp, name)

    def run_command(self, *args, env=None, expect_ok=False):
        """Run fastboot against the mock device; return (rc, output, bytes)."""
        self.port += 1
        port = self.port
        cap = self.path("capture.bin")
        if os.path.exists(cap):
            os.unlink(cap)
        mock = subprocess.Popen(
            [sys.executable, MOCK, str(port), cap],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.8)
        # TMPDIR is what libbase's GetSystemTempDir honors on POSIX; on Windows
        # it reads TMP, which wine passes through and path-translates, so both
        # land the extraction temp file inside this test's own directory.
        base_env = dict(os.environ, LANG="en_US.UTF-8", NO_COLOR="1",
                        NO_PROGRESS="1", WINEDEBUG="-all", TMPDIR=self.tmpdir,
                        TMP=self.tmpdir, TEMP=self.tmpdir)
        if env:
            base_env.update(env)
        proc = subprocess.run(
            [*self.runner, self.fb, "-s", f"tcp:127.0.0.1:{port}", *args],
            capture_output=True, env=base_env, timeout=600)
        mock.terminate()
        try:
            mock.wait(timeout=5)
        except subprocess.TimeoutExpired:
            mock.kill()
        text = (proc.stdout + proc.stderr).decode(errors="replace")
        text = text.replace("\r", "")   # wine console stdio is CRLF
        captured = b""
        if os.path.exists(cap):
            with open(cap, "rb") as f:
                captured = f.read()
        if expect_ok:
            assert proc.returncode == 0, f"command failed rc={proc.returncode}\n{text}"
            assert "OKAY" in text, f"no OKAY in output:\n{text}"
        return proc.returncode, text, captured


    def run_flash_x(self, *args, **kw):
        return self.run_command("flash-x", *args, **kw)


sys.path.insert(0, HERE)
# Shared with make_payload.py so the generator and the expected bytes can
# never drift apart.
from make_payload import partition_content as content  # noqa: E402
from make_payload import lfield, vfield  # noqa: E402


def truncate_blob(path: str, drop: int) -> None:
    """Cut |drop| bytes off the end of the payload's data section.

    Makes the last stored blob shorter than its destination extents, which is
    what a truncated download looks like.
    """
    size = os.path.getsize(path)
    with open(path, "r+b") as f:
        f.truncate(size - drop)


def check(label: str, cond: bool, detail: str = "") -> bool:
    print(f"  {'ok  ' if cond else 'FAIL'} {label}" + (f" — {detail}" if detail and not cond else ""))
    return cond


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("fastboot")
    ap.add_argument("--port", type=int, default=5670)
    ap.add_argument("--runner", default="",
                    help="prefix for every fastboot invocation, e.g. 'wine'")
    args = ap.parse_args()
    fb = os.path.abspath(args.fastboot)
    runner = args.runner.split() if args.runner else []
    h = Harness(fb, args.port, runner)
    passed = failed = 0

    def record(ok):
        nonlocal passed, failed
        if ok:
            passed += 1
        else:
            failed += 1

    # 96 KiB: several progress-bar chunks, still quick. The content is
    # deliberately incompressible (SHA-256 derived), so the compressors are
    # actually exercised and no bug can hide behind a degenerate stream.
    SIZE = 96 * 1024

    try:
        # --- 1. xz (what real payloads use) ------------------------------------
        pay = h.path("xz.bin")
        subprocess.run([sys.executable, MAKE, pay, "--codec", "xz",
                        "--parts", "boot,system,vbmeta", "--size", str(SIZE)],
                       check=True, capture_output=True)
        rc, text, cap = h.run_flash_x("boot", pay, expect_ok=True)
        record(check("flash-x boot <xz payload> extracts and flashes",
                     cap == content("boot", SIZE),
                     f"got {len(cap)} bytes, want {SIZE}"))

        # The slot suffix must be stripped when picking the payload entry.
        rc, text, cap = h.run_flash_x("boot_a", pay, expect_ok=True)
        record(check("flash-x boot_a finds payload entry 'boot'",
                     cap == content("boot", SIZE)))

        # An explicit NAME picks a different entry than the target partition.
        rc, text, cap = h.run_flash_x("boot", pay, "system", expect_ok=True)
        record(check("explicit NAME (system) overrides the partition name",
                     cap == content("system", SIZE)))

        # --- 2. bzip2 and raw --------------------------------------------------
        for codec in ("bz2", "raw"):
            pay2 = h.path(f"{codec}.bin")
            subprocess.run([sys.executable, MAKE, pay2, "--codec", codec,
                            "--parts", "boot", "--size", str(SIZE)],
                           check=True, capture_output=True)
            rc, text, cap = h.run_flash_x("boot", pay2, expect_ok=True)
            record(check(f"{codec} payload decodes to the same bytes",
                         cap == content("boot", SIZE)))

        # An xz stream whose integrity check is SHA-256 (check id 10). AOSP's
        # xz-embedded cannot compute it, so XZ_DEC_ANY_CHECK must make the
        # decoder skip the check and still produce the data — if that define or
        # the crc-table init is missing, this comes back as XZ_DATA_ERROR.
        pay_sha = h.path("xz-sha256.bin")
        subprocess.run([sys.executable, MAKE, pay_sha, "--codec", "xz",
                        "--xz-check", "sha256", "--parts", "boot",
                        "--size", str(SIZE)], check=True, capture_output=True)
        rc, text, cap = h.run_flash_x("boot", pay_sha, expect_ok=True)
        record(check("xz blob with a SHA-256 stream check still decodes",
                     cap == content("boot", SIZE), f"\n{text}"))

        # Same with the check byte stripped out of the manifest (no per-op hash)
        # so nothing else can substitute for the decode succeeding.
        pay_sha2 = h.path("xz-sha256-nohash.bin")
        subprocess.run([sys.executable, MAKE, pay_sha2, "--codec", "xz",
                        "--xz-check", "sha256", "--parts", "boot",
                        "--size", str(SIZE), "--no-hash"],
                       check=True, capture_output=True)
        rc, text, cap = h.run_flash_x("boot", pay_sha2, expect_ok=True)
        record(check("xz SHA-256-check blob decodes with no manifest hashes",
                     cap == content("boot", SIZE), f"\n{text}"))

        # dst_length is documented for BSDIFF ops and delta_generator omits it
        # for REPLACE*; the expected size then has to come from dst_extents, or
        # a blob that ends early decodes into a silently short image.
        for codec in ("xz", "raw", "bz2"):
            pay_ndl = h.path(f"nodstlen-{codec}.bin")
            subprocess.run([sys.executable, MAKE, pay_ndl, "--codec", codec,
                            "--parts", "boot", "--size", str(SIZE),
                            "--no-dst-length"], check=True, capture_output=True)
            rc, text, cap = h.run_flash_x("boot", pay_ndl, expect_ok=True)
            record(check(f"{codec} payload without dst_length still extracts",
                         cap == content("boot", SIZE),
                         f"got {len(cap)} of {SIZE}\n{text}"))

        # A blob shorter than its extents must fail rather than produce a short
        # image: this is the check that the extent-derived bound above enforces.
        pay_short = h.path("short.bin")
        subprocess.run([sys.executable, MAKE, pay_short, "--codec", "raw",
                        "--parts", "boot", "--size", str(SIZE), "--no-hash"],
                       check=True, capture_output=True)
        truncate_blob(pay_short, 4096)
        rc, text, cap = h.run_flash_x("boot", pay_short, expect_ok=False)
        record(check("blob shorter than its extents is rejected",
                     rc != 0 and not cap, f"rc={rc}\n{text}"))

        # ...and the same with dst_length absent, so only the extent-derived
        # bound can catch it. This is the case the fallback exists for: without
        # it, a truncated download decodes "successfully" into a short image.
        # --short-data declares data_length smaller than the stored blob while
        # the file itself stays intact, so the blob hash (when present) and the
        # reader both succeed; only comparing the decoded size with dst_extents
        # can catch it. With --no-hash and --no-dst-length that comparison is
        # the only defence left.
        pay_short2 = h.path("short-nodstlen.bin")
        subprocess.run([sys.executable, MAKE, pay_short2, "--codec", "raw",
                        "--parts", "boot", "--size", str(SIZE), "--no-hash",
                        "--no-dst-length", "--short-data", str(4096)],
                       check=True, capture_output=True)
        rc, text, cap = h.run_flash_x("boot", pay_short2, expect_ok=False)
        record(check("op that cannot fill its extents is rejected",
                     rc != 0 and not cap, f"rc={rc}\n{text}"))

        # --- 3. multiple ops with disjoint extents -----------------------------
        pay3 = h.path("multi.bin")
        subprocess.run([sys.executable, MAKE, pay3, "--codec", "xz",
                        "--parts", "system", "--size", str(SIZE), "--ops", "4"],
                       check=True, capture_output=True)
        rc, text, cap = h.run_flash_x("system", pay3, expect_ok=True)
        record(check("4 ops with disjoint extents reassemble in order",
                     cap == content("system", SIZE)))

        # --- 4. errors ---------------------------------------------------------
        # Unknown entry: names the payload's contents, exits non-zero, flashes
        # nothing.
        rc, text, cap = h.run_flash_x("nosuchpart", pay, expect_ok=False)
        record(check("unknown payload entry errors and lists what exists",
                     rc != 0 and "nosuchpart" in text and "boot" in text and not cap,
                     f"rc={rc} cap={len(cap)}\n{text}"))

        # Not a payload at all.
        junk = h.path("junk.bin")
        with open(junk, "wb") as f:
            f.write(b"ANDROID!" + os.urandom(4096))
        rc, text, cap = h.run_flash_x("boot", junk, expect_ok=False)
        record(check("non-payload file is rejected", rc != 0 and not cap))

        # Missing file.
        rc, text, cap = h.run_flash_x("boot", h.path("absent.bin"), expect_ok=False)
        record(check("missing payload file errors", rc != 0 and not cap))

        # Delta payload: SOURCE_COPY ops need the on-device data.
        pay_d = h.path("delta.bin")
        subprocess.run([sys.executable, MAKE, pay_d, "--codec", "xz",
                        "--parts", "boot", "--size", str(SIZE), "--delta"],
                       check=True, capture_output=True)
        rc, text, cap = h.run_flash_x("boot", pay_d, expect_ok=False)
        record(check("delta payload is refused", rc != 0 and not cap))

        # Corruption. Three distinct detection paths, because a flipped byte
        # lands differently depending on how the blob is stored:
        #   * xz blob: the CRC inside the xz stream fails first (XZ_DATA_ERROR)
        #     — correct, it must not be silently accepted;
        #   * raw blob with the op hash present: the manifest's per-operation
        #     SHA-256 no longer matches;
        #   * raw blob without the op hash: only the whole-image
        #     new_partition_info.hash can catch it.
        # None of the three may transfer a single byte to the device.
        def make_pay(name, *extra):
            path = h.path(name)
            subprocess.run([sys.executable, MAKE, path, "--parts", "boot",
                            "--size", str(SIZE), *extra],
                           check=True, capture_output=True)
            return path

        pay_cx = make_pay("corrupt-xz.bin", "--codec", "xz", "--corrupt")
        rc, text, cap = h.run_flash_x("boot", pay_cx, expect_ok=False)
        record(check("corrupt xz blob is rejected by the stream CRC",
                     rc != 0 and "xz" in text.lower() and not cap, f"\n{text}"))

        pay_cr = make_pay("corrupt-raw.bin", "--codec", "raw", "--corrupt")
        rc, text, cap = h.run_flash_x("boot", pay_cr, expect_ok=False)
        record(check("corrupt blob trips the per-operation SHA-256",
                     rc != 0 and "SHA-256" in text and not cap, f"\n{text}"))

        pay_cn = make_pay("corrupt-noophash.bin", "--codec", "raw", "--corrupt",
                          "--no-op-hash")
        rc, text, cap = h.run_flash_x("boot", pay_cn, expect_ok=False)
        record(check("corruption caught by the whole-image manifest hash",
                     rc != 0 and "hash" in text.lower() and not cap, f"\n{text}"))

        # Sanity: the same payload without --corrupt is accepted, so the
        # failure above is the corruption and not the flag.
        pay_ok = make_pay("noophash-ok.bin", "--codec", "raw", "--no-op-hash")
        rc, text, cap = h.run_flash_x("boot", pay_ok, expect_ok=True)
        record(check("payload without per-op hashes still extracts correctly",
                     cap == content("boot", SIZE)))

        # --- 5. usage ----------------------------------------------------------
        # Argument counts are checked before the payload is opened, but — like
        # every other command — after the device connects, so these run through
        # the harness too. None of them may transfer a single byte.
        rc, text, cap = h.run_flash_x("boot", pay, "system", "extra")
        record(check("flash-x with 4 arguments is a usage error",
                     rc != 0 and not cap, f"rc={rc}\n{text}"))

        rc, text, cap = h.run_flash_x()
        record(check("flash-x with no arguments is a usage error",
                     rc != 0 and not cap, f"rc={rc}\n{text}"))

        rc, text, cap = h.run_flash_x("boot")
        record(check("flash-x with only PARTITION is a usage error",
                     rc != 0 and not cap, f"rc={rc}\n{text}"))

        # --- 5b. hostile / malformed payloads ----------------------------------
        # Every one of these must fail with a message and exit non-zero, never
        # crash, hang, or try to allocate/ftruncate something absurd. The header
        # fields and the protobuf body all come straight from the file.
        def raw_payload(name, blob):
            p = h.path(name)
            with open(p, "wb") as f:
                f.write(blob)
            return p

        op_absurd = vfield(1, 6) + lfield(6, vfield(1, 1 << 40) + vfield(2, 1 << 40))
        man_absurd = lfield(13, lfield(1, b"boot") + lfield(8, op_absurd))
        man_empty = lfield(13, lfield(1, b"boot"))
        malformed = [
            ("empty", b""),
            ("magic_only", b"CrAU"),
            ("short_header", b"CrAU" + struct.pack(">Q", 2)),
            ("zero_manifest_len", b"CrAU" + struct.pack(">QQI", 2, 0, 0)),
            ("huge_manifest_len", b"CrAU" + struct.pack(">QQI", 2, 1 << 40, 0)),
            ("bad_major", b"CrAU" + struct.pack(">QQI", 9, 16, 0) + b"\0" * 16),
            ("truncated_manifest",
             b"CrAU" + struct.pack(">QQI", 2, 1000, 0) + b"\0" * 8),
            ("garbage_manifest", b"CrAU" + struct.pack(">QQI", 2, 8, 0) + b"\xff" * 8),
            ("partition_no_ops",
             b"CrAU" + struct.pack(">QQI", 2, len(man_empty), 0) + man_empty),
            ("absurd_extent",
             b"CrAU" + struct.pack(">QQI", 2, len(man_absurd), 0) + man_absurd),
        ]
        for name, blob in malformed:
            path = raw_payload(f"mal-{name}", blob)
            rc, text, cap = h.run_flash_x("boot", path)
            record(check(f"malformed payload: {name}",
                         rc != 0 and not cap and "AddressSanitizer" not in text,
                         f"rc={rc}\n{text}"))

        # --- 6. localization ---------------------------------------------------
        rc, text, cap = h.run_flash_x("nosuchpart", pay,
                                      env={"LANG": "zh_CN.UTF-8"}, expect_ok=False)
        record(check("zh_CN error text for unknown payload entry",
                     rc != 0 and "payload" in text and "nosuchpart" in text))

        # --- 7. no temp files left behind --------------------------------------
        before = set(os.listdir(h.tmpdir))
        h.run_flash_x("boot", pay, expect_ok=True)
        after = set(os.listdir(h.tmpdir))
        record(check("temp image is cleaned up after flashing",
                     not (after - before), f"left: {after - before}"))

    finally:
        h.close()

    print(f"\nflash-x: {passed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())