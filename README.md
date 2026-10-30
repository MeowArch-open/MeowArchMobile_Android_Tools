# MeowArchMobile_Adbd

[![build](https://github.com/MeowArch-open/MeowArchMobile_Adbd/actions/workflows/build.yml/badge.svg)](https://github.com/MeowArch-open/MeowArchMobile_Adbd/actions/workflows/build.yml)

AOSP's mobile tooling rebuilt standalone from upstream sources — no Android
userland, no soong:

* [`adbd-standalone/`](adbd-standalone/) — AOSP's **adbd** as a native glibc
  binary for **Arch Linux ARM (aarch64)**: TCP adb plus USB (FunctionFS).
  Runs *on* the device.
* [`better_fastboot/`](better_fastboot/) — AOSP's official host **fastboot**
  tool (x86_64 glibc), with the real `usb_linux` backend and the
  Android-forked `mke2fs`/`make_f2fs` helpers that `fastboot -w` needs.
  Runs on the host, talks to real phones.

Both compile the genuine AOSP sources in place from a shared partial checkout
(`../aosp-adbd`, pinned SHAs — see each README's Upstream table).

## Quick start

On the device (adbd):

    pacman -U adbd-meowarch-*.pkg.tar.zst
    sudo systemctl enable --now adbd
    adb connect <device>:5555

On the host (fastboot):

    pacman -U fastboot-meowarch-*.pkg.tar.zst
    fastboot-meowarch devices
    fastboot-meowarch flash boot boot.img

## Build

adbd (x86_64 + aarch64 cross):

    cd adbd-standalone
    ./scripts/fetch-aosp.sh         # pinned partial AOSP sources (~300 MB)
    cmake -B build-native && cmake --build build-native -j$(nproc)
    ./scripts/make-sysroot.sh       # ALARM cross sysroot
    cmake -B build-aarch64 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64.cmake \
          -DHOST_PROTOC=$PWD/build-native/protobuf/protoc
    cmake --build build-aarch64 -j$(nproc)
    CARCH=aarch64 makepkg -f

fastboot (x86_64):

    cd better_fastboot
    ./scripts/fetch-aosp.sh         # shares ../aosp-adbd with adbd-standalone
    cmake -B build-native -G Ninja && cmake --build build-native -j$(nproc)
    makepkg -f

CI builds and smoke-tests all of it on every push.

## License

Project glue (CMake, shims, patches, packaging) is GPL-3.0 — see
[LICENSE](LICENSE). The AOSP sources it builds remain under their upstream
licenses (Apache-2.0 unless noted in `adbd-standalone/README.md` and
`better_fastboot/README.md`).
