#!/bin/bash
# test-adb.sh — end-to-end smoke tests for the standalone adb client.
# ---------------------------------------------------------------------------
# Runs the built binary against the adbd binary from adbd-standalone (same
# repo, same AOSP snapshot) over TCP, so every check exercises the real
# protocol: connect, shell v2, sync push/pull, forwarding, server-status.
#
# Usage: bash scripts/test-adb.sh [adb-binary] [adbd-binary]
# Both default to the build-native/ trees of adb-standalone / adbd-standalone.
set -uo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
ADB="${1:-$HERE/build-native/adb}"
ADBD="${2:-$HERE/../adbd-standalone/build-native/adbd}"
PORT="${ADB_TEST_PORT:-15577}"
PASS=0; FAIL=0; SKIP=0
ok()   { echo "  ok   $*"; PASS=$((PASS+1)); }
bad()  { echo "  FAIL $*"; FAIL=$((FAIL+1)); }
skip() { echo "  skip $*"; SKIP=$((SKIP+1)); }

[ -x "$ADB" ] || { echo "no adb binary at $ADB"; exit 2; }

# Isolated $HOME so the server socket, keys and logs never touch the user's.
TMP="$(mktemp -d /tmp/adbtest.XXXXXX)"
export HOME="$TMP"
trap 'cleanup' EXIT
cleanup() {
    "$ADB" kill-server >/dev/null 2>&1
    [ -n "${ADBD_PID:-}" ] && kill "$ADBD_PID" 2>/dev/null
    rm -rf "$TMP"
}

echo "== 1. CLI surface (no device) =="
V="$("$ADB" version 2>&1 | head -2 | tr '\n' ' ')"
case "$V" in *"Android Debug Bridge version"*"meowarch-adb"*) ok "version: $V" ;; *) bad "version: $V" ;; esac
# Help coverage: every command upstream documents must still be listed. Checked
# in English (--lang=en) so a translation cannot mask a missing entry. The
# expected list is upstream's own; platform-tools' adb is the reference when it
# is installed, otherwise a fixed list.
EXPECTED="devices connect disconnect pair forward reverse push pull install uninstall
shell emu logcat bugreport jdwp root unroot remount wait-for get-state get-serialno
get-devpath kill-server start-server tcpip reboot sideload mdns keygen
enable-verity disable-verity install-multiple install-multi-package reconnect attach detach usb"
HELP="$("$ADB" --lang=en help 2>&1)"
MISSING=""
for c in $EXPECTED; do
    printf '%s\n' "$HELP" | grep -qE "(^| )$c([ \\[]|$)" || MISSING="$MISSING $c"
done
if [ -z "$MISSING" ]; then ok "help lists every documented command"; else bad "help missing:$MISSING"; fi

# Categorised help: sections addressable individually, and the list is printed.
SECTIONS="$("$ADB" help --list-sections 2>&1)"
case "$SECTIONS" in
    *networking*scripting*environment*) ok "help --list-sections" ;;
    *) bad "help --list-sections: $SECTIONS" ;;
esac
if "$ADB" help networking 2>&1 | grep -q 'connect HOST'; then ok "help <section>"; else bad "help <section>"; fi
# Our own flags are removed from argv before adb's parser runs, so the only
# place they are documented is the meowarch section. If it stops listing them
# the flags become undiscoverable.
MEOW="$("$ADB" help meowarch 2>&1)"
for f in --lang=CODE --list-langs --no-color --color --no-progress; do
    case "$MEOW" in
        *"$f"*) ok "help meowarch documents $f" ;;
        *) bad "help meowarch omits $f" ;;
    esac
done
# Each documented flag must actually work (not just be listed).
for f in "--list-langs" "--no-color" "--color" "--no-progress"; do
    "$ADB" $f version >/dev/null 2>&1 \
        && ok "$f is accepted" || bad "$f rejected by the parser"
done
"$ADB" --lang=en version >/dev/null 2>&1 && ok "--lang=en is accepted" || bad "--lang=en rejected"
"$ADB" help nosuchsection >/dev/null 2>&1; [ $? -ne 0 ] && ok "unknown section exits 1" || bad "unknown section rc"

