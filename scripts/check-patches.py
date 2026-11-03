#!/usr/bin/env python3
"""Verify every patch applies with EXACT context to a pristine file, and is
recognized as already-applied on a second pass.

Usage: python3 scripts/check-patches.py    (exit 0 = every patch is sound)

Why: a patch whose context only matches with fuzz can be re-applied at a
shifted offset on every CMake configure, silently duplicating its block (that
is what put 22 copies of the ADBD_NO_AUTH block into daemon/main.cpp).

Scratch files come from `git show HEAD:<path>` in the real worktree, because
.git under aosp-adbd is a symlink into .repo/ and resolves to nothing in a copy.
"""
import re, shutil, subprocess, sys, tempfile, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASES = [ROOT / "aosp-adbd/packages/modules/adb", ROOT / "aosp-adbd/system/core"]

def locate(rel):
    for b in BASES:
        if (b / rel).is_file():
            return b
    return None

def check(patch_path):
    text = patch_path.read_text()
    files = set()
    for m in re.finditer(r"^(?:---|\+\+\+) ([ab])/(.+?)$", text, re.M):
        files.add(m.group(2))
    scratch = pathlib.Path(tempfile.mkdtemp(prefix="ec."))
    missing = []
    for rel in sorted(files):
        base = locate(rel)
        if base is None:
            missing.append(rel); continue
        out = subprocess.run(["git", "-C", str(base), "show", f"HEAD:{rel}"],
                             capture_output=True)
        if out.returncode != 0:
            missing.append(rel + " (not in HEAD)"); continue
        dst = scratch / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(out.stdout)
    if missing:
        shutil.rmtree(scratch); return "LOCATE-FAIL", ", ".join(missing)

    def run():
        return subprocess.run(["patch", "-N", "-s", "--fuzz=0", "-r", "/dev/null",
                               "-p1", "-d", str(scratch), "-i", str(patch_path)],
                              capture_output=True, text=True)
    r1 = run(); r2 = run()
    shutil.rmtree(scratch)
    if r1.returncode == 0 and r2.returncode != 0:
        return "ok", ""
    if r1.returncode == 0:
        return "NOT-IDEMPOTENT", "second apply returned 0 (block duplicated)"
    return "NOT-EXACT", (r1.stdout + r1.stderr).strip().splitlines()[0] if (r1.stdout or r1.stderr) else f"rc={r1.returncode}"

bad = 0
for proj in ("adb-standalone", "adbd-standalone", "better_fastboot"):
    d = ROOT / proj / "patches"
    if not d.is_dir(): continue
    print(f"=== {proj} ===")
    for p in sorted(d.glob("*.patch")):
        verdict, why = check(p)
        print(f"  {verdict:<16} {p.name}" + (f"  -> {why}" if why else ""))
        if verdict != "ok": bad += 1
print()
print(f"{bad} problem(s)")
sys.exit(1 if bad else 0)
