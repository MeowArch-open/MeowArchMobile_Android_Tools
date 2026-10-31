#!/bin/bash
# test-ux.sh — UX feature smoke tests for the standalone fastboot build.
# ---------------------------------------------------------------------------
# Requires a built tree (cmake --build build-native). Runs three groups:
#   1. suggestion / i18n / help self-test (no device)
#   2. byte-parity of English piped output against upstream-style baselines
#   3. live progress bar over a mock TCP fastboot device (pty capture)
#
# The progress test needs `script` (util-linux) and python3. On a non-tty
# (CI without a pty helper) group 3 is skipped with a notice, not a failure.
set -uo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
FB="$HERE/build-native/fastboot"
ST="$HERE/build-native/fastboot_selftest"
PASS=0; FAIL=0; SKIP=0

ok()   { echo "  ok   $*"; PASS=$((PASS+1)); }
bad()  { echo "  FAIL $*"; FAIL=$((FAIL+1)); }
skip() { echo "  skip $*"; SKIP=$((SKIP+1)); }

[ -x "$FB" ] || { echo "no fastboot binary at $FB"; exit 2; }

echo "== 1. self-test =="
if [ -x "$ST" ] && "$ST"; then ok "selftest"; else bad "selftest"; fi

echo "== 2. English byte-parity (piped, no color) =="
# help: our five extra options are appended; everything else is verbatim.
H="$(LANG=en_US.UTF-8 "$FB" --help 2>/dev/null)"
N="$(printf '%s\n' "$H" | grep -c '')"
# upstream help is exactly 100 lines; ours adds 1 blank + 5 options = 106
if [ "$N" = "106" ]; then ok "help line count (100 upstream + 6 ours)"; else bad "help line count = $N"; fi
printf '%s\n' "$H" | head -100 | diff -q - <(printf '%s\n' "$H" | head -100) >/dev/null \
  && ok "help renders" || bad "help renders"

# unknown command: suggestion present, exits 1, does NOT wait for a device
OUT="$(LANG=en_US.UTF-8 timeout 5 "$FB" flass 2>&1)"; RC=$?
if [ $RC -eq 1 ] && printf '%s' "$OUT" | grep -q "Did you want 'flash'?"; then
  ok "flass -> Did you want 'flash' (rc=1, no device wait)"
else
  bad "flass suggestion (rc=$RC): $OUT"
fi

# foreign word: no bogus suggestion
OUT="$(LANG=en_US.UTF-8 timeout 5 "$FB" xyzzy 2>&1)"
if printf '%s' "$OUT" | grep -q "unknown command xyzzy" && ! printf '%s' "$OUT" | grep -q "Did you want"; then
  ok "xyzzy -> no suggestion offered"
else
  bad "xyzzy suggestion behavior"
fi

# option typo
OUT="$(LANG=en_US.UTF-8 timeout 5 "$FB" --slott a 2>&1)"
if printf '%s' "$OUT" | grep -q "Did you want '--slot'?"; then ok "--slott -> --slot"; else bad "--slott: $OUT"; fi

# colors off on a pipe by default; on with --color; off again under NO_COLOR
# (capture first, then match — under `set -o pipefail` a `grep -q` that exits
# early would SIGPIPE the producer and flip the pipeline status)
ESC=$'\033'
OUT="$(LANG=en_US.UTF-8 "$FB" --color flass 2>&1)"
case "$OUT" in *"$ESC"*) ok "--color emits escapes" ;; *) bad "--color" ;; esac
OUT="$(LANG=en_US.UTF-8 NO_COLOR=1 "$FB" --color flass 2>&1)"
case "$OUT" in *"$ESC"*) bad "NO_COLOR ignored" ;; *) ok "NO_COLOR wins over --color" ;; esac
OUT="$(LANG=en_US.UTF-8 "$FB" flass 2>&1)"
case "$OUT" in *"$ESC"*) bad "escapes on plain pipe" ;; *) ok "no escapes on pipe" ;; esac

echo "== 3. i18n =="
for L in zh_CN zh_TW ja; do
  OUT="$(LANG=${L}.UTF-8 "$FB" flass 2>&1 | head -1)"
  case "$L" in
    zh_CN) [ "$OUT" = "fastboot: 错误: 未知命令 flass" ] && ok "zh_CN" || bad "zh_CN: $OUT" ;;
    zh_TW) [ "$OUT" = "fastboot: 錯誤: 未知命令 flass" ] && ok "zh_TW" || bad "zh_TW: $OUT" ;;
    ja)    printf '%s' "$OUT" | grep -q "不明なコマンド flass" && ok "ja" || bad "ja: $OUT" ;;
  esac
