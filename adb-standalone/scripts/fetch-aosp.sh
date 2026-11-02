#!/bin/bash
# Fetch the partial AOSP checkout adb-standalone compiles against.
# ---------------------------------------------------------------------------
# Creates ../aosp-adbd (shared with adbd-standalone and better_fastboot) with
# the components the host adb client needs. Components already present are
# left untouched, so the three projects can share one tree.
#
# Repos fetched here (see README "Upstream" for the pinned SHAs):
#   packages/modules/adb     the client itself + crypto/tls/pairing
#   system/core              libbase/libcutils socket bits, libcrypto_utils,
#                            diagnose_usb, libziparchive deps
#   system/libbase, system/logging, system/libziparchive
#   external/boringssl       TLS (built from source; ASM off)
#   external/protobuf        protoc + full runtime (AdbServerStatus/AppProcesses
#                            are printed with TextFormat)
#   external/fmtlib          built into libbase
#   external/googletest      gtest_prod.h header only (libziparchive)
#   external/libusb          headers only: the client includes
#                            <libusb/libusb.h> by that path but links the
#                            *system* libusb-1.0 (which has every symbol adb
#                            uses; AOSP's fork only adds Android-only ones)
#   external/mdnsresponder   present in the tree but NOT built (its client stub
#                            needs the mdnsd daemon over a Unix socket)
#
# Googlesource may need a proxy; export https_proxy/http_proxy first if direct
# git fails.
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
AOSP="${AOSP_ROOT_DIR:-$HERE/../aosp-adbd}"
mkdir -p "$AOSP"

fetch() {  # fetch <path-in-tree> <url> <pinned-sha>
    local path="$1" url="$2" sha="$3"
    if [ -d "$AOSP/$path/.git" ]; then
        echo "==> $path: exists"
        return
    fi
    echo "==> $path @ ${sha:0:12}"
    if git init -q "$AOSP/$path" \
       && git -C "$AOSP/$path" remote add origin "$url" \
       && git -C "$AOSP/$path" fetch -q --depth 1 origin "$sha" \
       && git -C "$AOSP/$path" checkout -q FETCH_HEAD; then
        return
    fi
    echo "    shallow fetch failed, full clone..."
    rm -rf "$AOSP/$path"
    git -c advice.detachedHead=false clone -q --no-checkout "$url" "$AOSP/$path"
    git -C "$AOSP/$path" checkout -q "$sha"
}

fetch packages/modules/adb https://android.googlesource.com/platform/packages/modules/adb 1cf2f017d312f73b3dc53bda85ef2610e35a80e9
fetch system/core         https://android.googlesource.com/platform/system/core         a3b721a32242006b59cb12bd62c9133632af3a2d
fetch system/libbase      https://android.googlesource.com/platform/system/libbase      6d19b5c690fa4220c10e42c2326150bbd4d4b7bb
fetch system/logging      https://android.googlesource.com/platform/system/logging       bcac7c30d88a3773a7c0bc9f5617a23a886331fd
fetch system/libziparchive https://android.googlesource.com/platform/system/libziparchive 3861cb86976b51f61193255f61b7bcf99c5f2918
fetch external/fmtlib     https://android.googlesource.com/platform/external/fmtlib     97ceae9d27415d6444f3e24f491da8ed6902ecfa
fetch external/googletest https://android.googlesource.com/platform/external/googletest cbeab5d01811d23ea488efc07f2c31f7c53024e2
fetch external/boringssl  https://android.googlesource.com/platform/external/boringssl  ff82e866658b678df9ae4aac825828ce45b50209
fetch external/protobuf   https://android.googlesource.com/platform/external/protobuf   c8206780937dba60e53ce2fef7cc57c50439c30b
fetch external/libusb     https://android.googlesource.com/platform/external/libusb     7b5267099855d320f6013d667d113e3619f18bf5
