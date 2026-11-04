#!/usr/bin/env python3
"""Deterministic layout check for the transfer progress bar.

A real transfer cannot prove the bar's layout: over loopback a push finishes
before enough frames are emitted, and a pty created by `script` has a 0x0 window
so the bar falls back to 80 columns. This drives tests/test_progress_layout
directly under a pty whose window size we set ourselves, so every property can
be asserted exactly.

What is asserted, per terminal width:
  * the bar's starting COLUMN is identical for every label — a short name, a
    long name, a CJK name. This is the regression the user reported: the bar
    slid sideways as the file changed.
  * the bar's LENGTH is identical for every label and every frame.
  * no frame is wider than the terminal. A wrapped frame cannot be erased with
    "\\r", so every previous frame stays on screen — the "noisy" output.
  * the byte counter has constant width, including across a unit change
    (999 KiB -> 1.0 MiB) and across 99% -> 100%.
  * long labels are elided with "..." and still show the file name's tail.
"""
import fcntl
import os
import pty
import re
import struct
import subprocess
import sys
import termios

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "..", "build-native", "test_progress_layout")

ESC = re.compile(rb"\x1b\[[0-9;]*[A-Za-z]")


def display_width(s: str) -> int:
    """Columns a string occupies: 2 for the CJK/fullwidth ranges, else 1."""
    n = 0
    for ch in s:
        cp = ord(ch)
        wide = (
            0x1100 <= cp <= 0x115F
            or 0x2E80 <= cp <= 0xA4CF
            or 0xAC00 <= cp <= 0xD7A3
            or 0xF900 <= cp <= 0xFAFF
            or 0xFE30 <= cp <= 0xFE6F
            or 0xFF00 <= cp <= 0xFF60
            or 0xFFE0 <= cp <= 0xFFE6
            or 0x1F300 <= cp <= 0x1F64F
            or 0x1F900 <= cp <= 0x1F9FF
            or 0x20000 <= cp <= 0x3FFFD
        )
        n += 2 if wide else 1
    return n


def run(cols: int, rows: int, args: list) -> list:
    """Run the binary under a pty of exactly |cols| columns; return frames."""
    pid, fd = pty.fork()
    if pid == 0:
        env = dict(os.environ)
        env["TERM"] = "xterm-256color"
        env.pop("NO_PROGRESS", None)
        env.pop("NO_COLOR", None)
        try:
            os.execve(BIN, [BIN] + args, env)
        except OSError as e:
            os._exit(127)
    # Size the pty AFTER the fork: the child inherits it for ioctl(TIOCGWINSZ).
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    out = b""
    while True:
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            break
        if not chunk:
            break
        out += chunk
    os.waitpid(pid, 0)
    # Split on carriage return: each frame is one overprinted line.
    frames = []
    for raw in out.split(b"\r"):
        raw = ESC.sub(b"", raw)
        if b"[" not in raw:
            continue
        line = raw.decode("utf-8", "replace").rstrip("\n")
        if "[" in line and "]" in line:
            frames.append(line)
    return frames


def run_nobar(cols: int, rows: int, args: list, env_extra: dict = None) -> list:
    """Same as run(), but keeps every non-empty frame.

    Used for unknown-total streams (backup / restore / exec-out): those frames
    have no bar and therefore no bracket, so run()'s filter would drop them
    all. What is left is exactly the label plus the right-aligned fields, which
    is what has to stay inside the terminal.
    """
    pid, fd = pty.fork()
    if pid == 0:
        env = dict(os.environ)
        env["TERM"] = "xterm-256color"
        # Space the frames out: the speed field is derived from elapsed time, so
        # back-to-back Update() calls render it blank and its assertions would
        # be vacuous.
        env["TEST_PROGRESS_FRAME_DELAY_MS"] = "60"
        if env_extra:
            env.update(env_extra)
        env.pop("NO_PROGRESS", None)
        env.pop("NO_COLOR", None)
        try:
            os.execve(BIN, [BIN] + args, env)
        except OSError:
            os._exit(127)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    out = b""
    while True:
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            break
        if not chunk:
            break
        out += chunk
    os.waitpid(pid, 0)
    frames = []
    for raw in out.split(b"\r"):
        raw = ESC.sub(b"", raw)
        line = raw.decode("utf-8", "replace").rstrip("\n")
        if line.strip():
            frames.append(line)
    return frames


def bar_column(frame: str) -> int:
    return display_width(frame[: frame.index("[")])


def bar_length(frame: str) -> int:
    return display_width(frame[frame.index("[") : frame.index("]") + 1])


def counter_field(frame: str):
    """The "cur/tot" part INCLUDING its left padding, or None when the terminal
    is too narrow for it. The padding is the point: a short number ("0.0") is
    left-padded to the width the total needs, so the field cannot change width
    mid-transfer — which is one of the things that made the bar slide."""
    m = re.search(r"( *)(\d[\d.,]* [KMGT]?i?B)/(\d[\d.,]* [KMGT]?i?B)", frame)
    return m.group(0) if m else None


