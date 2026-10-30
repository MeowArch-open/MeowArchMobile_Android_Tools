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

echo "==> done: $AOSP"
echo "    build with: cmake -B build-native && cmake --build build-native"
