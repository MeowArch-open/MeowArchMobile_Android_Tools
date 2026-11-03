# adb-standalone — AOSP's host adb client, standalone

[![build](https://github.com/MeowArch-open/MeowArchMobile_Android_Tools/actions/workflows/build.yml/badge.svg)](https://github.com/MeowArch-open/MeowArchMobile_Android_Tools/actions/workflows/build.yml)

The **host** side of AOSP's adb — the `adb` you run on your Linux box — compiled
from `packages/modules/adb` in place, no soong, no Android userland. Not to be
confused with [`adbd-standalone`](../adbd-standalone/), which is the daemon that
runs *on* a device.

Talks to real phones over USB (libusb, plus the native usbfs backend the client
falls back to) and over TCP (`adb connect host:port`), with the genuine
shell/sync/forward/pairing protocol and its compression (brotli/lz4/zstd).

## MeowArchMobile additions

    adb-meowarch --list-langs              # en, zh_CN, zh_TW, ja
    adb-meowarch --lang=zh_CN devices      # one-shot language override
    adb-meowarch --no-color ...            # plain output (NO_COLOR=1 too)
    adb-meowarch --color ...               # force colour on a pipe
    adb-meowarch --no-progress ...         # no transfer bar (NO_PROGRESS=1 too)
    adb-meowarch help                      # categorised help
    adb-meowarch help networking           # one section
    adb-meowarch help --list-sections      # the section keywords

`adb help meowarch` (or `adb help --list-sections` → `meowarch`) lists the five
options this build adds and what each does — since they are stripped from argv
before adb's own parser runs, upstream's help has nowhere to show them. That
section is localized like the rest.

**Colours** (`src/fb_color.cpp`, shared with better_fastboot): `adb devices`
paints its header bold and each device's state — green for `device`, yellow for
`offline`/`unauthorized`/`recovery`/`sideload`/`bootloader`, dim otherwise —
and `adb help` uses bold section headers with a cyan command column. Detection
is `isatty` + `TERM`, with `NO_COLOR` winning over `--color`, and a pipe gets no
escape bytes at all, so scripted output is unchanged. On Windows the console
gets `ENABLE_VIRTUAL_TERMINAL_PROCESSING` turned on first, because conhost and
Windows Terminal leave it off.

**Transfer progress bar** (`src/adb_progress.cpp`): `adb push`/`pull`/`sync`
get one repainted line per file —

    /data/local/tmp/big.img: [#######>----------]  38%  22.1 MiB/57.2 MiB 41.0 MiB/s

with a real percentage, byte counter, instantaneous speed and a bar sized to the
terminal (measured in display columns, so CJK file names do not wrap the line).
One file gets its own bar with that file's size; a directory push/pull or
`adb sync` runs a single bar over the whole batch (that is the only total worth
showing) with the label following the file in flight. It is driven from the
sync layer's progress hook, so every transport — v1, v2, brotli, lz4, zstd —
reports identically, and it is
suppressed when `stderr` is not a tty, when `NO_PROGRESS` is set, and by
`--no-progress`. Upstream's own `[ 42%] name` line is skipped while the bar is
on screen so the two never fight over the same line; the summary line
(`1 file pushed, ...`) still prints as before.

`TERM` matters, like it does for git/ls. Colour is emitted only when `TERM` is
set and is not `dumb`; an unset `TERM` means the terminal has not advertised
ANSI support, so nothing colour-related goes out. The bar is more permissive —
it needs a tty and refuses only `TERM=dumb` — because repainting a line with a
carriage return and an erase-to-end is not colour: on a tty with no `TERM` it
still animates, just without colour. Verified: `TERM=xterm` gives coloured
frames, an unset `TERM` gives plain frames, `TERM=dumb` gives no bar and no
escapes at all.

**Categorised help**: upstream prints one flat block. `adb help` now renders the
same information grouped as global options / general / networking / file
transfer / shell / app installation / debugging / security / scripting /
internal debugging / usb / environment variables, with the command column
aligned. `adb help <section>` prints one group and `adb help --list-sections`
lists the keywords; both accept the English keyword and the translated section
name.

**Errors are coloured and localized too**: the `adb:` prefix is red on a
terminal (one funnel, `error_exit_va`, so `error_exit`, `perror_exit` and the
syntax errors all get it), and the message text comes from the active
language's table. adb builds its errors as format strings, but the ones users
actually see arrive already formatted — `device 'x' not found` is assembled in
`transport.cpp` — so `adbi18n::TranslateError` matches the English text with
each printf specifier as a wildcard and substitutes the captured values into
the translation. Covered today: device not found / offline / no devices / more
than one device / unauthorized (including its multi-line body) / closed /
unknown command / daemon start and connect failures / version mismatch /
install-mode messages. Anything without an entry prints exactly as before.

**Four languages**: en, zh_CN, zh_TW, ja for the help body, the progress-bar
labels and our error strings (`--lang=`, `--list-langs`, or the usual `LANG` /
`LC_ALL` / `LANGUAGE`). Flag descriptions and the environment-variable list stay
English: they are spellings, and a mistranslated flag is worse than an English
one.

## Layout

    CMakeLists.txt            build; references the AOSP tree in place
    patches/
      0001  host UX hooks: adb_mdns.h/adb.cpp declare the mDNS ServiceInfo
            record locally and report "no mDNS backend" in server-status;
            client/main.cpp calls adbglue::EarlyInit (and strips our flags from
            argv); client/commandline.cpp routes help to the categorised
            renderer and colours `adb devices`; client/file_sync_client.cpp
            drives the transfer bar from its byte-accounting hook
    src/                      MeowArchMobile UX glue (not AOSP code):
      fb_color.{h,cpp}              SGR escapes; tty/NO_COLOR/FORCE_COLOR
      adb_progress.{h,cpp}          the push/pull bar (width in display columns)
      adb_i18n.{h,cpp} + _en/_zh_cn/_zh_tw/_ja.cpp
                                    message catalog, four languages
      adb_help.{h,cpp}              categorised + colored help renderer
      adbglue.{h,cpp}               the API the patch calls
    shim/
      include/build/version.h        vendored libbuildversion header
      include/platform_tools_version.h  version stamp (36.0.0, same as fastboot)
      build_version.cpp              android::build::GetBuildNumber()
      mdns/mdns_stub.cpp             mDNS API stubs (discovery off, honestly)
      fastdeploy/fastdeploy_stub.cpp Quick Deploy stubs (falls back to a normal
                                     install; see README "What is not built")
      usb_host_stub.cpp              host usb_cleanup()
    cmake/protobuf-compat/     minimal config.h for AOSP protobuf 21.12
    scripts/fetch-aosp.sh      fetches the partial AOSP checkout (pinned SHAs)
    scripts/test-adb.sh        end-to-end suite against adbd-standalone over TCP
    PKGBUILD                   Arch package `adb-meowarch`

## Upstream

Sources compile **in place** from the partial AOSP checkout (`../aosp-adbd`,
shared with the other two projects). Pinned revisions:

| component                        | commit                                     | license    |
|----------------------------------|--------------------------------------------|------------|
| packages/modules/adb             | `1cf2f017d312f73b3dc53bda85ef2610e35a80e9` | Apache-2.0 |
| system/core (libbase neighbours, libcutils, libcrypto_utils, diagnose_usb) | `a3b721a32242006b59cb12bd62c9133632af3a2d` | Apache-2.0 |
| system/libbase                   | `6d19b5c690fa4220c10e42c2326150bbd4d4b7bb` | Apache-2.0 |
| system/logging/liblog            | `bcac7c30d88a3773a7c0bc9f5617a23a886331fd` | Apache-2.0 |
| system/libziparchive             | `3861cb86976b51f61193255f61b7bcf99c5f2918` | Apache-2.0 |
| external/boringssl               | `ff82e866658b678df9ae4aac825828ce45b50209` | ISC-style  |
| external/protobuf                | `c8206780937dba60e53ce2fef7cc57c50439c30b` | BSD-3      |
| external/fmtlib                  | `97ceae9d27415d6444f3e24f491da8ed6902ecfa` | MIT        |
| external/googletest (header)     | `cbeab5d01811d23ea488efc07f2c31f7c53024e2` | BSD-3      |
| external/libusb (headers only)   | `7b5267099855d320f6013d667d113e3619f18bf5` | LGPL-2.1   |

System libraries: `zlib`, `liblz4`, `libzstd`, `libbrotli`, `libusb-1.0`
(1.0.30 on Arch; adb needs ≥ 1.0.29 for the superspeed-plus descriptor call),
`libstdc++`/`libc++`. boringssl and protobuf are built from the tree, not
taken from the distro — distro protobuf is a libstdc++ build and its symbols do
not match this libc++ binary, and adb pins a protobuf whose generated code must
match its runtime exactly.

## Build

    cd adb-standalone
    ./scripts/fetch-aosp.sh                        # once; shares ../aosp-adbd
    cmake -B build-native -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    cmake --build build-native -j$(nproc)
    makepkg -f                                     # package

The build takes a few minutes the first time (boringssl + protobuf).

## Run

    adb-meowarch devices
    adb-meowarch -s <serial> shell
    adb-meowarch push local.txt /tmp/
    adb-meowarch install app.apk
    adb-meowarch connect 192.168.1.20:5555         # TCP / wireless debugging
    adb-meowarch pair 192.168.1.20:37xxx CODE      # wireless pairing

The package installs `/usr/bin/adb-meowarch` (a symlink into
`/usr/lib/adb-meowarch/`), so the distro's `android-tools` keeps owning
`/usr/bin/adb`; both packages install side by side.

If `adb-meowarch devices` is empty but `lsusb` sees the phone, it is udev
permissions — install Android udev rules (Arch: `android-udev` AUR) or run as
root.

## What is not built, and what that costs you

Two upstream components are outside the AOSP tree this project pins, or need a
service this package does not ship. Both are opt-in paths, and in both cases
the client falls back to something fully supported rather than failing:

* **mDNS discovery** (`adb pair`/`adb connect` by *service name*, `adb mdns`
  commands, automatic wireless-debugging discovery). Upstream gets it from
  openscreen (fetched from `chromium.googlesource.com`, not an AOSP component)
  and `external/mdnsresponder` (a client stub that requires the `mdnsd` daemon
  running and reachable over a Unix socket). `shim/mdns/mdns_stub.cpp` reports
  discovery as unavailable; `adb mdns check` says so, `adb server-status` omits
  the backend instead of naming one that is not there. Connect with an explicit
  address: `adb connect IP:PORT` (the address is shown on the device's wireless
  debugging screen), or pair with the address plus code.
* **Quick Deploy** (`adb install --fastdeploy`). It needs `libandroidfw` from
  `frameworks/base` (to parse `AndroidManifest.xml` out of the APK) and
  compiled-in agent APKs generated by a soong rule. `shim/fastdeploy/` stubs the
  API, so `--fastdeploy` prints the same "Fast Deploy is only compatible with
  devices of API version 24 or higher, ignoring." line it prints for an
  unsupported device and then installs normally.

Everything else is the real thing: shell v2, sync with brotli/lz4/zstd
compression, forwarding, reverse, JDWP, wireless pairing, bugreport, root,
remount, `install` (streamed/push/incremental), `logcat`, `emu`.

## Test

    bash scripts/test-adb.sh                                    # uses build-native/adb
    bash scripts/test-adb.sh build-native/adb ../adbd-standalone/build-native/adbd

It starts `adbd-standalone`'s daemon on a private port, then drives the client
against it in an isolated `$HOME`: connect, `devices`, shell with exit-status
propagation, byte-exact push/pull round trip (256 KiB of random data), recursive
directory push, forward allocation, and `server-status` (which goes through
protobuf TextFormat). 11 checks.

## License

Project glue (CMake, shims, patches, packaging) is GPL-3.0 — see
[LICENSE](../LICENSE). AOSP sources keep their upstream licenses (Apache-2.0
unless noted above).