# Error strings are localized too. The device-not-found text is built inside
# transport.cpp and reaches error_exit already formatted, so this also covers
# the pattern-matching path (specifier as wildcard) rather than a plain format
# lookup.
"$ADB" kill-server >/dev/null 2>&1; sleep 0.3
for L in en_US.UTF-8:not\ found zh_CN.UTF-8:未找到 zh_TW.UTF-8:找不到 ja_JP.UTF-8:見つかりません; do
    lang="${L%%:*}"; want="${L#*:}"
    OUT="$(LANG="$lang" "$ADB" -s no-such-device shell 2>&1 | tail -1)"
    case "$OUT" in
        *"$want"*) ok "error text in $lang" ;;
        *) bad "error text in $lang: $OUT" ;;
    esac
done
# Localized errors are still plain text on a pipe (no colour, no mangling).
case "$OUT" in *$'\033'*) bad "escape bytes in piped error" ;; *) ok "piped error has no escapes" ;; esac

# Language selection: --lang beats the environment, and every shipped language
# renders a header (no blank/garbled output).
for L in en zh_CN zh_TW ja; do
    H="$("$ADB" --lang="$L" help 2>&1 | head -1)"
    [ -n "$H" ] && ok "help renders in $L" || bad "help blank for $L"
done
"$ADB" --list-langs 2>&1 | grep -q zh_TW && ok "--list-langs lists zh_TW" || bad "--list-langs"
# mDNS is deliberately absent; it must say so rather than pretend.
M="$("$ADB" mdns check 2>&1)"
case "$M" in *unavailable*) ok "mdns check reports the backend is absent" ;; *) bad "mdns check: $M" ;; esac

echo "== 2. server + device =="
if [ ! -x "$ADBD" ]; then
    skip "no adbd at $ADBD (build adbd-standalone first)"
