#!/bin/bash
# test-adb-wine.sh — smoke tests for the Windows (mingw i686) adb.exe under wine.
# ---------------------------------------------------------------------------
# Mirrors the platform-independent half of test-adb.sh against adb.exe, then
# does what the Linux suite cannot: drives the Windows socket layer (ws2_32)
# end-to-end against the NATIVE Linux adbd from adbd-standalone over TCP. If
# that transfer is byte-exact, the win32 build is not just linking — its
# sysdeps_win32 FD indirection, adb_writev over WSABUF, socketpair emulation and
# sync framing all work.
#
# Usage: bash scripts/test-adb-wine.sh [build-dir] [adbd-binary]
#        (defaults: build-win32, ../adbd-standalone/build-native/adbd)
#
# Requires: wine, and a built adbd (skipped, not failed, when absent).
set -uo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$HERE/build-win32}"
ADBD="${2:-$HERE/../adbd-standalone/build-native/adbd}"
ADB="$BUILD/adb.exe"
SUGGEST="$BUILD/test_suggest.exe"
# The wine adb server gets its own port so it cannot collide with a real adb
# server the developer already has running on 5037, and its own adbd port.
PORT="${ADB_WIN_TEST_PORT:-15599}"
SERVER_PORT="${ADB_WIN_SERVER_PORT:-15600}"
PASS=0; FAIL=0; SKIP=0

ok()   { echo "  ok   $*"; PASS=$((PASS+1)); }
bad()  { echo "  FAIL $*"; FAIL=$((FAIL+1)); }
skip() { echo "  skip $*"; SKIP=$((SKIP+1)); }

command -v wine >/dev/null 2>&1 || { echo "wine not found"; exit 2; }
[ -f "$ADB" ] || { echo "no adb.exe at $ADB"; exit 2; }

export WINEDEBUG=-all
# Quiet wine: no 32-bit prefix noise on first run, no Mesa/GL chatter.
export WINEDLLOVERRIDES="mscoree,mshtml="
# adb.exe's server socket; see PORT above.
export ADB_SERVER_SOCKET="tcp:localhost:$SERVER_PORT"

TMP="$(mktemp -d /tmp/adbwine.XXXXXX)"
trap 'cleanup' EXIT
cleanup() {
    timeout 120 wine "$ADB" kill-server >/dev/null 2>&1
    [ -n "${ADBD_PID:-}" ] && kill "$ADBD_PID" 2>/dev/null
    rm -rf "$TMP"
}

# Run adb.exe and keep only its own output. wine's graphics stack prints MESA /
# pci-id / wayland lines to stderr on some hosts; they would land in every
# capture below and make the greps meaningless.
#
# The filtering happens in a SECOND pass over a temp file, never in a pipe off
# the process. That is load-bearing: the invocation that spawns the adb server
# hands that server the same stdout/stderr, so the server inherits the write end
# of the pipe and grep never sees EOF — the call hangs until the timeout even
# though adb finished. (This is how the suite stalled on `mdns check`, the first
# command that needs a server.) With a file, EOF comes when adb's own handle
# closes; the inherited duplicate is harmless because nothing waits on it.
WINE_TMP="$TMP/wine-cap.txt"
WINE_RC=0
wadb() {
    timeout 180 wine "$ADB" "$@" > "$WINE_TMP" 2>&1
    # Recorded in a global, not just returned: the grep below is the last
    # command in the function, so $? would be grep's status and every
    # `wadb ... && bad` or `RC=$?` check would silently pass. (This is exactly
    # how the exit-code and unknown-shell checks were wrong for a run.)
    WINE_RC=$?
    grep -vE "^(MESA|.*pci id for fd|wine: |.*winehq|.*waylanddrv|.*err:)" "$WINE_TMP"
    return "$WINE_RC"
}

