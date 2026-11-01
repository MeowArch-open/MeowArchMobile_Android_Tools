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
H="$(LANG=en_US.UTF-8 NO_COLOR=1 "$FB" --help 2>/dev/null)"
N="$(printf '%s\n' "$H" | grep -c '')"
# upstream help is exactly 100 lines. Ours appends one blank line, the flash-x
# command row (4 lines: the command, then three description lines) and the five
# option rows (1 line each) = 109. The first 100 stay byte-identical, which the
# baseline diff below is what actually guarantees.
if [ "$N" = "109" ]; then ok "help line count (100 upstream + 9 ours)"; else bad "help line count = $N"; fi
# byte-parity against the committed upstream baseline (tests/help-baseline-en.txt,
# captured from the distro platform-tools fastboot --help).
if printf '%s\n' "$H" | head -100 | diff -q - "$HERE/tests/help-baseline-en.txt" >/dev/null 2>&1; then
  ok "help first-100 == upstream baseline (byte-identical)"
else
  bad "help diverges from upstream baseline"
fi

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

# Regression: a super image reaches the driver as a few hundred-megabyte raw
# chunks (libsparse hands one callback per chunk). When those became single
# transport writes, the bar froze for the whole chunk and then jumped — big
# partitions looked stuck while small ones looked smooth. Self-contained: it
# runs its own mock device over a pty.
if command -v python3 >/dev/null 2>&1; then
  SPX="$(python3 "$HERE/tests/test_sparse_progress.py" "$FB" 2>&1 | tail -1)"
  case "$SPX" in
    *PASS*) ok "progress stays live through one huge sparse chunk (no freeze/jump)" ;;
    *)      bad "sparse progress: $SPX" ;;
  esac
else
  skip "python3 unavailable (sparse-progress regression)"
fi

# flash-x: extract a partition out of a payload.bin and flash it. Drives the
# real binary over TCP and compares the transmitted bytes with what the payload
# generator put in, so the whole chain is covered (manifest decode, xz/bz2/raw
# decompression, extent placement, SHA-256 verification, temp-file handoff).
if command -v python3 >/dev/null 2>&1; then
  FX="$(python3 "$HERE/tests/test_flash_x.py" "$FB" 2>&1 | tail -1)"
  case "$FX" in
    *"0 failed"*) ok "flash-x end-to-end ($FX)" ;;
    *)            bad "flash-x: $FX" ;;
  esac
else
  skip "python3 unavailable (flash-x end-to-end)"
fi

echo "== 5. shell completions (bash/zsh/fish) =="
for SH in bash zsh fish; do
  OUT="$(LANG=en_US.UTF-8 "$FB" --shell-completion=$SH 2>/dev/null)"
  RC=$?
  if [ $RC -eq 0 ] && [ "$(printf '%s\n' "$OUT" | grep -c '')" -gt 30 ]; then
    ok "--shell-completion=$SH generated ($(printf '%s\n' "$OUT" | wc -w) words)"
  else
    bad "--shell-completion=$SH (rc=$RC, $(printf '%s\n' "$OUT" | grep -c '') lines)"
  fi
  # the vocabulary must come from the binary: a command, a long option, and
  # our own additions must all appear. (fish spells long options as
  # `-l 'slot'` without the dashes, so match those separately.)
  if [ "$SH" = "fish" ]; then
    for WANT in "flash" "'slot'" "zh_TW" "getvar"; do
      printf '%s' "$OUT" | grep -q -- "$WANT" || bad "$SH script missing '$WANT'"
    done
  else
    for WANT in "flash" "--slot" "zh_TW" "getvar"; do
      printf '%s' "$OUT" | grep -q -- "$WANT" || bad "$SH script missing '$WANT'"
    done
  fi
done
printf '%s\n' "$("$FB" --shell-completion=bash)" > /tmp/fbcomp.bash
bash -n /tmp/fbcomp.bash && ok "bash script parses (bash -n)" || bad "bash -n failed"
if command -v fish >/dev/null 2>&1; then
  "$FB" --shell-completion=fish > /tmp/fbcomp.fish
  fish -n /tmp/fbcomp.fish && ok "fish script parses (fish -n)" || bad "fish -n failed"
else
  skip "fish not installed"
