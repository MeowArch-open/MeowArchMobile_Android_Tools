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
# The subcommands the standalone build is expected to keep. Anything missing
# here means a source file stopped being compiled.
for c in devices connect disconnect pair forward reverse push pull install uninstall \
         shell emu logcat bugreport jdwp root remount wait-for get-state get-serialno; do
    if "$ADB" help 2>&1 | grep -qE "^ +$c\b" || "$ADB" help 2>&1 | grep -qE "\b$c\b"; then
        :
    else
        bad "help lost the '$c' subcommand"
    fi
done
ok "help still lists the expected subcommands"
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
