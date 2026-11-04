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
# Resolve both binaries to ABSOLUTE paths. The completion drives cd into a
# scratch directory and run the command as COMP_WORDS[0], so a relative path
# stops resolving the moment the working directory changes — which is exactly
# how CI (which passes "build-native/adb") saw every device-side completion
# check return nothing while the same suite passed by hand with the default,
# absolute path.
ADB="$(cd "$(dirname "$ADB")" && pwd)/$(basename "$ADB")"
ADBD="$(cd "$(dirname "$ADBD")" 2>/dev/null && pwd)/$(basename "$ADBD")"

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

    # Multi-file push: ONE bar for the batch (that is the only total the user
    # can make sense of), with the label following the file in flight.
    # Regression: the bar used to be fed the ledger's cumulative byte count
    # while armed with a single file's size, so from the second file on it sat
    # pinned at 100% and the numbers ran past the total (6.2 MiB/5.7 MiB).
    # Push a DIRECTORY, not two named files: a directory transfer calls
    # NewTransfer() once for the whole batch, so the ledger accumulates across
    # files. Two separately-named arguments get their own transfer each and
    # would hide the bug entirely.
    mkdir -p "$TMP/multi"
    head -c 6000000 /dev/urandom > "$TMP/multi/a.img"
    head -c 2000000 /dev/urandom > "$TMP/multi/b.img"
    TERM=xterm-256color script -q -c \
        "$ADB -s $SER push $TMP/multi $TMP/multi-out" "$TMP/multi.txt" >/dev/null 2>&1
    FILES_BARRED="$(tr '\r' '\n' < "$TMP/multi.txt" | grep -aE '\[[#>-]+\]' | grep -aoE '[ab]\.img' | sort -u | tr -d '\n')"
    if [ "$FILES_BARRED" = "a.imgb.img" ]; then
        ok "multi-file push names every file on the batch bar"
    else
        bad "multi-file push barred: '$FILES_BARRED' (want 'a.imgb.img')"
    fi
    # No frame may report more bytes than the total it is counting against —
    # for a batch that is the batch total. With the cumulative-counter bug a
    # directory push showed "7.6 MiB/5.7 MiB" and pinned at 100%, which is
    # deterministic and does not depend on how many frames a fast loopback
    # transfer happens to emit.
    OVER="$(tr '\r' '\n' < "$TMP/multi.txt" \
        | sed 's/\x1b\[[0-9;]*m//g' \
        | grep -aE '\[[#>-]+\]' \
        | grep -aoE '[0-9.]+ (B|KiB|MiB|GiB)/[0-9.]+ (B|KiB|MiB|GiB)' \
        | awk -F'[ /]' '{
            # With the default FS this line would be "5.8 MiB/5.7 MiB" ->
            # $2="MiB/5.7", so m[u1] stays empty and every comparison passes;
            # that is what let the bug through the first version of this check.
            m["B"]=1; m["KiB"]=1024; m["MiB"]=1048576; m["GiB"]=1073741824;
            # 8% tolerance: upstream legitimately overshoots a little when a
            # file grows between stat and read, but the cumulative-counter bug
            # overshoots by the whole rest of the batch (33%+ here).
            if ($1*m[$2] > $3*m[$4] * 1.08) print $0;
          }')"
    if [ -z "$OVER" ]; then
        ok "no bar frame exceeds its file's size"
    else
        bad "bar over-reported bytes: $(printf '%s' "$OVER" | head -2 | tr '\n' ' ')"
    fi

    # ------------------------------------------------------------------
    # 2a. the bar beyond push/pull: exec-out streams a size nobody knows
    # ------------------------------------------------------------------
    # backup/restore/exec-out have no total, so the bar renders bytes and speed
    # only. Two rules that are easy to break:
    #   * the payload must stay byte-exact when stdout is a pipe or a file, and
    #     stderr must carry no escape bytes then either;
    #   * when the payload goes to a TERMINAL (exec-out with no redirect), the
    #     bar must be off — it would interleave with the stream it is counting.
    "$ADB" -s "$SER" exec-out 'dd if=/dev/zero bs=1M count=6 2>/dev/null' > "$TMP/eo.bin" 2>"$TMP/eo.err"
    if [ "$(stat -c%s "$TMP/eo.bin")" = "6291456" ] && [ ! -s "$TMP/eo.err" ]; then
        ok "exec-out is byte-exact with no bar on a pipe"
    else
        bad "exec-out piped: $(stat -c%s "$TMP/eo.bin") bytes, stderr $(stat -c%s "$TMP/eo.err") bytes"
    fi
    # On a pty with stdout redirected to a file, the bar renders on stderr.
    TERM=xterm-256color script -q -c \
        "$ADB -s $SER exec-out 'dd if=/dev/zero bs=1M count=6 2>/dev/null' > $TMP/eo-pty.bin" \
        "$TMP/eo-pty.txt" >/dev/null 2>&1
    EO_FRAMES="$(tr '\r' '\n' < "$TMP/eo-pty.txt" | grep -acE 'MiB|KiB')"
    if [ "${EO_FRAMES:-0}" -ge 2 ]; then
        ok "exec-out streams an unknown-total bar ($EO_FRAMES frames on a pty)"
    else
        bad "exec-out bar: $EO_FRAMES frames"
    fi
    # No percentage may appear: a fraction of an unknown total is meaningless,
    # and printing one is how a bar comes to lie.
    if tr '\r' '\n' < "$TMP/eo-pty.txt" | grep -aE '\[[#>-]+\]' | grep -qaE '[0-9]+%'; then
        bad "unknown-total bar printed a percentage"
    else
        ok "unknown-total bar shows no percentage"
    fi
    # Payload went to a file untouched, and every frame stayed inside 80 columns
    # (script's pty has a 0x0 window, so the bar falls back to its 80 default).
    if [ "$(stat -c%s "$TMP/eo-pty.bin")" = "6291456" ]; then
        ok "exec-out payload intact with the bar on a pty"
    else
        bad "exec-out payload on a pty: $(stat -c%s "$TMP/eo-pty.bin") bytes"
    fi
    # Measure only real bar frames (a line carrying a "<num> <unit>" field):
    # `script` writes its own header and footer into the same pty, and its
    # header quotes the whole command line, which is easily over 80 columns.
    WIDE="$(tr '\r' '\n' < "$TMP/eo-pty.txt" \
        | sed 's/\x1b\[[0-9;]*[A-Za-z]//g' \
        | grep -aE '[0-9]+(\.[0-9])? (B|KiB|MiB|GiB|TiB)' \
        | awk 'length($0)>80')"
    if [ -z "$WIDE" ]; then ok "no exec-out bar frame exceeds the terminal width"
    else bad "exec-out frame wider than the terminal: $(printf '%s' "$WIDE" | head -1 | cut -c1-60)"; fi
    # With the payload going to the SAME pty, the bar must stay off.
    TERM=xterm-256color script -q -c "$ADB -s $SER exec-out 'dd if=/dev/zero bs=1K count=8 2>/dev/null'" \
        "$TMP/eo-same.txt" >/dev/null 2>&1
    SAME="$(tr '\r' '\n' < "$TMP/eo-same.txt" | grep -acE '[0-9.]+ [KM]iB/s')"
    if [ "${SAME:-0}" -eq 0 ]; then
        ok "exec-out prints no bar when its payload goes to the terminal"
    else
        bad "exec-out barred over its own payload: $SAME frames"
    fi

    # ------------------------------------------------------------------
    # 2b. pipelined recursive pull (listing + small-file batching)
    # ------------------------------------------------------------------
    # `pull: building file list...` used to cost one round trip per directory,
    # and one more per file when the bytes moved, so a tree of many small
    # directories spent minutes listing before transferring anything. Both
    # phases now send a batch of requests in one write and read the answers back
    # in order. Two things must hold: the result is byte-identical to the
    # sequential walk, and the batching actually happened.
    rm -rf "$TMP/tree" "$TMP/tree-out" "$TMP/tree-out-seq"
    python3 - "$TMP/tree" <<'PYEOF'