# NOTE: never pipe a producer straight into `grep -q` under `set -o pipefail`.
# grep exits at the first match, the writer takes SIGPIPE (141) and the pipeline
# fails — the check then reports FAIL for a passing build. Everything below
# captures to a file first.

# Pre-flight, before any check can produce a misleading result.
#
# Every check below greps a captured file. If wine cannot run the exe at all, or
# the client cannot spawn its own server, those greps fail in ways that read like
# product bugs ("FAIL mdns check", "FAIL connect", ...) — ten identical failures
# pointing nowhere. One CI run did exactly that. So the two environmental
# preconditions are asserted here, once, with wine's own diagnostics, and the
# suite exits rather than reporting noise.
echo "== 0. pre-flight =="
wadb version > "$TMP/preflight.txt"
if grep -q "Android Debug Bridge version" "$TMP/preflight.txt"; then
    ok "wine runs adb.exe"
else
    echo "  FATAL wine cannot run $ADB:"; cat "$TMP/preflight.txt" | head -8
    echo "== summary: 0 passed, 1 failed, 0 skipped =="
    exit 1
fi
# The server is spawned with CreateProcessW + DETACHED_PROCESS and inherited pipe
# handles (adb.cpp's launch_server), which is the part that differs most between
# wine builds. Retry a few times: the first spawn also has to build the prefix.
SRV_OK=0
for i in 1 2 3 4 5; do
    wadb start-server > "$TMP/srv.txt"
    if ! grep -q "failed to start daemon" "$TMP/srv.txt"; then SRV_OK=1; break; fi
    sleep 3
done
if [ "$SRV_OK" = "1" ]; then
    ok "the client can spawn its own server (attempt $i)"
else
    echo "  FATAL adb.exe cannot spawn its server under this wine build:"
    cat "$TMP/srv.txt" | head -8
    WINEDEBUG=+process,+server,+seh timeout 120 wine "$ADB" start-server 2>&1 | tail -25
    echo "== summary: 0 passed, 1 failed, 0 skipped =="
    exit 1
fi

# Regression for patches/0006. adb's Windows server builds a hidden window for
# power notifications, and upstream guarded that with LOG(FATAL) — so a server
# with no desktop died before writing its ack and the client saw only "could not
# read ok from ADB Server" / "failed to start daemon". A whole-daemon abort
# caused by an optimization, on hosts where having no desktop is normal
# (session 0, a service account, any headless CI runner).
#
# Recreate the precondition rather than inherit it: DISPLAY and WAYLAND_DISPLAY
# are unset, so wine has no display to create a window on whichever machine this
# runs on. Verified to reach the path on a Wayland host and in a container with
# no X at all; the run prints
#   err:winediag:nodrv_CreateWindow Application tried to create a window, but no
#   driver could be loaded.
# followed by the warning patch 0006 substituted for the abort.
#
# Two parts, because they can observe different things. A DETACHED spawn does not
# show the server's later output: launch_server() returns as soon as it has the
# ack, and the window failure happens seconds after that, so the warning never
# reaches the client's capture — a detached-only check cannot tell "warned and
# carried on" from "window happened to work". Part (a) therefore runs the server
# in the FOREGROUND, where the warning is visible, and then proves it still
# answers a client: that is exactly the property 0006 restores. Part (b) covers
# the detached path a user actually hits.
echo "== 0b. the server survives having no display =="
timeout 120 wine "$ADB" kill-server >/dev/null 2>&1
sleep 1

# (a) foreground: the warning must appear AND the server must still serve.
FG_SOCK="tcp:localhost:$((SERVER_PORT + 1))"
( unset DISPLAY WAYLAND_DISPLAY
  WINEDEBUG=-all ADB_SERVER_SOCKET="$FG_SOCK" \
      timeout 120 wine "$ADB" server nodaemon > "$TMP/fg-nodisp.txt" 2>&1 ) &