LABELS = {
    "short": "f.img",
    "long": "/tmp/a_very_long_directory_path_that_keeps_going/system_framework_services.img",
    "cjk": "/tmp/下载目录/中文名镜像文件.img",
    "mixed": "/data/local/tmp/红米-zorn_boot_a-slot-recovery-image_2026.img",
}

failures = []
checks = 0


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        failures.append(msg)
        print(f"  FAIL {msg}")
    else:
        print(f"  ok   {msg}")


for cols in (140, 100, 80, 60, 40):
    print(f"== terminal width {cols} ==")
    total = 4 * 1024 * 1024 * 1024  # 4 GiB: exercises the GiB unit
    for name, label in LABELS.items():
        frames = run(cols, 24, [label, str(total), "0", str(total // 2), str(total)])
        if not frames:
            check(False, f"{name}: no frames emitted at width {cols}")
            continue
        w = display_width(frames[0])
        check(
            w <= cols,
            f"{name}: frame is {w} cols, terminal is {cols}",
        )
    # The bar's column and length must not depend on the label.
    cols_seen = {}
    lens_seen = {}
    for name, label in LABELS.items():
        frames = run(cols, 24, [label, str(total), "0", str(total // 3), str(total)])
        cols_seen[name] = sorted({bar_column(f) for f in frames})
        lens_seen[name] = sorted({bar_length(f) for f in frames})
    flat_cols = {c for v in cols_seen.values() for c in v}
    flat_lens = {l for v in lens_seen.values() for l in v}
    check(len(flat_cols) == 1, f"bar column stable across labels: {flat_cols}")
    check(len(flat_lens) == 1, f"bar length stable across labels: {flat_lens}")

# Counter width across a unit change and across 99% -> 100%.
print("== counter width ==")
total = 1023 * 1024 + 900  # just under 1 MiB, so it crosses into MiB
frames = run(120, 24, ["/tmp/x.bin", str(total), "0", "512000", str(total - 5), str(total)])
counters = [counter_field(f) for f in frames if counter_field(f)]
widths = {display_width(c) for c in counters}
check(len(widths) <= 1, f"counter width constant across the KiB->MiB change: {widths} {counters}")

# A total whose percentage text widens (9% -> 10% -> 100%) must not move the bar.
frames = run(120, 24, ["/tmp/y.bin", "100000000", "0", "9000000", "10000000", "99000000", "100000000"])
cols_at = sorted({bar_column(f) for f in frames})
check(len(cols_at) == 1, f"bar column stable across 9%->10%->100%: {cols_at}")

# Unknown-total streams (backup / restore / exec-out): no bar, no percentage,
# but the byte counter and the speed field are still right-aligned in fixed
# reservations. This is the case that shipped broken once: a value just past
# 1 MiB renders as "1005.0 KiB" (10 columns), which is wider than the "999.9 TiB"
# form the reservation had been sized for, so every frame from that point on
# overflowed the terminal by one column and wrapped.
print("== unknown total (streaming) ==")
# Crosses B -> KiB -> MiB, and passes through 1005.0 KiB and 1013.4 KiB, the
# two values that overflowed the old reservation.
STREAM = [str(v) for v in (
    0, 512, 1024, 1_048_000, 20_971_520, 1_073_741_824, 4_831_838_208_000,
)]
for cols in (140, 100, 80, 60, 40):
    frames = run_nobar(cols, 24, [LABELS["long"], "0"] + STREAM)
    check(bool(frames), f"streaming: frames emitted at width {cols}")
    if not frames:
        continue
    widths = {display_width(f) for f in frames}
    check(max(widths) <= cols, f"streaming: no frame wider than {cols}: {sorted(widths)}")
    check(max(widths) == cols, f"streaming: frames fill the terminal exactly at {cols}: {sorted(widths)}")
    # Right edge of the counter and of the speed field: those are what must not
    # move. The digits are right-aligned inside a fixed reservation, so the
    # START column legitimately shifts as the number grows.
    cnt_ends, spd_ends, labels = set(), set(), set()
    for f in frames:
        # Speed is absent on the very first sample (no interval yet), so it is
        # optional; the counter never is.
        m = re.search(r"(\d[\d.,]* [KMGT]?i?B)(?:\s+(\d[\d.,]* [KMGT]?i?B/s))?\s*$", f)
        if not m:
            continue
        cnt_ends.add(display_width(f[: m.end(1)]))
        if m.group(2):
            spd_ends.add(display_width(f[: m.end(2)]))
        # Everything left of the counter is the label field (left-aligned, then
        # padded), so its text is the prefix with the padding stripped.
        labels.add(f[: m.start(1)].rstrip())
    check(len(cnt_ends) <= 1, f"streaming: counter right edge fixed at width {cols}: {sorted(cnt_ends)}")
    check(len(spd_ends) <= 1, f"streaming: speed right edge fixed at width {cols}: {sorted(spd_ends)}")
    check(len(labels) == 1, f"streaming: label does not change between frames: {sorted(labels)}")
    if labels:
        lab = labels.pop()
        # The file name's tail survives at every width — that is what elision is
        # for — and it is only elided when the label genuinely does not fit.
        if "…" in lab:
            # Elided: what is shown after the ellipsis must still be a real
            # suffix of the path, and must include the file name.
            shown = lab.split("…", 1)[1]
            check(len(shown) > 0 and LABELS["long"].endswith(shown),
                  f"streaming: elided label tail is a suffix of the path at width {cols}: '{lab}'")
        else:
            check(lab == LABELS["long"],
                  f"streaming: label shown in full when it fits at width {cols}: '{lab}'")
        check(("…" in lab) == (display_width(LABELS["long"]) > display_width(lab)),
              f"streaming: label elided exactly when too long at width {cols}: '{lab}'")

# Percentage-driven mode (adb bugreport, whose device side reports a fraction
# rather than bytes): bar + "NN%", no byte counter and no speed. The percentage
# text itself widens from "  0%" to "100%", so this is the case that checks the
# pct field is a fixed 5 columns — "%3d%%" left one column short and every frame
# came out one narrower than the terminal.
print("== percent mode (bugreport) ==")
PCT = ["0", "9", "12", "47", "88", "99", "100"]
for cols in (140, 100, 80, 60, 40):
    frames = run_nobar(cols, 24, ["generating bugreport.zip", "100"] + PCT,
                       {"TEST_PROGRESS_MODE": "percent"})
    check(bool(frames), f"percent: frames emitted at width {cols}")
    if not frames:
        continue
    widths = {display_width(f) for f in frames}
    check(widths == {cols}, f"percent: every frame exactly {cols} cols: {sorted(widths)}")
    # No byte counter and no speed in this mode.
    check(not any("/" in f for f in frames), f"percent: no byte counter at width {cols}")
    check(not any(re.search(r"[KMGT]?i?B/s", f) for f in frames),
          f"percent: no speed field at width {cols}")
    # The bar and the percentage are both present and the bar is the same
    # length across all frames (a label-length-independent layout).
    check(all("[" in f and "]" in f for f in frames), f"percent: bar present at width {cols}")
    check(len({bar_length(f) for f in frames}) == 1, f"percent: bar length stable at width {cols}")
    check(len({re.search(r"(\d+)%", f).group(1) for f in frames if re.search(r"(\d+)%", f)})
          == len(PCT), f"percent: one frame per reported percentage at width {cols}")

# sideload-host scales its total by 100/47 (upstream's own heuristic: the
# device reads the package roughly twice, so 100% is reached at ~2.13x the
# package size). Two properties matter: a device that reads MORE than the
# scaled total must still clamp at 100% rather than print 137%, and the frame
# width must not change when the percentage reaches three digits.
print("== sideload overshoot ==")
OTA = 900 * 1024 * 1024
SIDETOTAL = OTA * 100 // 47
SIDE = ["0", str(OTA // 3), str(OTA), str(SIDETOTAL // 2),
        str(int(SIDETOTAL * 0.97)), str(SIDETOTAL),
        str(int(SIDETOTAL * 1.05)), str(int(SIDETOTAL * 1.3))]
for cols in (140, 80, 40):
    frames = run_nobar(cols, 24, ["/tmp/a_very_long_ota_package_name_that_is_long.zip",
                                  str(SIDETOTAL)] + SIDE)
    widths = {display_width(f) for f in frames}
    check(widths == {cols}, f"sideload: every frame exactly {cols} cols: {sorted(widths)}")
    pcts = [int(m.group(1)) for f in frames if (m := re.search(r"(\d+)%", f))]
    check(bool(pcts) and max(pcts) <= 100,
          f"sideload: percentage clamped at 100 despite overshoot: {pcts}")
    check(100 in pcts, f"sideload: reaches 100% when the scaled total is met at width {cols}")

# Long labels are elided but keep the file name's tail.
print("== elision ==")
frames = run(120, 24, [LABELS["long"], str(total), str(total // 2)])
if frames:
    lab = frames[0][: frames[0].index("[")].rstrip()
    check("…" in lab or "..." in lab, f"long label elided: '{lab}'")
    check(lab.endswith("services.img"), f"elided label keeps the file name: '{lab}'")
    check(display_width(lab) <= 120, f"elided label fits: {display_width(lab)} cols")

print()
print(f"{checks - len(failures)}/{checks} checks passed")
sys.exit(1 if failures else 0)