import os, sys
base = sys.argv[1]
# 60 directories x 3 files, plus symlinks (each needs a STAT to classify) and
# one file large enough to be pulled outside any batch.
for i in range(60):
    d = os.path.join(base, "dir%02d" % (i // 10), "sub%02d" % (i % 10))
    os.makedirs(d, exist_ok=True)
    for j in range(3):
        with open(os.path.join(d, "f%d.bin" % j), "wb") as f:
            f.write(bytes((i + j) % 251 for _ in range(2048)))
    if i % 4 == 0:
        os.symlink("f0.bin", os.path.join(d, "link.bin"))
os.makedirs(os.path.join(base, "big"), exist_ok=True)
with open(os.path.join(base, "big", "large.bin"), "wb") as f:
    f.write(os.urandom(9 * 1024 * 1024))
PYEOF
    "$ADB" -s "$SER" push "$TMP/tree" /tmp/ >/dev/null 2>&1

    ADB_SYNC_STATS=1 "$ADB" -s "$SER" --no-progress pull /tmp/tree "$TMP/tree-out" >"$TMP/pull.txt" 2>&1
    if diff -r "$TMP/tree" "$TMP/tree-out" >/dev/null 2>&1; then
        ok "batched recursive pull is byte-identical (dirs, symlinks, one large file)"
    else
        bad "batched pull differs: $(diff -rq "$TMP/tree" "$TMP/tree-out" 2>&1 | head -3 | tr '\n' ' ')"
    fi
    # The large file must NOT be inside a batch (a big file queued behind a batch
    # of small ones is the failure mode the size cap prevents), while the small
    # ones must be: 180 files at a 64-file window is 3 groups, so far fewer
    # groups than files means batching happened.
    BATCHES="$(sed -n 's/^STATS recv: \([0-9]*\) batched.*/\1/p' "$TMP/pull.txt")"
    if [ -n "$BATCHES" ] && [ "$BATCHES" -ge 1 ] && [ "$BATCHES" -lt 30 ]; then
        ok "small files were batched ($BATCHES RECV groups for 180 files)"
    else
        bad "small-file batching: '$BATCHES' groups (want 1..29)"
    fi
    # Anchor the fields explicitly: `.*\([0-9]*\)` is greedy, so `.*` eats the
    # digits and the capture group matches empty.
    # Every directory must still be listed (the batch must not silently drop
    # one), and the listings must have gone out in far fewer batches than
    # directories — that is the pipelining. 61 directories, window 64: one or
    # two batches, never 61.
    LS="$(sed -n 's/^STATS listing: [0-9]* entries, [0-9.]* s, \([0-9]*\) LIST requests in \([0-9]*\) batches.*/\1 \2/p' "$TMP/pull.txt")"
    set -- $LS
    if [ "${1:-0}" -ge 61 ] && [ "${2:-999}" -lt 20 ]; then
        ok "all directories listed in few batches ($1 LIST requests, $2 batches)"
    else
        bad "listing stats: '$LS' (want >= 61 requests in < 20 batches)"
    fi
    # Window=1 reproduces the old sequential behaviour; the tree must come out
    # the same either way, which is the real correctness argument for the batch.
    ADB_SYNC_LIST_WINDOW=1 ADB_SYNC_RECV_WINDOW=1 "$ADB" -s "$SER" --no-progress \
        pull /tmp/tree "$TMP/tree-out-seq" >/dev/null 2>&1
    if diff -r "$TMP/tree-out" "$TMP/tree-out-seq" >/dev/null 2>&1; then
        ok "batched and sequential pulls agree"
    else
        bad "batched and sequential pulls differ"
    fi
    # Long path names are the case where the COUNT-based window is not enough:
    # 64 requests of ~220 bytes each are ~14 KiB, but a window of 256 would be
    # over the byte budget, so the request write has to be chunked or it will
    # one day exceed the peer's receive buffer and deadlock. Nothing here can
    # prove the absence of a deadlock, but the answers must still line up with
    # the requests (a mismatch shows up as a corrupt or truncated tree).
    rm -rf "$TMP/long" "$TMP/long-out"
    mkdir -p "$TMP/long"
    python3 - "$TMP/long" <<'PYEOF'
import os, sys
base = sys.argv[1]
for i in range(120):
    name = ("f%03d_" % i) + "x" * 190 + ".bin"
    with open(os.path.join(base, name), "wb") as f:
        f.write(bytes([i % 251]) * 1500)
PYEOF
    "$ADB" -s "$SER" push "$TMP/long" /tmp/ >/dev/null 2>&1
    "$ADB" -s "$SER" --no-progress pull /tmp/long "$TMP/long-out" >/dev/null 2>&1
    if diff -r "$TMP/long" "$TMP/long-out" >/dev/null 2>&1; then
        ok "long remote paths pull correctly (byte-chunked request batches)"
    else
        bad "long-path pull differs: $(diff -rq "$TMP/long" "$TMP/long-out" 2>&1 | head -2 | tr '\n' ' ')"
    fi
    # A batch is normally flushed by the COUNT window, but the byte budget
    # (16 MiB) is what actually bounds in-flight data. Sixty 1 MiB files, three
    # at exactly the 4 MiB per-file cap (the cap is "> 4 MiB", so these batch)
    # and one just over it (must go alone) exercise the byte-flush path, the
    # boundary of the per-file cap, and the mixing of batched and solo files in
    # one directory.
    rm -rf "$TMP/bt" "$TMP/bt-out"
    mkdir -p "$TMP/bt"
    python3 - "$TMP/bt" <<'PYEOF'
import os, sys
base = sys.argv[1]
for i in range(60):
    with open(os.path.join(base, "m%03d.bin" % i), "wb") as f:
        f.write(bytes([i % 251]) * (1024 * 1024))
for i in range(3):
    with open(os.path.join(base, "cap%d.bin" % i), "wb") as f:
        f.write(bytes([(i * 37) % 251]) * (4 * 1024 * 1024))
with open(os.path.join(base, "over.bin"), "wb") as f:
    f.write(bytes([7]) * (4 * 1024 * 1024 + 4096))
PYEOF
    "$ADB" -s "$SER" push "$TMP/bt" /tmp/ >/dev/null 2>&1
    ADB_SYNC_STATS=1 "$ADB" -s "$SER" --no-progress pull /tmp/bt "$TMP/bt-out" >"$TMP/bt.txt" 2>&1
    if diff -r "$TMP/bt" "$TMP/bt-out" >/dev/null 2>&1; then
        ok "byte-budget flush path pulls correctly (60 MiB, cap and over-cap files)"
    else
        bad "byte-budget pull differs: $(diff -rq "$TMP/bt" "$TMP/bt-out" 2>&1 | head -2 | tr '\n' ' ')"
    fi
    # 64 files, ~60 MiB: the count window (512) never binds, so the number of
    # groups must be well above 1 — that is the byte budget doing the splitting.
    BT_BATCHES="$(sed -n 's/^STATS recv: \([0-9]*\) batched.*/\1/p' "$TMP/bt.txt")"
    if [ -n "$BT_BATCHES" ] && [ "$BT_BATCHES" -ge 3 ]; then
        ok "the byte budget split the batch ($BT_BATCHES groups for 64 files)"
    else
        bad "byte-budget splitting: '$BT_BATCHES' groups (want >= 3)"
    fi
    # The bar over a batched directory pull. The tree above contains symlinks,
    # whose listed size is the target path's length while the pull fetches the
    # target's bytes, so the batch counter legitimately runs a few percent past
    # its total — upstream's own code documents this and prints "?" past 100%.
    # Our bar clamps the percentage at 100 and shows the real counter, so what
    # must hold is narrower and stricter: no frame wider than the terminal, and
    # no percentage above 100.
    rm -rf "$TMP/bar-out"
    TERM=xterm-256color script -q -c "$ADB -s $SER pull /tmp/tree $TMP/bar-out" \
        "$TMP/bar.txt" >/dev/null 2>&1
    BAR_BAD="$(tr '\r' '\n' < "$TMP/bar.txt" \
        | sed 's/\x1b\[[0-9;]*[A-Za-z]//g' \
        | grep -aE '\[[#>-]+\]' \
        | awk -v cols="$(tput cols 2>/dev/null || echo 80)" '
            { line=$0; w=length(line);
              if (match(line, /[0-9]+%/)) {
                pct=substr(line, RSTART, RLENGTH-1)+0;
                if (pct > 100) print "pct=" pct ": " line;
              } }')"
    if [ -z "$BAR_BAD" ]; then
        ok "pull bar never reports over 100% despite symlink overshoot"
    else
        bad "pull bar over 100%: $(printf '%s' "$BAR_BAD" | head -1 | cut -c1-70)"
    fi
    BAR_COLS="$(tr '\r' '\n' < "$TMP/bar.txt" \
        | sed 's/\x1b\[[0-9;]*[A-Za-z]//g' \
        | grep -aE '\[[#>-]+\]' \
        | awk '{ print length($0) }' | sort -un | tail -1)"
    # script's pty is 0x0 unless the parent has a real size, so the bar falls
    # back to its 80-column default; the invariant is that no frame exceeds it.
    if [ "${BAR_COLS:-0}" -le 80 ]; then
        ok "no pull bar frame exceeds the terminal width (max ${BAR_COLS:-0} cols)"
    else
        bad "pull bar frame is $BAR_COLS columns wide"
    fi
    "$ADB" -s "$SER" shell rm -rf /tmp/tree /tmp/long /tmp/bt >/dev/null 2>&1

    # ------------------------------------------------------------------
    # 3. shell completions, generated by the binary itself
    # ------------------------------------------------------------------
    for SH in bash zsh fish; do
        "$ADB" --shell-completion=$SH madb > "$TMP/comp-$SH" 2>&1
        if [ $? -eq 0 ] && [ -s "$TMP/comp-$SH" ]; then
            ok "completion script generated for $SH"
        else
            bad "completion script for $SH is empty or failed"
        fi
    done
    OUT="$("$ADB" --shell-completion=tclsh 2>&1)"; RC=$?
    if [ $RC -ne 0 ] && printf '%s' "$OUT" | grep -q "unknown shell"; then
        ok "unknown completion shell is rejected"
    else
        bad "unknown completion shell: rc=$RC out=$OUT"
    fi
    bash -n "$TMP/comp-bash" && ok "bash script parses" || bad "bash script has a syntax error"
    if command -v fish >/dev/null 2>&1; then
        fish -c "source $TMP/comp-fish" >/dev/null 2>&1 && ok "fish script parses" || bad "fish script has a syntax error"
    else
        skip "fish not installed (parse check skipped)"
    fi
    if command -v zsh >/dev/null 2>&1; then
        zsh -n "$TMP/comp-zsh" && ok "zsh script parses" || bad "zsh script has a syntax error"
    else
        skip "zsh not installed (parse check skipped)"
    fi
    # The generated vocabulary must match `adb help`: a command that is
    # documented but not completable (or the reverse) is exactly the drift that
    # generating from the binary is supposed to prevent.
    # Strip the assignment's shell syntax first, then split: `tr ' ' '\n'`
    # before the prefix removal would leave "local" and "cmds=devices" as two
    # tokens and match nothing.
    GEN_CMDS="$(sed -n "s/^    local cmds='\(.*\)'$/\1/p" "$TMP/comp-bash")"
    for C in push pull shell sync install uninstall sideload reboot devices connect \
             disconnect forward reverse mdns help logcat bugreport jdwp root remount \
             get-state get-serialno start-server kill-server tcpip keygen attach detach; do
        case " $GEN_CMDS " in *" $C "*) ok "completion offers '$C'" ;; *) bad "completion missing '$C'" ;; esac
    done

    # bash functional drive with a real device attached, so the device-side
    # queries (remote paths, device executables, package names) run for real.
    rm -rf "$TMP/cd" && mkdir -p "$TMP/cd"
    touch "$TMP/cd/app.apk" "$TMP/cd/ota.zip" "$TMP/cd/notes.txt"
    cat > "$TMP/drive.sh" <<DRIVE