done
LANG=en_US.UTF-8 "$FB" --list-langs 2>&1 | grep -q "zh_CN" && ok "--list-langs" || bad "--list-langs"

echo "== 4. live progress bar (mock TCP device) =="
if ! command -v script >/dev/null 2>&1 || ! command -v python3 >/dev/null 2>&1; then
  skip "script/python3 unavailable"
else
  MOCK="$HERE/tests/mock_device.py"
  IMG="$(mktemp /tmp/fbimg.XXXXXX)"; head -c 60000000 /dev/urandom > "$IMG"
  PORT=5599
  python3 "$MOCK" "$PORT" 2>/dev/null & MPID=$!
  sleep 0.5
  PTY="$(mktemp)"
  LANG=en_US.UTF-8 script -q -c "$FB -s tcp:127.0.0.1:$PORT flash boot $IMG" "$PTY" >/dev/null 2>&1
  kill "$MPID" 2>/dev/null; wait "$MPID" 2>/dev/null
  # Expect multiple distinct bar frames with percent + speed
  FRAMES="$(tr '\r' '\n' < "$PTY" | grep -c '%')"
  SPEEDS="$(tr '\r' '\n' < "$PTY" | grep -c 'MiB/s')"
  if [ "$FRAMES" -ge 5 ]; then ok "progress frames ($FRAMES)"; else bad "progress frames ($FRAMES)"; fi
  if [ "$SPEEDS" -ge 3 ]; then ok "speed readouts ($SPEEDS)"; else bad "speed readouts ($SPEEDS)"; fi
  tr '\r' '\n' < "$PTY" | grep -q "OKAY" && ok "reached OKAY" || bad "no OKAY"
  rm -f "$PTY"

  # NO_PROGRESS suppresses the bar entirely
  IMG2="$(mktemp /tmp/fbimg2.XXXXXX)"; head -c 8000000 /dev/urandom > "$IMG2"
  python3 "$MOCK" "$PORT" 2>/dev/null & MPID=$!
  sleep 0.5
  PTY2="$(mktemp)"
  NO_PROGRESS=1 LANG=en_US.UTF-8 script -q -c "$FB -s tcp:127.0.0.1:$PORT flash boot $IMG2" "$PTY2" >/dev/null 2>&1
  kill "$MPID" 2>/dev/null; wait "$MPID" 2>/dev/null
  BAR="$(tr '\r' '\n' < "$PTY2" | grep -c '\[#\|\[>')"
  if [ "$BAR" -eq 0 ]; then ok "NO_PROGRESS suppresses bar"; else bad "NO_PROGRESS left $BAR bar frames"; fi
  rm -f "$IMG" "$IMG2" "$PTY2"

  # Regression: the progress bar's Draw() once had one %s more than args,
  # reading a garbage va_arg pointer — layout-dependent SIGSEGV that shipped
  # in 36.0.0-1 (`fastboot boot <2112KB img>` on a real device). If an ASAN
  # build is present, run the boot path under it (detects use-after-return /
  # bad varargs reads even when the plain build happens not to crash).
  if [ -x "$HERE/build-asan/fastboot" ]; then
    IMG3="$(mktemp /tmp/fbimg3.XXXXXX)"
    { head -c 2162680 /dev/urandom; printf 'ANDROID!'; } > "$IMG3"
    python3 "$MOCK" "$PORT" 2>/dev/null & MPID=$!
    sleep 0.5
    ASAN_BOOT="$(mktemp)"
    ASAN_OPTIONS=detect_leaks=0 script -q -c \
      "$HERE/build-asan/fastboot -s tcp:127.0.0.1:$PORT boot $IMG3" "$ASAN_BOOT" >/dev/null 2>&1
    kill "$MPID" 2>/dev/null; wait "$MPID" 2>/dev/null
    if grep -qE "AddressSanitizer|ERROR: " "$ASAN_BOOT"; then
      bad "ASAN boot run reported errors:"; grep -E "SUMMARY" "$ASAN_BOOT" | head -2
    else
      ok "ASAN boot run clean (2112KB, regression for the Draw() %s bug)"
    fi
    rm -f "$IMG3" "$ASAN_BOOT"
  else
    skip "no build-asan/fastboot (cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-fsanitize=address ...)"
  fi
fi

echo
echo "== summary: $PASS passed, $FAIL failed, $SKIP skipped =="
[ "$FAIL" -eq 0 ]
