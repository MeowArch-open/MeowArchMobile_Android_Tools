# adb-standalone — AOSP's host adb client, standalone

[![build](https://github.com/MeowArch-open/MeowArchMobile_Android_Tools/actions/workflows/build.yml/badge.svg)](https://github.com/MeowArch-open/MeowArchMobile_Android_Tools/actions/workflows/build.yml)

The **host** side of AOSP's adb — the `adb` you run on your Linux box — compiled
from `packages/modules/adb` in place, no soong, no Android userland. Not to be
confused with [`adbd-standalone`](../adbd-standalone/), which is the daemon that
runs *on* a device.

A Windows `adb.exe` cross-builds from the same sources (see
[Build → Windows](#windows-cross-compiled-like-googles)).

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
      adb_completion.{h,cpp}        bash/zsh/fish completion generator
      adb_suggest.{h,cpp}           "Did you want X?" scorer (shared vocabulary)
      adbglue.{h,cpp}               the API the patch calls
    shim/
      include/build/version.h        vendored libbuildversion header
      include/platform_tools_version.h  version stamp (36.0.0, same as fastboot)
      build_version.cpp              android::build::GetBuildNumber()
      mdns/mdns_stub.cpp             mDNS API stubs (discovery off, honestly)
      fastdeploy/fastdeploy_stub.cpp Quick Deploy stubs (falls back to a normal
                                     install; see README "What is not built")
      usb_host_stub.cpp              host usb_cleanup() (Linux only)
      gnu_compat/              GCC shims, used by the mingw cross build:
        compat.h               __builtin_available, _Nonnull, <cstdint>
        stdatomic.h            C11 atomic names in C++ (liblog's logger.h)
        android-base/thread_annotations.h
                               clang-only lock annotations, neutralized
    cmake/protobuf-compat/     minimal config.h for AOSP protobuf 21.12
    cmake/mingw-i686.cmake     Windows cross toolchain (mingw-w64 i686, as soong)
    cmake/win32.cmake          Windows-specific source/flags selection
    win-prebuilt/usb/          Google's prebuilt AdbWinApi (what soong links
                               into the official Windows adb.exe)
    scripts/fetch-aosp.sh      fetches the partial AOSP checkout (pinned SHAs)
    scripts/test-adb.sh        end-to-end suite against adbd-standalone over TCP
    scripts/test-adb-wine.sh   the same suite against adb.exe, under wine
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
| external/libusb                  | `7b5267099855d320f6013d667d113e3619f18bf5` | LGPL-2.1   |
| external/zlib                    | `f29fc757b1f27c182f36790eb0ccc6cf8ec27e8e` | zlib       |
| external/lz4                     | `f2a717445d3a8a1aaa5aeef1cb2e9a4bc739e9b1` | BSD-2      |
| external/zstd                    | `415469b3387190483d00ed838165d022d86cb899` | BSD-3      |
| external/brotli                  | `e6b1ca20ff13da592055bbbc4f907c68ae8931d2` | MIT        |
| development (sparse, Windows)    | `88f7870b2c652922dc2afb01c076737e8bc50130` | Apache-2.0 |

System libraries: `zlib`, `liblz4`, `libzstd`, `libbrotli`, `libusb-1.0`
(1.0.30 on Arch; adb needs ≥ 1.0.29 for the superspeed-plus descriptor call),
`libstdc++`/`libc++`. boringssl and protobuf are built from the tree, not
taken from the distro — distro protobuf is a libstdc++ build and its symbols do
not match this libc++ binary, and adb pins a protobuf whose generated code must
match its runtime exactly.

On Windows all five system libraries come from the tree instead (`external/zlib`,
`external/lz4`, `external/zstd`, `external/brotli`, `external/libusb`), because
Arch ships no mingw cross packages for any of them. They are fetched
unconditionally so one checkout serves both builds and the two cannot drift.

## Build

    cd adb-standalone
    ./scripts/fetch-aosp.sh                        # once; shares ../aosp-adbd
    cmake -B build-native -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    cmake --build build-native -j$(nproc)
    makepkg -f                                     # package

The build takes a few minutes the first time (boringssl + protobuf).

### Windows (cross-compiled, like Google's)

Google builds the official Windows `adb.exe` with **mingw-w64 GCC, i686**
(soong `x86_windows_host.go`: `-DUSE_MINGW`; `AdbWinApi` pins
`compile_multilib "32"`), not MSVC — the sources are POSIX-flavored. We
reproduce that, so the .exe talks to a device through Google's own prebuilt
`AdbWinApi.dll` and `client/usb_windows.cpp`:

    # the cross build needs a HOST protoc: build the native tree first
    cmake -B build-win32 -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake \
          -DHOST_PROTOC="$PWD/build-native/protobuf/protoc"
    cmake --build build-win32 -j$(nproc)
    # → build-win32/adb.exe (static libstdc++/libgcc/winpthread, PE32 i386)
    #   + AdbWinApi.dll / AdbWinUsbApi.dll copied next to it — ship all three,
    #     the same layout as Google's platform-tools zip

Same upstream sources and same UX features (colours, the fixed-width progress
bar, Did-you-want, completion generation, all four languages). Differences, all
mirroring soong's `windows:` blocks:

* `sysdeps_win32.cpp` + `sysdeps/win32/{errno,stat}.cpp` replace
  `sysdeps_unix.cpp` + `sysdeps/posix/network.cpp`; `fdevent_poll.cpp` replaces
  `fdevent_epoll.cpp`; `client/usb_windows.cpp` replaces `client/usb_linux.cpp`.
  `client/usb_libusb*.cpp` stay — they are unconditional upstream too, and
  `ADB_LIBUSB=1` selects them at run time (the default on Windows is
  AdbWinApi, since `is_libusb_enabled()` returns false there).
* libbase swaps `errors_unix.cpp` → `errors_windows.cpp` and adds `utf8.cpp`,
  and drops `cmsg.cpp`; libcutils swaps the five unix socket TUs for the three
  windows ones (`socket_local_*` has no Windows counterpart at all); liblog
  drops `event_tag_map.cpp` (`sys/mman.h`).
* Entry point is `sysdeps_win32.cpp`'s `wmain()`, which converts argv from
  UTF-16 to UTF-8 before calling our `main()` — hence `-municode`.
* `-DUNICODE=1 -D_UNICODE=1` (adb_defaults' windows cflags) are applied to adb's
  own targets only, **not** globally: `libbase/file.cpp` passes a narrow `char*`
  to `GetModuleFileName`, which `UNICODE` would map to the wide version. That is
  also why `adb version` can print `Installed as <path>`.
* `WIN32_LEAN_AND_MEAN` and `NOMINMAX` are scoped the same way — libusb's
  `windows_usbdk.c` calls `min()` expecting the macro `NOMINMAX` removes.
* mingw-w64 ships no libc++, so this build uses libstdc++. Checked rather than
  assumed: both `is_standard_layout` asserts in `types.h` compile clean under
  g++ and clang+libstdc++.
* GCC cannot parse libbase's clang-only thread-safety annotations
  (`static void f(int) EXCLUDES(m)` is a hard error) or `_Nonnull`, so
  `shim/gnu_compat/` supplies neutral versions. The thread-annotation shim is
  header-lookup only and is therefore global; `compat.h` is force-included
  **per target** via `adb_apply_gnu_compat()`, because a global
  `add_compile_options(-include ...)` would also reach the vendored protobuf
  tree and break its configure.

Three upstream defects only surface on Windows, so they are patched there
(`patches/0004`, `0005`, `0006`) rather than worked around in the build. Each
patch body carries the measurement that found it.

* `sysdeps/win32/stat.cpp` passes a `struct adb_stat*` to `wstat()`. That
  compiles only when the toolchain aliases `struct stat` to the fixed-size
  struct — Google's prebuilt mingw does, distro mingw-w64 does not, where
  `wstat()` takes `struct _stat64*`. Now it stats into the fixed-size struct
  and copies, with a `static_assert` on the layout equivalence.
* `is_root_dir()` only recognized `/`, but mingw's `dirname("/tmp")` returns
  `\`, so the directory-creation walk in `copy_local_dir_remote()` never
  terminated and every directory push died with `std::bad_alloc`. It now
  accepts either separator, and the loop also stops on a `dirname()` fixed
  point (`C:\`, `//x`) since that shape hangs on its own.
* `client/usb_windows.cpp`'s `_power_notification_thread()` created a hidden
  window for resume notifications and checked it with `LOG(FATAL)`. A server
  with no desktop to create a window on — session 0, a service account, a
  headless host — therefore died before writing the ack `launch_server()`
  waits for, and the only thing the client reported was `could not read ok
  from ADB Server` / `failed to start daemon`. Both failures are now warnings
  and the thread returns: the lost feature is the kick-after-resume, while
  `device_poll_thread()` keeps finding devices. The thread's own comment had
  already flagged the case as "not sure".

One further requirement is a build-config rule rather than a patch:
`_FILE_OFFSET_BITS=64` must reach **every** translation unit. mingw picks both
`sizeof(struct stat)` (48 vs 56) and which `wstat` entry point is used from it,
so a writer compiled one way and a reader the other silently disagree about
where `st_size` is — that is how a win32 directory push read a garbage size out
of `local_build_list()`'s `lstat` and died in `Block::resize()`, while
single-file pushes worked.

Smoke-test the .exe on Linux via wine (`bash scripts/test-adb-wine.sh`); CI runs
it natively on a `windows-latest` runner.

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
    bash scripts/test-adb-wine.sh                               # the same, against adb.exe

It starts `adbd-standalone`'s daemon on a private port, then drives the client
against it in an isolated `$HOME`: connect, `devices`, shell with exit-status
propagation, byte-exact push/pull round trip (256 KiB of random data), recursive
directory push, forward allocation, and `server-status` (which goes through
protobuf TextFormat). 110 checks, plus 119 layout checks in
`scripts/test-progress-layout.py` and 33 in `test_suggest`.

`test-adb-wine.sh` replays the platform-independent half of that against
`adb.exe` under wine and then goes one step further: it drives the Windows
binary against the **native Linux adbd** over TCP, so a real sync push/pull
round trip has to come back byte-identical through `ws2_32`, `sysdeps_win32`'s
FD indirection and `adb_writev` over WSABUF. It also checks each compression
algorithm (`-z lz4|zstd|brotli|none`) survives the cross-compiled codecs.
38 checks, all passing against the native Linux adbd; wine is required because
adbd is a Linux ELF and cannot run on a `windows-latest` runner. Two of them are
the regression check for `patches/0006`: with `DISPLAY` and `WAYLAND_DISPLAY`
unset they start the server in the foreground and assert it both prints
`power notifications disabled` and still answers a client, then start it detached
the way a user would. A check that only looked for survival would pass on a wine
whose display-less window creation works, so it asserts the warning too — and
reports `skip`, not `pass`, when that warning never appears.

It opens with a pre-flight that asserts wine can run the exe and that the
client can spawn its own server (retried, and on failure it prints wine's
`+process/+server/+seh` trace and exits). Both are environmental
preconditions, and every check below greps a captured file — so without that
gate an environment where the server cannot start reports ten identical
"cannot connect to daemon" failures that read like product bugs and point
nowhere. That is not hypothetical: it is what one CI run looked like.

Two habits the suite had to adopt, both recorded at the call site so nobody
"tidies" them away:

* a wine invocation is **never** piped into a filter. The call that spawns the
  adb server hands that server the same stdout/stderr, so the server inherits
  the pipe's write end and `grep` never sees EOF — the suite hangs at the
  timeout even though adb finished. Redirect to a file, then filter the file.
* `wadb()` records the exit status in `$WINE_RC` rather than leaving it in `$?`,
  because the trailing `grep` would otherwise overwrite it and every
  `wadb ... && bad` or `RC=$?` check would pass vacuously.

`-z` is a **sync** argument, not a global option: it has to follow `push`/`pull`,
or adb's global parser reports "unknown command -z".

## License

Project glue (CMake, shims, patches, packaging) is GPL-3.0 — see
[LICENSE](../LICENSE). AOSP sources keep their upstream licenses (Apache-2.0
unless noted above).
