#!/bin/bash
# Assemble the aarch64 cross sysroot for adbd-standalone.
# ---------------------------------------------------------------------------
# Produces sysroot-aarch64/ next to this script by merging:
#   1. /usr/aarch64-linux-gnu      (cross-gcc: glibc headers/libs, crt)
#   2. Arch Linux ARM aarch64 packages from a mirror:
#        libc++, libc++abi  (adbd requires libc++ — Block's standard-layout
#                            asserts fail with libstdc++'s unique_ptr)
#        libcap, brotli, zstd, lz4
# The mirror must serve versions matching the target system: the binary links
# libc++.so.1 / libbrotli*.so.1 / libzstd.so.1 / liblz4.so.1 / libcap.so.2,
# all of which ship with Arch Linux ARM proper.
#
# Layout produced:
#   include/   glibc + ALARM headers (ALARM files added non-clobbering)
#   lib/       ld-linux, libc, and all ALARM .so files (primary link dir)
#   usr/, lib64/  mirrors for pkg-config and driver search quirks
#   lib/pkgconfig/  hand-written .pc files (the ALARM pkgs ship none in a
#                   form usable cross; version pins below must match the
#                   downloaded package versions)
#
# Usage: ./make-sysroot.sh [mirror-base]
#   mirror-base default: https://mirrors.tuna.tsinghua.edu.cn/archlinuxarm
set -euo pipefail

MIRROR="${1:-https://mirrors.tuna.tsinghua.edu.cn/archlinuxarm}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
SYSROOT="$HERE/sysroot-aarch64"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Packages to overlay, as "repo/name". Versions are NOT pinned: ALARM is a
# rolling mirror and hard-coded filenames 404 as soon as a rebuild lands
# (this broke CI once: brotli 1.2.0 -> 1.2.1, libc++ 23.1.1 -> 23.1.4).
# Each name is resolved against the live repo listing, newest first, so the
# sysroot always matches what a device running ALARM today actually has.
packages=(
    "core/libcap"
    "core/brotli"
    "core/zstd"
    "core/lz4"
    "extra/libc++"
    "extra/libc++abi"
)

echo "==> base: copying /usr/aarch64-linux-gnu"
rm -rf "$SYSROOT"
cp -a /usr/aarch64-linux-gnu "$SYSROOT"

echo "==> fetching ALARM packages"
mkdir -p "$WORK/root"
for p in "${packages[@]}"; do
    repo="${p%%/*}"
    name="${p#*/}"
    # Mirror listings URL-encode '+' as %2B (libc++ -> libc%2B%2B).
    enc="${name//+/%2B}"
    listing="$WORK/$repo.html"
    if [ ! -f "$listing" ]; then
        curl -fsSL --retry 3 -o "$listing" "$MIRROR/aarch64/$repo/"
    fi
    # Exact-name match only: the trailing '-' keeps libcap from matching
    # libcap-ng, libc++ from matching libc++abi, etc. grep -oE already
    # captures the whole href="filename" — just strip the wrapper and pick
    # the newest version.
    file="$(grep -oE "href=\"${enc}-[0-9][^\"]*-aarch64\.pkg\.tar\.(xz|zst)\"" "$listing" \
            | sed 's/^href="//; s/"$//' \
            | sort -u -V | tail -1)"
    if [ -z "$file" ]; then
        echo "ERROR: no aarch64 package matching $name in $MIRROR/aarch64/$repo/" >&2
        exit 1
    fi
    echo "    $repo/$file"
    curl -fsSL --retry 3 -o "$WORK/pkg.tar.xz" "$MIRROR/aarch64/$repo/$file"
    tar -xf "$WORK/pkg.tar.xz" -C "$WORK/root"
done

echo "==> overlaying ALARM files (headers non-clobbering)"
# userland libs land in usr/lib; glibc headers live in include/ (this is how
# the cross-gcc sysroot is laid out), so also mirror new headers there.
cp -a "$WORK/root/usr/." "$SYSROOT/usr/"
cp -an "$WORK/root/usr/include/." "$SYSROOT/include/" 2>/dev/null || true
mkdir -p "$SYSROOT/include/sys"
cp -an "$WORK/root/usr/include/sys/." "$SYSROOT/include/sys/" 2>/dev/null || true

echo "==> linking ALARM libs into lib/ (primary linker path)"
mkdir -p "$SYSROOT/lib"
for so in libc++ libc++abi libcap libbrotlicommon libbrotlienc libbrotlidec libzstd liblz4; do
    # -L dereferences: ALARM's libc++ package ships usr/lib/libc++.so.1 as a
    # symlink into usr/share/libc++/ (multi-ABI layout), and the cross-gcc
    # sysroot may contain dangling links of its own — copying them verbatim
    # leaves libc++.so.1 unresolvable at runtime.
    cp -aL "$SYSROOT"/usr/lib/${so}.so* "$SYSROOT/lib/" 2>/dev/null || true
done
# lib64 mirror (some linkers/drivers look there first)
ln -sfn lib "$SYSROOT/lib64"

echo "==> writing pkg-config files"
# Written out per-library (mirroring upstream brotli's own .pc files, which
# carry Requires.private chains): a sed-based transform of one template
# misfired here once — the pattern "libbrotlienc" never matches the "-l" flag
# "-lbrotlienc", silently leaving every .pc pointing at the encoder.
mkdir -p "$SYSROOT/lib/pkgconfig"
write_pc() {  # write_pc <name> <description> <version> <libs> [requires]
    local name="$1" desc="$2" ver="$3" libs="$4" req="${5:-}"
    cat > "$SYSROOT/lib/pkgconfig/$name.pc" <<EOF
prefix=/usr
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: $name
Description: $desc
Version: $ver
Libs: -L\${libdir} $libs
Requires.private: $req
Cflags: -I\${includedir}
EOF
}
write_pc libbrotlienc "Brotli encoder library" 1.2.0 "-lbrotlienc" "libbrotlicommon >= 1.2.0"
write_pc libbrotlidec "Brotli decoder library" 1.2.0 "-lbrotlidec" "libbrotlicommon >= 1.2.0"
write_pc libbrotlicommon "Brotli common library" 1.2.0 "-lbrotlicommon"
write_pc libzstd "Zstandard library" 1.5.7 "-lzstd"
write_pc liblz4 "LZ4 lossless compression library" 1.10.0 "-llz4"
write_pc libcap "POSIX capabilities library" 2.78 "-lcap"

echo "==> done: $SYSROOT"
echo "    build with:"
echo "      cmake -B build-aarch64 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64.cmake \\"
echo "            -DHOST_PROTOC=build-native/protobuf/protoc"