fi
OUT="$("$FB" --shell-completion=csh 2>&1)"; RC=$?
if [ $RC -eq 1 ] && printf '%s' "$OUT" | grep -q "unknown shell 'csh'"; then
  ok "unknown shell errors (rc=1)"
  # Each shell autoloads a completion by *command name*, so the package must
  # install the script under both fastboot-m and fastboot-meowarch; and the
  # scripts must register for both (otherwise the short name gets nothing).
  for sh in bash zsh fish; do
    case $sh in
      bash) out=$("$FB" --shell-completion=bash); want='complete -F _fastboot_m fastboot-m fastboot-meowarch' ;;
      zsh)  out=$("$FB" --shell-completion=zsh);  want='#compdef fastboot-m fastboot-meowarch' ;;
      fish) out=$("$FB" --shell-completion=fish); want='complete -c fastboot-m -c fastboot-meowarch' ;;
    esac
    case "$out" in
      *"$want"*) ok "$sh completion registers both command names" ;;
      *) bad "$sh completion missing '$want'" ;;
    esac
  done
else
  bad "unknown shell: rc=$RC out=$OUT"
fi
# bash functional drive: source the generated script and check candidate sets
if [ -f /usr/share/bash-completion/bash_completion ] || true; then
  # fixture dir for the flash-second-argument file checks
  rm -rf /tmp/ct && mkdir -p /tmp/ct/sub
  touch /tmp/ct/boot.img /tmp/ct/a.img /tmp/ct/note.txt
  cat > /tmp/fbcomp-drive.sh <<EOS
source /tmp/fbcomp.bash
PASS=0; FAIL=0
try() {
  local desc="\$1" want="\$2"; shift 2
  COMP_WORDS=("\$@"); COMP_CWORD=\$(( \${#COMP_WORDS[@]} - 1 ))
  COMPREPLY=()
  _fastboot_m
  case "\${COMPREPLY[*]}" in *"\$want"*) PASS=\$((PASS+1));; *) echo "  FAIL \$desc"; FAIL=\$((FAIL+1));; esac
}
try "cmds"  "flash"         fastboot-m ""
try "parts" "vendor_boot"   fastboot-m "flash" ""
try "vars"  "current-slot"  fastboot-m "getvar" ""
try "langs" "zh_TW"         fastboot-m "--lang" ""
try "slots" "other"         fastboot-m "--slot" ""
# flash PARTITION [FILENAME]: argument 1 is the partition, argument 2 is a file
# (regression: the second slot used to complete command/partition names)
cd /tmp/ct || exit 1
try_file() {
  local desc="\$1" want="\$2"; shift 2
  COMP_WORDS=("\$@"); COMP_CWORD=\$(( \${#COMP_WORDS[@]} - 1 ))
  COMPREPLY=()
  _fastboot_m
  case "\${COMPREPLY[*]}" in *"\$want"*) PASS=\$((PASS+1));; *) echo "  FAIL \$desc (got: \${COMPREPLY[*]})"; FAIL=\$((FAIL+1));; esac
}
try_file "flash2->img"  "boot.img"   fastboot-m flash boot_a ""
try_file "flash2->./img" "./boot.img" fastboot-m flash boot_a "./"
try_file "flash:raw2->img" "boot.img" fastboot-m flash:raw boot ""
# flash-x PARTITION PAYLOAD [NAME]: partition, then a file, then a payload entry
try_file "flash-x1->payload" "note.txt" fastboot-m flash-x boot ""
COMP_WORDS=(fastboot-m flash-x boot ""); COMP_CWORD=3; COMPREPLY=(); _fastboot_m
case "\${COMPREPLY[*]}" in
  *vendor_boot*) echo "  FAIL flash-x3 should offer payload entries"; FAIL=\$((FAIL+1));;
  *) PASS=\$((PASS+1));;
esac
echo "drive: \$PASS passed, \$FAIL failed"
[ "\$FAIL" -eq 0 ]
EOS
  if bash /tmp/fbcomp-drive.sh; then ok "bash functional drive (10 checks)"; else bad "bash functional drive"; fi
