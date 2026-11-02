#!/usr/bin/env python3
"""test_sparse_progress.py — the bar must keep moving through a huge sparse chunk.

Regression for `fastboot flash super super.img`: a super image arrives as a few
hundred-megabyte raw chunks, and libsparse hands each chunk to the driver as a
single callback, which became a single transport write. The progress callback
only fires when a write *returns*, so the bar froze for the duration of the
chunk (seconds) and then jumped straight to 30-40% — the jumpiness users saw on
big partitions while small ones looked smooth.

The fix chunks every transport write at 1 MiB in SendBuffer(buf, size). This
test proves it: it flashes a one-chunk 200 MiB sparse image over a pty against
the throttled mock device and asserts that no two frames are more than
MAX_GAP apart, i.e. the bar never goes quiet for the length of a chunk.

Usage: test_sparse_progress.py <fastboot> [--image PATH] [--port N]
Prints a one-line verdict; exit 0 = pass.
"""
import argparse
import os
import pty
import re
import select
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MOCK = os.path.join(HERE, "mock_device.py")

# The mock device throttles ingest to ~15 MiB/s. A single unchunked write of a
# 76 MiB chunk would therefore go quiet for ~5s; 1 MiB writes report roughly
# every 70ms, and the bar's own throttle is 80ms. Anything under 1.5s is
# comfortably a pass, anything over a second of silence is a real freeze.
MAX_GAP = 1.5
IMAGE_BYTES = 80_000_000  # one 76 MiB chunk: ~5s of silence pre-fix
ANSI = re.compile(rb"\x1b\[[0-9;]*[A-Za-z]|\x1b[=>]")


def build_sparse(path: str, total: int) -> None:
    """One raw chunk: the worst case for callback granularity."""
    subprocess.run([sys.executable, os.path.join(HERE, "make_sparse.py"),
                    path, str(total), "1"], check=True)


def run(fb: str, image: str, port: int) -> list:
    """Run `fb flash` under a pty, returning [(t, frame_bytes), ...]."""
    env = dict(os.environ, LANG="en_US.UTF-8", TERM="xterm", NO_COLOR="1",
               COLUMNS="100")
    mock = subprocess.Popen([sys.executable, MOCK, str(port)],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    time.sleep(0.8)

    master, slave = pty.openpty()
    proc = subprocess.Popen(
        [fb, "-s", f"tcp:127.0.0.1:{port}", "flash", "super", image],
        stdin=slave, stdout=slave, stderr=slave, env=env,
        start_new_session=True)
    os.close(slave)

    frames = []
    buf = b""
    t0 = time.time()
    while True:
        r, _, _ = select.select([master], [], [], 0.5)
        if r:
            try:
                chunk = os.read(master, 65536)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            while b"\r" in buf:
                frame, buf = buf.split(b"\r", 1)
                frames.append((time.time() - t0, frame))
        if proc.poll() is not None:
            # drain whatever is left
            try:
                os.set_blocking(master, False)
                tail = os.read(master, 65536)
                if tail:
                    buf += tail
            except OSError:
                pass
            break
    if buf:
        frames.append((time.time() - t0, buf))

    proc.wait(timeout=10)
    mock.terminate()
    try:
        mock.wait(timeout=5)
    except subprocess.TimeoutExpired:
        mock.kill()
    os.close(master)
    return frames


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("fastboot")
    ap.add_argument("--image")
    ap.add_argument("--port", type=int, default=5655)
    args = ap.parse_args()

    tmp = tempfile.mkdtemp(prefix="spx-")
    image = args.image or os.path.join(tmp, "super.img")
    try:
        if not args.image:
            build_sparse(image, IMAGE_BYTES)

        frames = run(args.fastboot, image, args.port)
        # only frames that are actual bar repaints (contain a percentage)
        bar = [(t, f) for t, f in frames
               if b"%" in ANSI.sub(b"", f) and b"Sending" in f]
        if len(bar) < 5:
            print(f"FAIL only {len(bar)} bar frames captured (expected >=5)")
            return 1

        gaps = [(bar[i][0] - bar[i - 1][0], i) for i in range(1, len(bar))]
        worst_gap, worst_i = max(gaps)

        # percent must advance monotonically and cover most of the transfer
        def pct(f):
            m = re.search(rb"(\d{1,3})%", ANSI.sub(b"", f))
            return int(m.group(1)) if m else -1

        pcts = [pct(f) for _, f in bar]
        reached = max(pcts)
        monotonic = all(a <= b for a, b in zip(pcts, pcts[1:]))

        print(f"frames={len(bar)} worst_gap={worst_gap:.2f}s "
              f"at frame {worst_i} reached={reached}% "
              f"monotonic={monotonic} limit={MAX_GAP}s")
        if worst_gap > MAX_GAP:
            print(f"FAIL bar froze for {worst_gap:.2f}s (> {MAX_GAP}s) — "
                  "progress callback is not firing inside a large chunk")
            return 1
        if not monotonic:
            print("FAIL percent went backwards")
            return 1
        if reached < 95:
            print(f"FAIL transfer only reached {reached}%")
            return 1
        print("PASS bar advanced smoothly through the chunk")
        return 0
    finally:
        if not args.image:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())