FG_PID=$!
# Poll for the warning, and decide on the SERVICE rather than on process
# liveness. Two reasons: the meaningful property is "does it still answer", and
# `kill -0` on the backgrounded subshell is not a reliable proxy — measured on
# this host, it reported the PID gone seconds before wine's own process tree was,
# while a client connected to the server fine. Every combination is asserted, so
# a wine whose display-less window creation succeeds cannot make this pass
# vacuously either.
WARN_SEEN=0
for _ in $(seq 1 20); do
    sleep 1
    grep -q "power notifications disabled" "$TMP/fg-nodisp.txt" 2>/dev/null \
        && { WARN_SEEN=1; break; }
done
SERVES=0
( unset DISPLAY WAYLAND_DISPLAY
  WINEDEBUG=-all ADB_SERVER_SOCKET="$FG_SOCK" timeout 60 wine "$ADB" version \
      > "$TMP/fg-client.txt" 2>&1 ) \
    && grep -q "Android Debug Bridge version" "$TMP/fg-client.txt" && SERVES=1
if [ "$WARN_SEEN" = "1" ] && [ "$SERVES" = "1" ]; then
    ok "a server with no display warns and keeps serving (0006)"
elif [ "$WARN_SEEN" = "1" ]; then
    bad "server warned but did not survive: $(tr '\n' '|' < "$TMP/fg-client.txt" | head -c 160)"
elif [ "$SERVES" = "1" ]; then
    # No warning, server fine: this wine built the window despite the unset
    # display vars, so 0006's path was not reached. Report it as uncovered
    # rather than claim a pass it did not earn.
    skip "wine created the power window with no display; 0006's path not reached"
else
    bad "foreground server neither warned nor served: \
$(tail -3 "$TMP/fg-nodisp.txt" | tr '\n' '|')"
fi
kill "$FG_PID" 2>/dev/null; wait "$FG_PID" 2>/dev/null

# (b) detached: the path a user hits, and the one that failed in CI.
if ( unset DISPLAY WAYLAND_DISPLAY
     WINEDEBUG=-all ADB_SERVER_SOCKET="tcp:localhost:$SERVER_PORT" timeout 120 \
         wine "$ADB" start-server > "$TMP/headless.txt" 2>&1 ) \
        && ! grep -qE "failed to start daemon|could not read ok" "$TMP/headless.txt"; then
    ok "a detached server spawns with no display"
else
    bad "detached server failed without a display: $(tr '\n' '|' < "$TMP/headless.txt" | head -c 200)"
fi
timeout 120 wine "$ADB" kill-server >/dev/null 2>&1
sleep 1
# Restart the ordinary server; every check below expects a warm one.
wadb start-server > "$TMP/srv2.txt"

echo "== 1. CLI surface =="
wadb version > "$TMP/version.txt"
if grep -q "Android Debug Bridge version" "$TMP/version.txt" \
        && grep -q "meowarch-adb" "$TMP/version.txt"; then
    ok "version: $(head -1 "$TMP/version.txt")"
else
    bad "version: $(tr '\n' '|' < "$TMP/version.txt")"
fi
# Installed as <windows path>: this only prints when GetExecutablePath works,
# which on Windows goes through libbase's utf8.cpp + GetModuleFileName — i.e.
# the reason base_static is built WITHOUT UNICODE.
if grep -qi "Installed as" "$TMP/version.txt"; then
    ok "GetExecutablePath works on Windows"
else
    bad "no 'Installed as' line: $(tr '\n' '|' < "$TMP/version.txt")"
fi

# Categorised help renders, and every documented command is still listed.
EXPECTED="devices connect disconnect pair forward reverse push pull install uninstall
shell emu logcat bugreport jdwp root unroot remount wait-for get-state get-serialno
get-devpath kill-server start-server tcpip reboot sideload mdns keygen
enable-verity disable-verity install-multiple install-multi-package reconnect attach detach usb"
wadb --lang=en help > "$TMP/help.txt"
MISSING=""
for c in $EXPECTED; do
    grep -qE "(^| )$c([ \\\[]|$)" "$TMP/help.txt" || MISSING="$MISSING $c"