fi
# zsh functional drive (17 vocab checks) when a zsh is available
if command -v zsh >/dev/null 2>&1; then
  "$FB" --shell-completion=zsh > /tmp/fbcomp-zsh
  if zsh -f "$HERE/scripts/test-completions.zsh" /tmp/fbcomp-zsh | tail -1 | grep -q " 0 failed"; then
    ok "zsh functional drive (scripts/test-completions.zsh)"
  else
    bad "zsh functional drive"; zsh -f "$HERE/scripts/test-completions.zsh" /tmp/fbcomp-zsh 2>&1 | tail -5
  fi
else
  skip "zsh not installed (functional drive skipped; syntax-level checks above still ran)"
fi
# fish functional drive via `complete -C` (needs the binary on PATH as
# fastboot-m, matching the completion's -c name)
if command -v fish >/dev/null 2>&1; then
  mkdir -p /tmp/fbcomp-fishbin /tmp/fbcomp-fishcomp
  ln -sf "$(readlink -f "$FB")" /tmp/fbcomp-fishbin/fastboot-m
  "$FB" --shell-completion=fish > /tmp/fbcomp-fishcomp/fastboot-m.fish
  FISHPASS=0; FISHFAIL=0
  fish_try() {
    # source the generated script, then ask fish for candidates
    if fish -c "source /tmp/fbcomp-fishcomp/fastboot-m.fish; complete -C 'fastboot-m $2'" 2>/dev/null | grep -q -- "$3"; then
      ok "fish drive: $1"; FISHPASS=$((FISHPASS+1))
    else
      bad "fish drive: $1"; FISHFAIL=$((FISHFAIL+1))
    fi
  }
  # PATH must include the wrapper for `complete -C` to resolve the command.
  export PATH="/tmp/fbcomp-fishbin:$PATH"
  fish_try "flas->flash"      "flas"          "flash"
  fish_try "flash->parts"     "flash "        "vendor_boot"
  fish_try "getvar->vars"     "getvar "       "current-slot"
  fish_try "--slot->slots"    "--slot "       "other"
  fish_try "--lang->langs"    "--lang "       "zh_TW"
  # flash PARTITION *FILENAME*: the second argument must offer files, not
  # partitions (the bug: fish's rule ignored the argument position)
  ( cd /tmp/ct && touch boot.img a.img && mkdir -p sub )
  fish_neg() {
    if fish -c "source /tmp/fbcomp-fishcomp/fastboot-m.fish; cd /tmp/ct; complete -C 'fastboot-m $2'" 2>/dev/null | grep -qx -- "$3"; then
      bad "fish drive: $1"; FISHFAIL=$((FISHFAIL+1))
    else
      ok "fish drive: $1"; FISHPASS=$((FISHPASS+1))
    fi
  }
  if fish -c "source /tmp/fbcomp-fishcomp/fastboot-m.fish; cd /tmp/ct; complete -C 'fastboot-m flash boot_a '" 2>/dev/null | grep -qx -- "boot.img"; then
    ok "fish drive: flash2->img"; FISHPASS=$((FISHPASS+1))
  else
    bad "fish drive: flash2->img"; FISHFAIL=$((FISHFAIL+1))
  fi
  fish_neg "flash2 no partitions" "flash boot_a " "vendor_boot"
  fish_try "flash-x3->entries" "flash-x boot p.bin " "vendor_boot"
  # the PAYLOAD position offers files, and only from the right directory
  if fish -c "source /tmp/fbcomp-fishcomp/fastboot-m.fish; cd /tmp/ct; complete -C 'fastboot-m flash-x boot '" 2>/dev/null | grep -qx -- "note.txt"; then
    ok "fish drive: flash-x2->files"; FISHPASS=$((FISHPASS+1))
  else
    bad "fish drive: flash-x2->files"; FISHFAIL=$((FISHFAIL+1))
  fi
  fish_neg "flash-x2 no partitions" "flash-x boot " "vendor_boot"
  fish_neg "flash2 no notes.txt"  "flash boot_a " "note.txt"
  if [ "$FISHFAIL" -eq 0 ]; then ok "fish drive: all $FISHPASS checks"; fi
  export PATH="${PATH#/tmp/fbcomp-fishbin:}"
else
  skip "fish not installed (functional drive skipped)"
fi

echo
echo "== summary: $PASS passed, $FAIL failed, $SKIP skipped =="
[ "$FAIL" -eq 0 ]