set -u
source "$TMP/comp-bash"
PASS=0; FAIL=0
try() {
  local desc="\$1" want="\$2"; shift 2
  COMP_WORDS=("$ADB" "\${@:2}"); COMP_CWORD=\$(( \${#COMP_WORDS[@]} - 1 )); COMPREPLY=()
  _madb
  case "\${COMPREPLY[*]}" in *"\$want"*) PASS=\$((PASS+1));;
    *) echo "  FAIL \$desc (want '\$want', got: \${COMPREPLY[*]})"; FAIL=\$((FAIL+1));; esac
}
notry() {
  local desc="\$1" bad="\$2"; shift 2
  COMP_WORDS=("$ADB" "\${@:2}"); COMP_CWORD=\$(( \${#COMP_WORDS[@]} - 1 )); COMPREPLY=()
  _madb
  case "\${COMPREPLY[*]}" in *"\$bad"*) echo "  FAIL \$desc (unexpected '\$bad')"; FAIL=\$((FAIL+1));;
    *) PASS=\$((PASS+1));; esac
}
try "commands"        "push"        madb ""
try "options"         "--no-color"  madb ""
try "langs"           "zh_TW"       madb --lang ""
try "compression"     "brotli"      madb push -z ""
try "reboot target"   "bootloader"  madb reboot ""
try "sync target"     "system"      madb sync ""
try "mdns sub"        "services"    madb mdns ""
try "help sections"   "networking"  madb help ""
try "forward sub"     "remove-all"  madb forward ""
try "serials"         "$SER"        madb -s ""
cd "$TMP/cd" || exit 1
try "install -> apk"      "app.apk"   madb install ""
notry "install hides non-apk" "notes.txt" madb install ""
try "sideload -> zip"     "ota.zip"   madb sideload ""
try "push LOCAL"          "app.apk"   madb push ""
try "pull REMOTE (device)" "/"        madb pull "/"
try "shell -> device cmds" "ls"       madb shell ""
try "shell pm sub"        "packages"  madb shell pm list ""
echo "drive: \$PASS passed, \$FAIL failed"
[ "\$FAIL" -eq 0 ]
DRIVE
    if ANDROID_SERIAL="$SER" bash "$TMP/drive.sh" > "$TMP/drive.out" 2>&1; then
        ok "bash functional drive ($(tail -1 "$TMP/drive.out"))"
    else
        bad "bash functional drive"; sed 's/^/      /' "$TMP/drive.out" | tail -12
    fi

    # zsh functional drive. zsh has no `complete -C`, so scripts/
    # test-completions.zsh loads the script through autoload and calls the
    # function with a capturing compadd stub; the serial lets the device-side
    # checks run for real.
    if command -v zsh >/dev/null 2>&1; then
        # The zsh drive calls the completion function with words[1] = "madb",
        # so the device-side helpers run `madb` from PATH exactly as a real
        # shell would. Put a symlink there, the same way the fish drive does;
        # without it every device query silently returns nothing (a completion
        # that found no device is indistinguishable from one that found no
        # candidates).
        mkdir -p "$TMP/zshbin"
        ln -sf "$ADB" "$TMP/zshbin/madb"
        "$ADB" --shell-completion=zsh madb > "$TMP/comp-zsh-adbm"
        if PATH="$TMP/zshbin:$PATH" zsh -f "$HERE/scripts/test-completions.zsh" \
                "$TMP/comp-zsh-adbm" "$SER" "$ADB" > "$TMP/zsh.out" 2>&1; then
            ok "zsh functional drive ($(grep -oE '[0-9]+ passed' "$TMP/zsh.out" | head -1))"
        else
            bad "zsh functional drive"; sed 's/^/      /' "$TMP/zsh.out" | tail -8
        fi
    else
        skip "zsh not installed (functional drive skipped)"
    fi

    # fish drive via `complete -C`. fish resolves the command by name, so the
    # binary has to be on PATH under the name the script completes for.
    #
    # That name must NOT be `madb`: `complete -C 'madb ...'` makes fish
    # autoload every completion registered for `madb`, including any
    # /usr/share/fish/vendor_completions.d/madb.fish installed on the machine,
    # whose functions have the same names and therefore OVERWRITE the ones the
    # freshly generated script just defined. The drive would then be testing
    # the installed copy, not the build under test — which is exactly how two
    # real bugs in the generated fish script (an out-of-range list index in
    # fish 4, and an empty transport token that made every device query run
    # `adb '' shell ...`) stayed invisible here.
    FISH_CMD="adbm-test-$$"
    if command -v fish >/dev/null 2>&1; then
        mkdir -p "$TMP/fishbin"
        ln -sf "$ADB" "$TMP/fishbin/$FISH_CMD"
        "$ADB" --shell-completion=fish "$FISH_CMD" > "$TMP/comp-fish-adbm"
        grep -q "complete -c $FISH_CMD" "$TMP/comp-fish-adbm" \
            || bad "fish script was not generated for $FISH_CMD"
        FPASS=0; FFAIL=0
        fish_try() {
            local desc="$1" want="$2" tail_="$3" nlines
            # Capture everything, stderr included: an empty candidate set and a
            # helper that errored look the same through grep. Then grep the
            # FILE, not a pipe — `printf ... | grep -q` under `set -o pipefail`
            # fails whenever grep exits early on a big list (printf takes
            # SIGPIPE), which is how a 6129-candidate result read as a failure.
            PATH="$TMP/fishbin:$PATH" ANDROID_SERIAL="$SER" \
                fish -c "source $TMP/comp-fish-adbm; complete -C '$FISH_CMD $tail_'" \
                > "$TMP/fish.out" 2>&1
            nlines=$(wc -l < "$TMP/fish.out")
            if grep -q -- "$want" "$TMP/fish.out"; then
                FPASS=$((FPASS+1))
            else
                echo "      FAIL fish: $desc (want '$want', got $nlines lines)"
                head -4 "$TMP/fish.out" | sed 's/^/        | /'
                FFAIL=$((FFAIL+1))
            fi
        }
        # `-s` as the LAST token used to abort the fish helper ("invalid index
        # value" in fish 4 for an index past the end of a list), and an empty
        # target used to become `madb '' shell ...` ("unknown command"), which
        # silently disabled every device-side candidate. Both are asserted
        # here: an error line fails the check, and the logs are captured so a
        # regression surfaces as a failure rather than as an empty list.
        fish_err() {
            local desc="$1" line="$2"
            PATH="$TMP/fishbin:$PATH" ANDROID_SERIAL="$SER" \
                fish -c "source $TMP/comp-fish-adbm; complete -C '$line'" \
                > "$TMP/fish.out" 2> "$TMP/fish.err"
            if [ -s "$TMP/fish.err" ]; then
                echo "      FAIL fish: $desc"; head -3 "$TMP/fish.err" | sed 's/^/        | /'
                FFAIL=$((FFAIL+1))
            else
                FPASS=$((FPASS+1))
            fi
        }
        fish_try "commands"        "push"        ""
        fish_err "no error for '-s' as the last token" "$FISH_CMD -s "
        fish_err "no error for a bare 'pull /'"        "$FISH_CMD pull /"
        fish_err "no error for 'shell '"               "$FISH_CMD shell "
        fish_err "no error for '-s SER pull /'"        "$FISH_CMD -s $SER pull /"
        fish_try "serials for -s"  "$SER"           "-s "
        fish_try "pull path WITH -s" "/"            "-s $SER pull /"
        fish_try "options"         "--no-color"  ""
        fish_try "langs"           "zh_TW"       "--lang "
        fish_try "reboot target"   "bootloader"  "reboot "
        fish_try "sync target"     "system"      "sync "
        fish_try "mdns sub"        "services"    "mdns "
        fish_try "help sections"   "networking"  "help "
        # Device-side: the device's own executables. Needs the daemon under
        # test, so it is only meaningful when CONNECTED is set (we are inside
        # that block).
        fish_try "device cmds"     "ls"          "shell "
        # Remote paths for pull: `ls -dp` output from the device.
        fish_try "remote paths"    "/"           "pull /"
        if [ $FFAIL -eq 0 ]; then ok "fish functional drive ($FPASS checks)"; else bad "fish functional drive"; fi
    else
        skip "fish not installed (functional drive skipped)"
    fi

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