done
if [ -z "$MISSING" ]; then ok "help lists every documented command"; else bad "help missing:$MISSING"; fi

wadb help --list-sections > "$TMP/sections.txt"
if grep -q networking "$TMP/sections.txt" && grep -q meowarch "$TMP/sections.txt"; then
    ok "help --list-sections (incl. our own section)"
else
    bad "help --list-sections: $(tr '\n' '|' < "$TMP/sections.txt")"
fi
# Our five flags must survive argv compaction on Windows too.
for f in --list-langs --no-color --color --no-progress; do
    if wadb $f version >/dev/null 2>&1; then ok "$f is accepted"; else bad "$f rejected by the parser"; fi
done
wadb --list-langs > "$TMP/langs.txt"
if grep -q zh_TW "$TMP/langs.txt" && grep -q 日本語 "$TMP/langs.txt"; then
    ok "--list-langs lists every language (UTF-8 output intact)"
else
    bad "--list-langs: $(tr '\n' '|' < "$TMP/langs.txt")"
fi
# mDNS is deliberately not built in on any platform; it must say so.
wadb mdns check > "$TMP/mdns.txt"
if grep -qi unavailable "$TMP/mdns.txt"; then
    ok "mdns check reports the backend is absent"
else
    bad "mdns check: $(tr '\n' '|' < "$TMP/mdns.txt")"
fi

echo "== 2. Did-you-want + localized errors =="
wadb --lang=en sheel > "$TMP/sheel.txt"
# NOTE: the prefix is `adb.exe:` here, not `adb:` — upstream builds it from
# Basename(GetExecutablePath()), so the suffix is part of it on Windows. That is
# upstream's own behaviour, not ours, and it is why this does not anchor to a
# bare "adb:".
if grep -q "Did you want 'shell'?" "$TMP/sheel.txt" \
        && grep -q "^adb.exe: unknown command sheel" "$TMP/sheel.txt"; then
    ok "unknown command suggests 'shell' (upstream line unchanged)"
else
    bad "unknown-command hint: $(tr '\n' '|' < "$TMP/sheel.txt")"
fi
# The help pointer must name the program WITHOUT its .exe suffix.
if grep -q "Run 'adb help' for usage" "$TMP/sheel.txt"; then
    ok "the help pointer drops .exe"
else
    bad "help pointer: $(grep -i help "$TMP/sheel.txt" | head -2)"
fi
wadb --lang=en --one-devic > "$TMP/opt.txt"
if grep -q "Did you want '--one-device'?" "$TMP/opt.txt"; then
    ok "unknown option suggests '--one-device'"
else
    bad "option hint: $(tr '\n' '|' < "$TMP/opt.txt")"
fi
wadb --lang=en xyzzy > "$TMP/xyzzy.txt"
# exactly one line: upstream's error, no wrong guess after it
if [ "$(grep -c '' "$TMP/xyzzy.txt")" -eq 1 ]; then
    ok "a foreign word gets no hint"
else
    bad "foreign word produced extra lines: $(tr '\n' '|' < "$TMP/xyzzy.txt")"
fi
wadb --lang=zh_CN sheel > "$TMP/sheel_zh.txt"
if grep -q "您是否想输入 'shell'" "$TMP/sheel_zh.txt"; then
    ok "hint localized in zh_CN"
else
    bad "zh_CN hint: $(tr '\n' '|' < "$TMP/sheel_zh.txt")"
fi
# A localized error, not a hint: transport.cpp builds this text and it reaches
# error_exit already formatted, so it exercises the pattern-matching path.
wadb --lang=ja -s no-such-device-xyz shell > "$TMP/ja.txt"
if grep -q 見つかりません "$TMP/ja.txt"; then
    ok "error text localized in ja"
else
    bad "ja error: $(tr '\n' '|' < "$TMP/ja.txt")"