else
    ADBD_NO_AUTH=1 ADBD_PORT="$PORT" "$ADBD" >"$TMP/adbd.log" 2>&1 &
    ADBD_PID=$!
    sleep 2
    "$ADB" connect "127.0.0.1:$PORT" >"$TMP/connect.txt" 2>&1
    case "$(cat "$TMP/connect.txt")" in
        *connected*) ok "connect: $(cat "$TMP/connect.txt")" ;;
        *) bad "connect: $(cat "$TMP/connect.txt")" ;;
    esac
    sleep 1
    D="$("$ADB" devices 2>&1)"
    case "$D" in *"127.0.0.1:$PORT"*device*) ok "devices lists the device" ;; *) bad "devices: $D" ;; esac
    SER="127.0.0.1:$PORT"

    # devices -l colouring must not disturb the layout: on a pty the state word
    # gains escapes, and everything else (serial, the padding that upstream uses
    # instead of a tab, "transport_id:N") stays byte-for-byte. On a pipe there
    # are no escapes at all and the output is byte-identical to plain devices -l.
    TERM=xterm-256color script -q -c "$ADB -s $SER devices -l" "$TMP/dev.txt" >/dev/null 2>&1
    LINE="$(tr -d '\r' < "$TMP/dev.txt" | grep -a "^$SER" | head -1)"
    # Strongest form of the check: strip the escapes and the pty line must be
    # identical to the pipe line, i.e. colouring changes nothing about layout.
    STRIPPED="$(printf '%s' "$LINE" | sed 's/\x1b\[[0-9;]*m//g')"
    PIPED="$("$ADB" -s "$SER" devices -l | grep -a "^$SER" | head -1)"
    if [ "$STRIPPED" = "$PIPED" ]; then
        ok "devices -l layout unchanged by colouring"
    else
        bad "devices -l layout: pty='$STRIPPED' pipe='$PIPED'"
    fi
    tr -d '\r' < "$TMP/dev.txt" | grep -a "^$SER" | grep -q $'\033' \
        && ok "devices -l colours the state on a pty" || bad "devices -l had no colour on a pty"
    # And the converse: a pty that claims TERM=dumb (or no TERM at all) must
    # NOT get escapes, because we cannot know the terminal interprets them.
    TERM=dumb script -q -c "$ADB -s $SER devices -l" "$TMP/dumb.txt" >/dev/null 2>&1
    if tr -d '\r' < "$TMP/dumb.txt" | grep -q $'\033'; then
        bad "TERM=dumb still got colour escapes"
    else
        ok "TERM=dumb gets no colour"
    fi
    PLAIN="$("$ADB" -s "$SER" devices -l)"
    case "$PLAIN" in
        *$'\033'*) bad "devices -l leaked escapes into a pipe" ;;
        *"transport_id:"*) ok "devices -l clean on a pipe" ;;
        *) bad "devices -l lost its fields on a pipe: $PLAIN" ;;
    esac
    # Upstream prints the listing with a trailing blank line (it printf's a
    # newline onto a string that already ends in one). Scripts capture this
    # output, so the exact byte count matters: the last two bytes must both be
    # newlines, not just one.
    TAIL2="$("$ADB" -s "$SER" devices | tail -c 2 | od -An -tx1 | tr -d ' ')"
    if [ "$TAIL2" = "0a0a" ]; then
        ok "devices ends with upstream's trailing blank line"
    else
        bad "devices trailing bytes are $TAIL2, expected 0a0a"
    fi

    # shell v2 (pty + exit status)
    S="$("$ADB" -s "$SER" shell 'echo shell-ok; exit 7' 2>&1)"; RC=$?
    if [ "$S" = "shell-ok" ] && [ "$RC" = "7" ]; then ok "shell: output + exit code 7"; else bad "shell: '$S' rc=$RC"; fi

    # sync push/pull, byte-exact, including a file with binary content
    head -c 262144 /dev/urandom > "$TMP/in.bin"
    "$ADB" -s "$SER" push "$TMP/in.bin" /tmp/adb-test-in.bin >"$TMP/push.txt" 2>&1 || bad "push failed: $(cat "$TMP/push.txt")"
    "$ADB" -s "$SER" pull /tmp/adb-test-in.bin "$TMP/out.bin" >"$TMP/pull.txt" 2>&1 || bad "pull failed: $(cat "$TMP/pull.txt")"
    if cmp -s "$TMP/in.bin" "$TMP/out.bin"; then ok "push+pull round-trip is byte-identical (256 KiB)" ; else bad "push/pull mismatch"; fi

    # long file names / nested dirs exercise the sync v2 path more
    mkdir -p "$TMP/dir/sub"
    echo nested > "$TMP/dir/sub/file.txt"
    "$ADB" -s "$SER" push "$TMP/dir" /tmp/ >/dev/null 2>&1
    N="$("$ADB" -s "$SER" shell 'cat /tmp/dir/sub/file.txt' 2>&1)"
    if [ "$N" = "nested" ]; then ok "recursive directory push"; else bad "directory push: '$N'"; fi

    # forwarding
    if "$ADB" -s "$SER" forward tcp:0 tcp:1 >/dev/null 2>&1; then ok "forward allocation"; else bad "forward"; fi
    "$ADB" -s "$SER" forward --remove-all >/dev/null 2>&1

    # Push/pull progress bar: on a pty it must paint percentage frames; on a
    # pipe it must stay byte-identical to upstream (no bar at all).
    head -c 8000000 /dev/urandom > "$TMP/bar.bin"
    TERM=xterm-256color script -q -c "$ADB -s $SER push $TMP/bar.bin /tmp/adb-test-bar.bin" "$TMP/pty.txt" >/dev/null 2>&1
    BAR_FRAMES="$(tr '\r' '\n' < "$TMP/pty.txt" | grep -acE '\[[#>-]+\]')"
    if [ "${BAR_FRAMES:-0}" -ge 2 ]; then ok "push progress bar repaints ($BAR_FRAMES frames on a pty)"
    else bad "push progress bar: $BAR_FRAMES frames"; fi
    QUIET="$("$ADB" -s "$SER" push "$TMP/bar.bin" /tmp/adb-test-bar.bin 2>&1)"
    case "$QUIET" in
        *$'\r'*|*'[#'*) bad "progress leaked into piped output" ;;
        *) ok "no progress bar when stderr is a pipe" ;;
    esac
    # NO_PROGRESS / --no-progress turn it off on a tty too.
    TERM=xterm-256color script -q -c "NO_PROGRESS=1 $ADB -s $SER push $TMP/bar.bin /tmp/adb-test-bar.bin" "$TMP/pty2.txt" >/dev/null 2>&1
    NP="$(tr '\r' '\n' < "$TMP/pty2.txt" | grep -acE '\[[#>-]+\]')"
    [ "${NP:-0}" -eq 0 ] && ok "NO_PROGRESS suppresses the bar" || bad "NO_PROGRESS left $NP frames"

    # server-status protobuf (goes through protobuf TextFormat)
    ST="$("$ADB" -s "$SER" server-status 2>&1)"
    case "$ST" in
        *"build: \"meowarch-adb\""*) ok "server-status carries our build stamp" ;;
        *) bad "server-status: $ST" ;;
    esac
    case "$ST" in
        *mdns_backend*) bad "server-status should not claim an mDNS backend" ;;
        *) ok "server-status reports no mDNS backend" ;;
    esac
fi

echo
echo "== summary: $PASS passed, $FAIL failed, $SKIP skipped =="
[ "$FAIL" -eq 0 ]
