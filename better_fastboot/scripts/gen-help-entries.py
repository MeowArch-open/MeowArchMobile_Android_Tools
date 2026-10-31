#!/usr/bin/env python3
"""Generate fb_i18n_en.cpp's help tables straight from upstream fastboot.cpp.

Reads the show_help() literal block in system/core/fastboot/fastboot.cpp and
emits the English command/option entries as (cmd, desc) pairs, guaranteeing
byte-identical rendering vs upstream. Continuation lines fold into the
preceding entry's desc with embedded "\n". Run from the repo root:

    python3 better_fastboot/scripts/gen-help-entries.py

Output is written to better_fastboot/src/fb_i18n_en_generated.h (a static
array included by fb_i18n_en.cpp), so the tedious table is never hand-typed.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
AOSP = os.path.join(ROOT, "aosp-adbd")
FB_CPP = os.path.join(AOSP, "system/core/fastboot/fastboot.cpp")
OUT = os.path.join(HERE, "..", "src", "fb_i18n_en_generated.h")

CMD_COL = 1   # one leading space
DESC_COL = 28  # description column (upstream's 1234...89|0 ruler)


def extract_help_lines(src: str):
    # Grab the string literals inside show_help()'s fprintf(stdout, ... );
    m = re.search(r"static int show_help\(\)\s*\{(.*?)\n\s*//\s*clang-format on", src, re.S)
    if not m:
        sys.exit("could not locate show_help() block")
    block = m.group(1)
    # Only the fprintf(stdout, ...) region
    fm = re.search(r"fprintf\(stdout,(.*?)\n\s*\);", block, re.S)
    if not fm:
        sys.exit("could not locate fprintf(stdout, ...) in show_help()")
    region = fm.group(1)
    # Drop // comment lines (upstream has ruler comments + commented-out
    # "continue" help between literals).
    lines = []
    for raw in region.split("\n"):
        # strip trailing // comment that isn't inside a string; the ruler
        # comments start with // at col 0-ish and contain only digits/comment.
        # A literal line looks like:  " text...\n"  possibly with a // prefix.
        s = raw.strip()
        if not s:
            continue
        if s.startswith("//"):
            continue
        # collect all "..." literals on this source line
        for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', s):
            # unescape \n and \" minimally
            lit = lit.replace('\\n', '\n').replace('\\"', '"').replace('\\\\', '\\')
            lines.append(lit)
    # Join then split on \n to get logical help lines
    joined = "".join(lines)
    help_lines = joined.split("\n")
    return help_lines


def parse_entries(help_lines):
    """Return list of dicts: {kind, cmd, desc, section}."""
    entries = []
    cur = None
    section = None
    for ln in help_lines:
        if ln.startswith("usage:"):
            entries.append({"kind": "usage", "text": ln})
            cur = None
            continue
        if ln.strip() == "":
            entries.append({"kind": "blank"})
            cur = None
            continue
        if not ln.startswith(" ") and ln.endswith(":"):
            section = ln
            entries.append({"kind": "section", "text": ln})
            cur = None
            continue
        # entry / continuation
        cmd = ln[CMD_COL:DESC_COL]
        desc = ln[DESC_COL:] if len(ln) > DESC_COL else ""
        if cmd.strip():
            # Upstream pads the command column to 27 chars when a description
            # shares the line. Two overflow shapes exist:
            #   a) the command fills the column exactly (no trailing spaces):
            #      "flash vendor_boot:RAMDISK [", "format[:FS_TYPE[:SIZE]] PART"
            #   b) the command crosses the column mid-token and the desc field
            #      starts with syntax, not prose: "flash:raw PARTITION KERNEL
            #      [RAMDISK [SECOND]]" (desc begins with '[').
            overflow = (len(cmd) == DESC_COL - CMD_COL and cmd == cmd.rstrip()) or \
                       (desc[:1] == "[")
            if overflow:
                cur = {"kind": "entry", "cmd": ln[CMD_COL:].rstrip(), "desc": "",
                       "section": section}
            else:
                cur = {"kind": "entry", "cmd": cmd.rstrip(), "desc": desc, "section": section}
            entries.append(cur)
        else:
            if cur:
                cur["desc"] += ("\n" if cur["desc"] else "") + desc
            else:
                entries.append({"kind": "raw", "text": ln})
    return entries


def cliteral(s: str) -> str:
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


# (section, cmd) -> message ID in fb_i18n.h. "--dtb DTB" appears twice
# (flashing + boot image), hence the section qualifier. Rows with no mapping
# keep id 0 and render English-only.
ID_MAP = {
    ("__usage__", ""): "kHelpUsage",
    ("flashing:", "update ZIP"): "kHUpdate",
    ("flashing:", "flashall"): "kHFlashall",
    ("flashing:", "flash PARTITION [FILENAME]"): "kHFlash",
    ("flashing:", "flash vendor_boot:RAMDISK [FILENAME]"): "kHFlashVb",
    ("flashing:", "--dtb DTB"): "kHDtbIf",
    ("basics:", "devices [-l]"): "kHDevices",
    ("basics:", "getvar NAME"): "kHGetvar",
    ("basics:", "reboot [bootloader]"): "kHReboot",
    ("locking/unlocking:", "flashing lock|unlock"): "kHFlashingLock",
    ("locking/unlocking:", "flashing lock_critical|unlock_critical"): "kHFlashingCrit",
    ("locking/unlocking:", "flashing get_unlock_ability"): "kHFlashingAbility",
    ("advanced:", "erase PARTITION"): "kHErase",
    ("advanced:", "format[:FS_TYPE[:SIZE]] PARTITION"): "kHFormat",
    ("advanced:", "set_active SLOT"): "kHSetActive",
    ("advanced:", "oem [COMMAND...]"): "kHOem",
    ("advanced:", "gsi wipe|disable|status"): "kHGsi",
    ("advanced:", "wipe-super [SUPER_EMPTY]"): "kHWipeSuper",
    ("advanced:", "create-logical-partition NAME SIZE"): "kHCreateLp",
    ("advanced:", "delete-logical-partition NAME"): "kHDeleteLp",
    ("advanced:", "resize-logical-partition NAME SIZE"): "kHResizeLp",
    ("advanced:", "snapshot-update cancel"): "kHSnapCancel",
    ("advanced:", "snapshot-update merge"): "kHSnapMerge",
    ("advanced:", "fetch PARTITION OUT_FILE"): "kHFetch",
    ("boot image:", "boot KERNEL [RAMDISK [SECOND]]"): "kHBoot",
    ("boot image:", "flash:raw PARTITION KERNEL [RAMDISK [SECOND]]"): "kHFlashRaw",
    ("boot image:", "--dtb DTB"): "kHDtb",
    ("boot image:", "--cmdline CMDLINE"): "kHCmdline",
    ("boot image:", "--base ADDRESS"): "kHBase",
    ("boot image:", "--kernel-offset"): "kHKernelOffset",
    ("boot image:", "--ramdisk-offset"): "kHRamdiskOffset",
    ("boot image:", "--tags-offset"): "kHTagsOffset",
    ("boot image:", "--dtb-offset"): "kHDtbOffset",
    ("boot image:", "--page-size BYTES"): "kHPageSize",
    ("boot image:", "--header-version VERSION"): "kHHeaderVersion",
    ("boot image:", "--os-version MAJOR[.MINOR[.PATCH]]"): "kHOsVersion",
    ("boot image:", "--os-patch-level YYYY-MM-DD"): "kHOsPatch",
    ("Android Things:", "stage IN_FILE"): "kHStage",
    ("Android Things:", "get_staged OUT_FILE"): "kHGetStaged",
    ("options:", "-w"): "kOWipe",
    ("options:", "-s SERIAL"): "kOSerial",
    ("options:", "-s tcp|udp:HOST[:PORT]"): "kOSerialNet",
    ("options:", "-S SIZE[K|M|G]"): "kOSparse",
    ("options:", "--force"): "kOForce",
    ("options:", "--slot SLOT"): "kOSlot",
    ("options:", "--set-active[=SLOT]"): "kOSetActive",
    ("options:", "--skip-secondary"): "kOSkipSecondary",
    ("options:", "--skip-reboot"): "kOSkipReboot",
    ("options:", "--disable-verity"): "kODisableVerity",
    ("options:", "--disable-verification"): "kODisableVerification",
    ("options:", "--disable-super-optimization"): "kODisableSuperOpt",
    ("options:", "--exclude-dynamic-partitions"): "kOExcludeDynamic",
    ("options:", "--disable-fastboot-info"): "kODisableFbInfo",
    ("options:", "--fs-options=OPTION[,OPTION]"): "kOFsOptions",
    ("options:", "--unbuffered"): "kOUnbuffered",
    ("options:", "--verbose, -v"): "kOVerbose",
    ("options:", "--version"): "kOVersion",
    ("options:", "--help, -h"): "kOHelp",
}
SECTION_IDS = {
    "flashing:": "kSecFlashing",
    "basics:": "kSecBasics",
    "locking/unlocking:": "kSecLocking",
    "advanced:": "kSecAdvanced",
    "boot image:": "kSecBootImage",
    "Android Things:": "kSecAndroidThings",
    "options:": "kSecOptions",
}


def main():
    if not os.path.exists(FB_CPP):
        sys.exit(f"missing {FB_CPP} (run scripts/fetch-aosp.sh)")
    src = open(FB_CPP, encoding="utf-8").read()
    help_lines = extract_help_lines(src)
    entries = parse_entries(help_lines)

    # Verify round-trip: reconstruct and compare to original help text.
    recon = []
    for e in entries:
        if e["kind"] == "usage":
            recon.append(e["text"])
        elif e["kind"] == "blank":
            recon.append("")
        elif e["kind"] == "section":
            recon.append(e["text"])
        elif e["kind"] == "raw":
            recon.append(e["text"])
        elif e["kind"] == "entry":
            first, *rest = e["desc"].split("\n")
            padded = e["cmd"].ljust(DESC_COL - CMD_COL)
            if padded.endswith(" "):
                recon.append(" " + padded + first)
                rest_first_done = True
            else:
                # Command occupies the full column: description starts on the
                # next line (upstream layout for long commands).
                recon.append(" " + e["cmd"])
                recon.append(" " * DESC_COL + first)
                rest_first_done = True
            for r in rest:
                recon.append(" " * DESC_COL + r)
            _ = rest_first_done
    recon_text = "\n".join(recon)
    orig_text = "\n".join(help_lines)
    if recon_text != orig_text:
        sys.stderr.write("WARNING: round-trip mismatch (non-fatal, inspect output)\n")
        import difflib
        for d in list(difflib.unified_diff(orig_text.split("\n"), recon_text.split("\n"),
                                           lineterm=""))[:30]:
            sys.stderr.write(d + "\n")

    # Emit the generated header: ordered list of HelpItem rows. `id` is the
    # fb_i18n.h message ID (0 = untranslated, render `a`/`b` verbatim).
    rows = []
    unmapped = []
    section = None
    for e in entries:
        k = e["kind"]
        if k == "usage":
            rows.append(f'    {{fbhelp::Usage, fbi18n::kHelpUsage, {cliteral(e["text"])}, nullptr}},')
        elif k == "blank":
            rows.append("    {fbhelp::Blank, 0, nullptr, nullptr},")
        elif k == "section":
            section = e["text"]
            sid = SECTION_IDS.get(section, 0)
            rows.append(f'    {{fbhelp::Section, fbi18n::{sid}, {cliteral(e["text"])}, nullptr}},'
                        if sid else
                        f'    {{fbhelp::Section, 0, {cliteral(e["text"])}, nullptr}},')
        elif k == "raw":
            rows.append(f'    {{fbhelp::Raw, 0, {cliteral(e["text"])}, nullptr}},')
        elif k == "entry":
            mid = ID_MAP.get((section, e["cmd"]), 0)
            if mid == 0:
                unmapped.append((section, e["cmd"]))
            mids = f"fbi18n::{mid}" if mid else "0"
            rows.append(
                f'    {{fbhelp::Entry, {mids}, {cliteral(e["cmd"])}, {cliteral(e["desc"])} }},')
    if unmapped:
        sys.stderr.write("unmapped help entries (render English-only):\n")
        for s, c in unmapped:
            sys.stderr.write(f"  [{s}] {c}\n")

    out = []
    out.append("/* GENERATED by scripts/gen-help-entries.py — do not edit. */")
    out.append("/* Help rows transcribed from upstream fastboot.cpp show_help(). */")
    out.append("#pragma once")
    out.append("")
    out.append('#include "fb_i18n.h"  // message IDs referenced below')
    out.append("")
    out.append("namespace fbhelp {")
    out.append("enum RowKind { Usage, Blank, Section, Raw, Entry };")
    out.append("struct HelpRow { RowKind kind; int id; const char* a; const char* b; };")
    out.append("static const HelpRow kHelpRows[] = {")
    for r in rows:
        out.append(r)
    out.append("};")
    out.append(f"static const size_t kHelpRowCount = {len(rows)};")
    out.append("}  // namespace fbhelp")
    out.append("")
    open(os.path.abspath(OUT), "w", encoding="utf-8").write("\n".join(out))
    print(f"wrote {os.path.abspath(OUT)}: {len(rows)} rows")


if __name__ == "__main__":
    main()