fi
# Piped output must carry no ANSI escapes (fb_color's GetConsoleMode check, not
# _isatty — wine hands back a character device for a redirected pipe).
if grep -q $'\033' "$TMP/sheel.txt" "$TMP/sheel_zh.txt" "$TMP/ja.txt"; then
    bad "escape bytes leaked into piped output"
else
    ok "piped output has no colour escapes"
fi

echo "== 3. unit tests (native PE, run under wine) =="
if [ -f "$SUGGEST" ]; then
    # direct redirect, never a pipe: see wadb() for why
    timeout 300 wine "$SUGGEST" > "$TMP/suggest.txt" 2>&1
    if grep -q "0 failed" "$TMP/suggest.txt"; then
        ok "suggest scorer ($(grep -oE '[0-9]+ passed' "$TMP/suggest.txt" | head -1), 0 failed)"
    else
        bad "suggest scorer under wine"; grep FAIL "$TMP/suggest.txt" | head -4
    fi
else
    skip "test_suggest.exe not built"
fi

echo "== 4. completion generation (same generator as Linux) =="
wadb --shell-completion=bash > "$TMP/comp.bash"
wadb --shell-completion=zsh  > "$TMP/comp.zsh"
# fish gets an explicit NAME here (as the Linux suite does) so the check is
# about the script's content, and a separate case below covers the derived name.
wadb --shell-completion=fish madb > "$TMP/comp.fish"
if grep -q "complete -F _madb madb adb-meowarch" "$TMP/comp.bash"; then
    ok "bash script generated (registers both command names)"
else
    bad "bash completion: $(head -3 "$TMP/comp.bash" | tr '\n' '|')"
fi
if grep -q "#compdef madb adb-meowarch" "$TMP/comp.zsh"; then
    ok "zsh script generated"
else
    bad "zsh completion: $(head -3 "$TMP/comp.zsh" | tr '\n' '|')"
fi
# The two real fish bugs this suite exists to keep fixed: a list index past the
# end is an error in fish 4, and an empty transport token made every device
# query run `adb '' shell ...`.
# Single quotes are load-bearing: inside double quotes bash would expand $cmd,
# $i and $__madb_target to nothing and grep would match a near-empty pattern,
# making this check pass or fail for reasons unrelated to the script.
if grep -q 'complete -c madb' "$TMP/comp.fish" \
        && grep -qF 'test (count $cmd) -gt $i' "$TMP/comp.fish" \
        && grep -qF 'count $out' "$TMP/comp.fish"; then
    ok "fish script generated (both index/empty-transport guards present)"
else
    bad "fish completion missing a guard: $(grep -cF 'count $cmd' "$TMP/comp.fish")/$(grep -cF 'count $out' "$TMP/comp.fish")"
