# adbd-standalone — AOSP adbd for Arch Linux ARM (aarch64, glibc)

[![build](https://github.com/MeowArch-open/MeowArchMobile_Android_Tools/actions/workflows/build.yml/badge.svg)](https://github.com/MeowArch-open/MeowArchMobile_Android_Tools/actions/workflows/build.yml)

Standalone CMake build of AOSP's adbd (`packages/modules/adb` daemon) that runs
on standard Arch Linux ARM — no Android userland, no soong. Produces a native
glibc aarch64 binary speaking both TCP adb and USB (FunctionFS).

## Layout

    CMakeLists.txt            build; references the AOSP tree in place
    cmake/aarch64.cmake       cross toolchain (clang --target=aarch64-linux-gnu, libc++)
    cmake/protobuf-compat/    minimal config.h for AOSP protobuf 21.12
    patches/                  applied to the AOSP tree with `patch -N` (idempotent):
      0001  fdevent.h: include <vector> explicitly
      0002  daemon/main.cpp: ADBD_NO_AUTH env disables key authorization
      0003  daemon/main.cpp: unguard usb_init() so FunctionFS works off-Android
    shim/                     small host implementations:
      processgroup shim (jdwp_service's freezer query), build version stamp,
      sys/system_properties.h + __system_property_wait (bionic-only API that
      PropertyMonitor needs; backed by libbase's host property store)
    sources/adbd_auth/        libadbd_auth vendored from frameworks/native
                              (adbd's auth callback surface, Apache-2.0)
    scripts/fetch-aosp.sh     fetches the partial AOSP checkout (pinned SHAs)
                              into ../aosp-adbd
    scripts/make-sysroot.sh   assembles sysroot-aarch64/ (cross gcc glibc +
                              ALARM packages: libc++, libcap, brotli, zstd, lz4)
    PKGBUILD                  in-tree package; run as `CARCH=aarch64 makepkg -f`
                              on an x86_64 host (the env var survives makepkg.conf,
                              so .PKGINFO gets "arch = aarch64" and the device
                              installs it with a plain pacman -U; without it the
                              package is stamped x86_64 and pacman -U on the
                              device refuses with "not a valid architecture")

## Upstream

The build compiles sources **in place** from a partial AOSP checkout
(`../aosp-adbd`) — no source vendoring except `sources/adbd_auth`. Pinned
component revisions (as of this build, 2025-03 AOSP main):

| component                                | commit                                    | license    |
|------------------------------------------|-------------------------------------------|------------|
| packages/modules/adb                     | `1cf2f017d312f73b3dc53bda85ef2610e35a80e9` | Apache-2.0 |
| system/core (libcutils, libasyncio, diagnose_usb, headers) | `a3b721a32242006b59cb12bd62c9133632af3a2d` | Apache-2.0 |
| system/libbase                           | `6d19b5c690fa4220c10e42c2326150bbd4d4b7bb` | Apache-2.0 |
| system/logging/liblog                    | `bcac7c30d88a3773a7c0bc9f5617a23a886331fd` | Apache-2.0 |
| frameworks/native (libadbd_auth, vendored in sources/adbd_auth) | copied from the same 2025-03 main snapshot | Apache-2.0 |
| external/protobuf                        | `c8206780937dba60e53ce2fef7cc57c50439c30b` | BSD-3      |
| external/boringssl                       | `ff82e866658b678df9ae4aac825828ce45b50209` | ISC-style (BoringSSL authors) |
| external/fmtlib                          | `97ceae9d27415d6444f3e24f491da8ed6902ecfa` | MIT (fmt)  |
| build/soong (libbuildversion header, vendored in shim/include/build) | header copied from the same snapshot | Apache-2.0 |

Upstream repositories:

* adb: https://android.googlesource.com/platform/packages/modules/adb/
* AOSP mirror (GitHub): https://github.com/aosp-mirror
* libadbd_auth: https://android.googlesource.com/platform/frameworks/native/

All AOSP sources remain under their original licenses (Apache-2.0 unless noted
above). This project's own glue — CMakeLists.txt, shims, patches, packaging —
is licensed GPL-3.0 (see LICENSE). AOSP-derived files keep their upstream
license headers intact.

### Vendored files provenance

* `sources/adbd_auth/` — copied from AOSP `frameworks/native/libs/adbd_auth`
  (Apache-2.0, copyright The Android Open Source Project). The daemon's auth
  callback library lives there rather than in packages/modules/adb, so it is
  vendored here unchanged.
* `shim/include/build/version.h` — copied from AOSP
  `build/soong/cc/libbuildversion/include/build/version.h` (Apache-2.0); the
  implementation is ours (shim/build_version.cpp).

## Build

    ./scripts/fetch-aosp.sh                                         # once: ../aosp-adbd
    cmake -B build-native && cmake --build build-native -j$(nproc)  # host tools + protoc
    ./scripts/make-sysroot.sh                                       # once
    cmake -B build-aarch64 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64.cmake \
          -DHOST_PROTOC=$PWD/build-native/protobuf/protoc
    cmake --build build-aarch64 -j$(nproc)
    CARCH=aarch64 makepkg -f                                       # package

The x86_64 build (build-native/adbd) is fully functional too — handy for
development loops with `adb connect 127.0.0.1:<port>`.

## Run (on the device)

    pacman -U adbd-meowarch-*.pkg.tar.zst
    systemctl enable --now adbd          # TCP :5555, ADBD_NO_AUTH=1 by default
    adb connect <device>:5555

USB: the daemon picks up /dev/usb-ffs/adb/ep0 automatically when present —
configure the gadget first, e.g.:

    sudo /usr/lib/adbd/adbd-usb          # binds the FunctionFS gadget to a UDC

Kernel AIO (CONFIG_AIO) must be available for the USB transport.

## Security note

A glibc host has no framework to approve RSA keys, so ADBD_NO_AUTH=1 (default
in /etc/conf.d/adbd) lets *anyone who can reach the port* run shell commands.
Keep ADBD_NO_AUTH=0 and accept "unauthorized" if the device is network-visible;
or firewall the port.

## Acknowledgements

* **The Android Open Source Project** — adbd, libbase, libcutils, liblog,
  libadbd_auth and the rest of the code this project builds.
* **Arch Linux ARM** — the target distribution, whose packaged libc++/libcap/
  brotli/zstd/lz4 the binary links against.
* **Android Tools (android-tools package maintainers)** — prior art for
  building adb components off-Android.
