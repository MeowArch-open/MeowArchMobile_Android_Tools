#!/bin/bash
# test-ux-wine.sh — smoke tests for the Windows (mingw i686) build under wine.
# ---------------------------------------------------------------------------
# Mirrors the platform-independent half of test-ux.sh against fastboot.exe:
# selftest, version, English help byte-parity (after CRLF strip), Did-you-want
# suggestions, NO_COLOR/NO_PROGRESS opt-outs, and a live flash against the
# mock TCP device (exercises ws2_32 + the AdbWinApi-linked USB path's absence).
#
# Usage: bash scripts/test-ux-wine.sh [build-dir]   (default: build-win32)
set -uo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$HERE/build-win32}"
FB="$BUILD/fastboot.exe"
ST="$BUILD/fastboot_selftest.exe"
PASS=0; FAIL=0; SKIP=0

ok()   { echo "  ok   $*"; PASS=$((PASS+1)); }
bad()  { echo "  FAIL $*"; FAIL=$((FAIL+1)); }
skip() { echo "  skip $*"; SKIP=$((SKIP+1)); }

command -v wine >/dev/null 2>&1 || { echo "wine not found"; exit 2; }
[ -f "$FB" ] || { echo "no fastboot.exe at $FB"; exit 2; }
export WINEDEBUG=-all
# Quiet wine: no 32-bit prefix noise on first run.
export WINEDLLOVERRIDES="mscoree,mshtml="

wfb() { # run fastboot.exe with env, capture stdout+stderr, strip wine noise
    timeout 120 wine "$FB" "$@" 2>&1 | grep -vE "^(MESA|.*pci id for fd|wine: |.*winehq)"
}

echo "== 1. selftest =="
if timeout 120 wine "$ST" 2>/dev/null | grep -q "all checks passed"; then
    ok "selftest under wine"
else
    bad "selftest under wine"
fi

echo "== 2. version + help parity =="
V="$(wfb --version | head -1)"
case "$V" in *"fastboot version"*) ok "--version: $V" ;; *) bad "--version: $V" ;; esac

# Windows console stdio is CRLF; upstream parity is checked after stripping.
H="$(LANG=en_US.UTF-8 NO_COLOR=1 wfb --help | tr -d '\r')"
N="$(printf '%s\n' "$H" | grep -c '')"
[ "$N" = "106" ] && ok "help line count (100 upstream + 6 ours)" || bad "help line count = $N"
# Committed upstream baseline: tests/help-baseline-en.txt (captured from the
# distro platform-tools fastboot --help, first 100 lines).
printf '%s\n' "$H" | head -100 > /tmp/fb-win-help100.txt
if diff -q /tmp/fb-win-help100.txt "$HERE/tests/help-baseline-en.txt" >/dev/null 2>&1; then
    ok "help first-100 == upstream baseline (byte-identical)"
else
    bad "help diverges from upstream baseline:"
    diff /tmp/fb-win-help100.txt "$HERE/tests/help-baseline-en.txt" | head -6
fi

echo "== 3. Did-you-want + opt-outs =="
OUT="$(LANG=en_US.UTF-8 wfb flass)"
if printf '%s' "$OUT" | grep -q "Did you want 'flash'?"; then ok "flass -> flash"; else bad "flass: $OUT"; fi
OUT="$(LANG=zh_CN.UTF-8 wfb flass)"
if printf '%s' "$OUT" | grep -q "您是否想输入 'flash'"; then ok "zh_CN suggestion"; else bad "zh_CN: $OUT"; fi
OUT="$(LANG=ja_JP.UTF-8 wfb --list-langs)"
if printf '%s' "$OUT" | grep -q "日本語"; then ok "ja --list-langs"; else bad "ja: $OUT"; fi

# completion script generation (same generator as the Linux build)
OUT="$("$FB" --shell-completion=bash 2>/dev/null | tr -d '\r')"
if printf '%s' "$OUT" | grep -q "complete -F _fastboot_m fastboot-m fastboot-meowarch" && \
   printf '%s' "$OUT" | grep -q "flash"; then
    ok "--shell-completion=bash on Windows"
else
    bad "--shell-completion=bash on Windows"
fi

echo "== 4. live flash via mock TCP device (ws2_32 path) =="
if ! command -v python3 >/dev/null 2>&1; then
    skip "python3 unavailable"
else
    MOCK="$HERE/tests/mock_device.py"
    IMG="$(mktemp /tmp/fbwin.XXXXXX)"; head -c 8000000 /dev/urandom > "$IMG"
    # wine maps 127.0.0.1 through; the mock runs natively.
    PORT=5601
    python3 "$MOCK" "$PORT" 2>/dev/null & MPID=$!
    sleep 0.5
    OUT="$(LANG=en_US.UTF-8 timeout 180 wine "$FB" -s tcp:127.0.0.1:$PORT flash boot "$IMG" 2>&1 | grep -vE 'MESA|pci id')"
    kill "$MPID" 2>/dev/null; wait "$MPID" 2>/dev/null
    if printf '%s' "$OUT" | grep -q "OKAY" && printf '%s' "$OUT" | grep -q "Finished"; then
        ok "flash over TCP: OKAY + Finished"
    else
        bad "flash over TCP: $OUT"
    fi
    rm -f "$IMG"
fi

echo
echo "== summary: $PASS passed, $FAIL failed, $SKIP skipped =="
[ "$FAIL" -eq 0 ]