fi
# Without a NAME the generator derives the command from argv[0]. On Windows that
# is a full path with BACKSLASH separators and a ".exe" suffix, and splitting on
# '/' alone used to put the whole path into `complete -c` — a name no shell would
# ever type, so fish completion silently did nothing while the explicit-NAME case
# above kept working. This is the regression check for that.
wadb --shell-completion=fish > "$TMP/comp-fish-bare"
DERIVED="$(sed -n 's/^complete -c \([^ ]*\).*/\1/p' "$TMP/comp-fish-bare" | head -1)"
case "$DERIVED" in
    */*|*\\*|*.exe|*.EXE|"")
        bad "fish name derived from argv[0] is not a bare command: '$DERIVED'" ;;
    *) ok "fish name derived from argv[0]: '$DERIVED'" ;;
esac
wadb --shell-completion=csh > "$TMP/comp.csh" 2>&1
if [ "$WINE_RC" -eq 0 ]; then bad "an unknown shell was accepted (rc=0)"; fi
if grep -q "unknown shell 'csh'" "$TMP/comp.csh"; then
    ok "unknown shell is rejected"
else
    bad "unknown shell message: $(tr '\n' '|' < "$TMP/comp.csh")"
fi

echo "== 5. live device over TCP (ws2_32 + sysdeps_win32 + sync) =="
if [ ! -x "$ADBD" ]; then
    skip "no adbd at $ADBD (build adbd-standalone first)"
elif ! command -v python3 >/dev/null 2>&1; then
    skip "python3 unavailable"
else
    # The NATIVE Linux adbd, listening on a host port that wine reaches through
    # 127.0.0.1. No mock: this is the real sync protocol end to end.
    ADBD_NO_AUTH=1 ADBD_PORT="$PORT" "$ADBD" >"$TMP/adbd.log" 2>&1 &
    ADBD_PID=$!
    sleep 2

    # Assert adbd is actually listening before any check that depends on it. An
    # adbd that cannot even load — the artifact links libc++.so.1, which a
    # bare container image does not have — exits immediately, and every check
    # below then fails with "cannot connect to 127.0.0.1:$PORT: Connection
    # refused". Eleven identical failures that read like a bug in the win32
    # socket layer and are really a missing runtime for the LINUX half of the
    # pair. One CI run looked exactly like that; adbd's own message names it.
    if ! grep -q "listening on" "$TMP/adbd.log"; then
        echo "  FATAL the native adbd did not start — nothing below can pass:"
        head -4 "$TMP/adbd.log"
        echo "  (a 'libc++.so.1: cannot open shared object file' line means this"
        echo "   image needs the libc++/libc++abi packages, not a product fix)"
        echo "== summary: $PASS passed, $((FAIL+1)) failed, $SKIP skipped =="
        exit 1
    fi

    wadb connect "127.0.0.1:$PORT" > "$TMP/connect.txt"
    if grep -q connected "$TMP/connect.txt"; then
        ok "connect: $(head -1 "$TMP/connect.txt")"
    else
        bad "connect: $(tr '\n' '|' < "$TMP/connect.txt")"
    fi
    sleep 1
    wadb devices > "$TMP/devices.txt"
    if grep -q "127.0.0.1:$PORT.*device" "$TMP/devices.txt"; then
        ok "devices lists the TCP device"
    else
        bad "devices: $(tr '\n' '|' < "$TMP/devices.txt")"
    fi
    SER="127.0.0.1:$PORT"

    # shell v2: output AND exit status must survive the Windows socket layer.
    wadb -s "$SER" shell 'echo shell-ok; exit 7' > "$TMP/shell.txt"
    RC=$WINE_RC   # not $?: see wadb()
    # Windows console stdio is CRLF, so the exact match has to strip \r first.
    if tr -d '\r' < "$TMP/shell.txt" | grep -qx "shell-ok" && [ "$RC" = "7" ]; then
        ok "shell: output + exit code 7"
    else
        bad "shell: '$(tr '\n' '|' < "$TMP/shell.txt")' rc=$RC"
    fi

    # sync push/pull, byte-exact, binary content (not text, so a CRLF
    # translation anywhere in the Windows path would show up immediately).
    head -c 262144 /dev/urandom > "$TMP/in.bin"
    SUM_IN="$(sha256sum "$TMP/in.bin" | cut -d' ' -f1)"
    wadb -s "$SER" push "$TMP/in.bin" "/tmp/adbwin-in.bin" > "$TMP/push.txt"
    if grep -q "pushed" "$TMP/push.txt"; then
        ok "push reported success: $(tr -d '\r' < "$TMP/push.txt" | tail -1)"
    else
        bad "push: $(tr '\n' '|' < "$TMP/push.txt")"
    fi
    wadb -s "$SER" pull "/tmp/adbwin-in.bin" "$TMP/out.bin" > "$TMP/pull.txt"
    if grep -q "pulled" "$TMP/pull.txt" && [ -f "$TMP/out.bin" ]; then
        SUM_OUT="$(sha256sum "$TMP/out.bin" | cut -d' ' -f1)"
        if [ "$SUM_IN" = "$SUM_OUT" ]; then
            ok "push/pull round trip is byte-exact (sha256 matches)"
        else
            bad "sha256 differs: in=$SUM_IN out=$SUM_OUT"
        fi
    else
        bad "pull: $(tr '\n' '|' < "$TMP/pull.txt")"
    fi

    # A directory push: many small files, which is the path where the batch
    # progress bar and the per-file ledger meet.
    mkdir -p "$TMP/dir/sub"
    for i in 1 2 3; do head -c 4096 /dev/urandom > "$TMP/dir/f$i.bin"; done
    head -c 8192 /dev/urandom > "$TMP/dir/sub/g.bin"
    # adbd names a pushed directory after its source basename, so this lands in
    # /tmp/$DIRNAME. The name carries the mktemp suffix: a directory left behind
    # by a crashed earlier run must not be able to satisfy the check below.
    DIRNAME="adbwin-$(basename "$TMP")"
    rm -rf "$TMP/$DIRNAME"; mv "$TMP/dir" "$TMP/$DIRNAME"
    wadb -s "$SER" push "$TMP/$DIRNAME" /tmp/ > "$TMP/pushdir.txt"
    if grep -q "pushed" "$TMP/pushdir.txt"; then
        ok "directory push: $(tr -d '\r' < "$TMP/pushdir.txt" | tail -1)"
    else
        bad "directory push: $(tr '\n' '|' < "$TMP/pushdir.txt")"
    fi
    # every file must actually be there, at the right size
    # pushed into /tmp/ (the Linux suite does the same): glibc adbd runs as the
    # invoking user, so Android paths like /data/local/tmp do not exist here.
    wadb -s "$SER" shell "ls -l /tmp/$DIRNAME" > "$TMP/ls.txt"
    if tr -d '\r' < "$TMP/ls.txt" | grep -q "f1.bin" && tr -d '\r' < "$TMP/ls.txt" | grep -q "sub"; then
        ok "directory contents landed on the device"
    else
        bad "ls after directory push: $(tr '\n' '|' < "$TMP/ls.txt")"
    fi

    # Compression negotiation: the win32 build compiles lz4/zstd/brotli from
    # AOSP's own external/, so a forced algorithm must still transfer exactly.
    for Z in lz4 zstd brotli none; do
        # -z goes AFTER push: it is a sync argument (parse_sync_args), not a
        # global option. Before the subcommand adb's parser rejects it as
        # "unknown command -z".
        wadb -s "$SER" push -z "$Z" "$TMP/in.bin" "/tmp/adbwin-z-$Z" > "$TMP/pz-$Z.txt"
        wadb -s "$SER" pull "/tmp/adbwin-z-$Z" "$TMP/oz-$Z.bin" > "$TMP/qz-$Z.txt"
        if grep -q pushed "$TMP/pz-$Z.txt" && [ -f "$TMP/oz-$Z.bin" ] \
                && [ "$(sha256sum "$TMP/oz-$Z.bin" | cut -d' ' -f1)" = "$SUM_IN" ]; then
            ok "push/pull with -z $Z is byte-exact"
        else
            bad "-z $Z: $(tr '\n' '|' < "$TMP/pz-$Z.txt")$(tr '\n' '|' < "$TMP/qz-$Z.txt")"
        fi
    done

    # Cleanup on the device so a re-run starts from a clean slate.
    wadb -s "$SER" shell "rm -rf /tmp/adbwin-in.bin /tmp/$DIRNAME /tmp/adbwin-z-*" >/dev/null 2>&1
    kill "$ADBD_PID" 2>/dev/null; wait "$ADBD_PID" 2>/dev/null; ADBD_PID=""
fi

echo
echo "== summary: $PASS passed, $FAIL failed, $SKIP skipped =="
[ "$FAIL" -eq 0 ]
