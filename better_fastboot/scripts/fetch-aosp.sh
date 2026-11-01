#!/bin/bash
# Fetch the partial AOSP checkout the fastboot standalone build compiles against.
# ---------------------------------------------------------------------------
# Creates ../aosp-adbd (relative to this script's better_fastboot/) with just
# the components CMakeLists.txt references — NOT a full repo/AOSP sync. Each
# component is a shallow-pinned checkout at the revision this build was
# verified against (2025-03 AOSP main snapshot; AOSP main froze 2025-03-27,
# so "main" tips ARE the snapshot). Shares the tree with adbd-standalone:
# components already present are left untouched.
#
# Repos fetched (see README "Upstream" for the full table):
#   system/core               fastboot itself, libsparse, libcutils sockets,
#                             diagnose_usb, liblp + libstorage_literals (fs_mgr)
#   system/libbase            libbase (host property store, files, logging)
#   system/logging            liblog subset
#   system/libziparchive      zip reader used by `fastboot update/flashall`
#   external/fmtlib           fmt (built into libbase; result.h ErrorfImpl)
#   external/zlib             (referenced for completeness; system libz is used)
#   external/googletest       gtest_prod.h header only (libziparchive)
#   external/avb              libavb headers (AVB footer handling)
#   system/tools/mkbootimg    bootimg.h headers
#   system/extras             ext4_utils (liblp block-device sizing) + mke2fs.conf
#   external/e2fsprogs        Android fork: mke2fs with -E android_sparse
#   external/f2fs-tools       Android fork: make_f2fs / make_f2fs_casefold
#   external/xz-embedded      libxz (REPLACE_XZ in payload.bin) for flash-x
#   external/bzip2            libbz (REPLACE_BZ in payload.bin) for flash-x
#   development               SPARSE (host/windows/usb + prebuilt/usb):
#                             adb_api.h and Google's prebuilt AdbWinApi —
#                             Windows (mingw i686) build only, see cmake/win32
#
# NOT fetched here (adbd-standalone needs them, fastboot does not): protobuf,
# boringssl, packages/modules/adb. fastboot's libcrypto need (liblp SHA256)
# is satisfied by the system OpenSSL, so no boringssl checkout is required.
#
# googlesource may need a proxy; export https_proxy (and http_proxy) first if
# direct git fails.
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
    # Shallow fetch of the exact SHA (googlesource allows want-by-sha);
    # fall back to a full clone if the server refuses.
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

fetch system/core           https://android.googlesource.com/platform/system/core           a3b721a32242006b59cb12bd62c9133632af3a2d
fetch system/libbase        https://android.googlesource.com/platform/system/libbase        6d19b5c690fa4220c10e42c2326150bbd4d4b7bb
fetch system/logging        https://android.googlesource.com/platform/system/logging        bcac7c30d88a3773a7c0bc9f5617a23a886331fd
fetch system/libziparchive  https://android.googlesource.com/platform/system/libziparchive  3861cb86976b51f61193255f61b7bcf99c5f2918
fetch external/fmtlib       https://android.googlesource.com/platform/external/fmtlib       97ceae9d27415d6444f3e24f491da8ed6902ecfa
fetch external/zlib         https://android.googlesource.com/platform/external/zlib         f29fc757b1f27c182f36790eb0ccc6cf8ec27e8e
fetch external/googletest   https://android.googlesource.com/platform/external/googletest   cbeab5d01811d23ea488efc07f2c31f7c53024e2
fetch external/avb          https://android.googlesource.com/platform/external/avb          761178607206f4cb2af79ed9eec52d8cbd814adb
fetch system/tools/mkbootimg https://android.googlesource.com/platform/system/tools/mkbootimg d2bb0af5ba6d3198a3e99529c97eda1be0b5a093
fetch system/extras         https://android.googlesource.com/platform/system/extras         fc2494a2abd7ab21774d03deb09c1362bbb0bba8
fetch external/e2fsprogs    https://android.googlesource.com/platform/external/e2fsprogs    ef148046cf58dc1009da35ed9edb7a7425e19f39
fetch external/f2fs-tools   https://android.googlesource.com/platform/external/f2fs-tools   d34dcc887617535a0001ea389a2bb8058f41ea05
# flash-x (payload.bin extraction) needs the two compressors update_engine uses.
# AOSP's own copies, not the system libraries: mingw has no cross liblzma/libbz2
# packages, and matching AOSP keeps decode behaviour identical.
fetch external/xz-embedded  https://android.googlesource.com/platform/external/xz-embedded  d2c16535d139cb185e89120452531bba6b36d3c6
fetch external/bzip2        https://android.googlesource.com/platform/external/bzip2        0ac9a9daea5cce2e775d5da949508593e2ee9206

# development repo — SPARSE: only host/windows/usb (adb_api.h for
# usb_windows.cpp) and host/windows/prebuilt/usb (Google's prebuilt
# AdbWinApi.dll/.a + AdbWinUsbApi.dll, exactly what soong links into the
# official Windows fastboot.exe — AdbWinApi_defaults ships compile_multilib
# "32", so the Windows target here is i686-w64-mingw32, same as Google).
DEVELOPMENT_SHA=88f7870b2c652922dc2afb01c076737e8bc50130
if [ -d "$AOSP/development/.git" ]; then
    echo "==> development: exists"
else
    echo "==> development (sparse: host/windows/usb + prebuilt) @ ${DEVELOPMENT_SHA:0:12}"
    git init -q "$AOSP/development"
    git -C "$AOSP/development" remote add origin \
        https://android.googlesource.com/platform/development
    git -C "$AOSP/development" sparse-checkout init --cone
    git -C "$AOSP/development" sparse-checkout set host/windows/usb host/windows/prebuilt/usb
    git -C "$AOSP/development" fetch -q --depth 1 origin "$DEVELOPMENT_SHA"
    git -C "$AOSP/development" checkout -q FETCH_HEAD
fi

echo "==> done: $AOSP"
echo "    build with: cmake -B build-native && cmake --build build-native"
