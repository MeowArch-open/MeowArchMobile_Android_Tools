# better_fastboot — AOSP host fastboot, standalone (x86_64 glibc)

Standalone CMake build of AOSP's **official host `fastboot` tool**
(`system/core/fastboot`) plus its Android-forked filesystem helpers
(`mke2fs`, `make_f2fs`, `make_f2fs_casefold`). Not a re-implementation:
the genuine upstream sources are compiled in place, the same code Google's
platform-tools ships — including the official Linux USB backend
(`usb_linux.cpp`, usbdevicefs ioctls, no libusb) that talks to real phones
in bootloader/fastbootd mode out of the box.

Companion to [../adbd-standalone](../adbd-standalone) (the device-side
daemon); shares the same partial AOSP checkout in `../aosp-adbd`.

On top of upstream behavior this build adds four UX features (all
opt-out-able, all no-ops on a non-tty so piped output stays byte-identical):

1. **Live progress bar** with instantaneous speed and ETA while
   downloading/uploading/fetching (`NO_PROGRESS=1` or `--no-progress` to
   disable).
2. **"Did you want 'flash'?"** typo suggestions instead of the terse
   upstream error — `fastboot flass` suggests `flash` instantly, before any
   device wait.
3. **Colors** in terminals that support them (`NO_COLOR=1` / `--no-color` to
   disable, `--color` / `FORCE_COLOR=1` to force on a pipe).
4. **Multiple languages** — English (byte-identical to upstream), Simplified
   Chinese, Traditional Chinese, Japanese. Selected via `--lang=CODE`,
   `LANGUAGE`, `LC_ALL`, `LC_MESSAGES`, or `LANG`; list them with
   `--list-langs`.

## What gets built

| binary                 | purpose                                                                  |
|------------------------|--------------------------------------------------------------------------|
| `fastboot`             | the flashing tool (USB + TCP/UDP transports)                             |
| `mke2fs`               | Android e2fsprogs fork — `-E android_sparse` emits sparse ext4 images     |
| `make_f2fs`            | Android f2fs-tools fork — `-S` sparse mode, `-g android` preset           |
| `make_f2fs_casefold`   | same, with `CONF_CASEFOLD`/`CONF_PROJID` defaults baked in                |
| `mke2fs.conf`          | Android ext4 profile; `fastboot -w` points `MKE2FS_CONFIG` at it          |

`fastboot -w` and `fastboot flash <part>` with userdata-style generation
shell out to `mke2fs`/`make_f2fs` **in the same directory as the fastboot
binary** (`GetExecutableDirectory()`), exactly like Google's platform-tools
dist — the package installs them side by side (see PKGBUILD for the
`/usr/lib` + symlink layout that keeps them out of `e2fsprogs`' way).

## Layout

    CMakeLists.txt        builds fastboot from the AOSP tree in place
    cmake/fs_tools.cmake  Android-forked mke2fs / make_f2fs (mirrors their
                          Android.bp host modules; the forks carry checked-in
                          Android config headers, no autotools needed)
    patches/              applied to the AOSP tree with `patch -N` (idempotent):
      0001  fastboot_driver_interface.h: include <cstdint> explicitly
      0002  super_flash_helper.cpp: include <climits> explicitly (UINT_MAX)
            (both: the pinned 2025-03 snapshot relies on transitive includes
            that modern stdlib headers no longer leak; soong would break the
            same way today)
      0003  UX integration: one-line hooks into fastboot.cpp /
            fastboot_driver.{h,cpp} / util.cpp calling the glue library in
            src/ — progress callback in the driver's transfer loops,
            localized prolog/epilog/error strings, colored OKAY/FAILED,
            Did-you-want on unknown commands/options (validated BEFORE the
            device-wait), and --lang/--color/--no-progress getopt entries.
            The fd-download path also gains 1 MiB transport chunking so the
            bar advances (protocol-transparent: the device consumes exactly
            the DATA byte count regardless of framing).
    src/                  the UX glue library (fastboot_glue), no AOSP code:
      fb_progress.{h,cpp}   bar renderer: throttle, speed ring buffer, ETA
      fb_color.{h,cpp}      SGR escapes; tty/NO_COLOR/FORCE_COLOR detection
      fb_i18n.{h,cpp}       X-macro message IDs + per-locale table registry
      fb_i18n_en.cpp        English (matches upstream wording exactly)
      fb_i18n_zh_cn.cpp     Simplified Chinese
      fb_i18n_zh_tw.cpp     Traditional Chinese (opencc s2t + TW terminology)
      fb_i18n_ja.cpp        Japanese
      fb_i18n_en_generated.h  GENERATED — help rows transcribed from upstream
                            show_help() (see scripts/gen-help-entries.py)
      fb_help.{h,cpp}       help renderer reproducing upstream's exact column
                            layout (cmd col 1-28, desc col 29, overflow rules)
      fb_suggest.{h,cpp}    Levenshtein + prefix/substring ranking over the
                            command/option vocabularies
      fb_completion.{h,cpp} --shell-completion={bash,zsh,fish} script generator
                            (same vocabularies as fb_suggest + help rows)
      fbglue.{h,cpp}        the API the patch calls: EarlyInit, Validate-
                            FirstCommand, UnknownCommandError, ListLangs, ...
    shim/                 build version stamp + platform_tools_version.h.in
                          (generated at configure time; -DPLATFORM_TOOLS_VERSION=...)
                          gnu_compat/: GCC-only shims — force-included compat.h
                          (__builtin_available) and stdatomic.h for liblog
                          (AOSP sources are clang-built in soong; clang builds
                          use neither)
    scripts/fetch-aosp.sh fetches the partial AOSP checkout (pinned SHAs)
                          into ../aosp-adbd (shared with adbd-standalone)
    scripts/gen-help-entries.py
                          regenerates src/fb_i18n_en_generated.h from the
                          AOSP tree's fastboot.cpp (run after bumping SHAs)
    scripts/test-ux.sh    UX smoke suite: selftest + byte-parity + Did-you-want
                          + NO_COLOR/NO_PROGRESS + live bar against tests/
                          mock_device.py (mock TCP fastboot device, pty capture)
    scripts/test-ux-wine.sh
                          the same checks against fastboot.exe under wine
    tests/mock_device.py  the mock device (FB01 framing, throttled ingest)
    tests/help-baseline-en.txt
                          upstream `fastboot --help` (first 100 lines), captured
                          from the distro platform-tools build — the byte-parity
                          reference for both the Linux and Windows suites
    cmake/mingw-i686.cmake  Windows cross toolchain (mingw-w64 i686, as soong)
    cmake/win32.cmake     Windows-specific source/flags selection
    shim/win_compat/openssl/sha.h
                          SHA256 shim over libavb for the Windows liblp build
    win-prebuilt/usb/     Google's prebuilt AdbWinApi.dll/.a + AdbWinUsbApi.dll
                          and adb_api.h (Apache-2.0; the exact files soong
                          links into the official Windows fastboot.exe)
    PKGBUILD              Arch package `fastboot-meowarch` (x86_64)

## Upstream

Sources compile **in place** from the partial AOSP checkout (`../aosp-adbd`).
Pinned revisions (2025-03 AOSP main — main froze 2025-03-27, so these tips
are the snapshot):

| component                                   | commit                                     | license    |
|---------------------------------------------|--------------------------------------------|------------|
| system/core (fastboot, libsparse, libcutils sockets, diagnose_usb, fs_mgr/liblp, libstorage_literals) | `a3b721a32242006b59cb12bd62c9133632af3a2d` | Apache-2.0 (fastboot: BSD-3) |
| system/libbase                              | `6d19b5c690fa4220c10e42c2326150bbd4d4b7bb` | Apache-2.0 |
| system/logging (liblog)                     | `bcac7c30d88a3773a7c0bc9f5617a23a886331fd` | Apache-2.0 |
| system/libziparchive                        | `3861cb86976b51f61193255f61b7bcf99c5f2918` | Apache-2.0 |
| system/extras (ext4_utils, mke2fs.conf)     | `fc2494a2abd7ab21774d03deb09c1362bbb0bba8` | Apache-2.0 |
| system/tools/mkbootimg (bootimg.h headers)  | `d2bb0af5ba6d3198a3e99529c97eda1be0b5a093` | Apache-2.0 |
| external/avb (libavb headers)               | `761178607206f4cb2af79ed9eec52d8cbd814adb` | BSD-3 (AVB authors) |
| external/e2fsprogs (mke2fs fork)            | `ef148046cf58dc1009da35ed9edb7a7425e19f39` | GPL-2.0 / LGPL-2.1 |
| external/f2fs-tools (make_f2fs fork)        | `d34dcc887617535a0001ea389a2bb8058f41ea05` | GPL-2.0 / LGPL-2.1 |
| external/fmtlib (built into libbase)        | `97ceae9d27415d6444f3e24f491da8ed6902ecfa` | MIT (fmt)          |
| external/googletest (gtest_prod.h only)     | `cbeab5d01811d23ea488efc07f2c31f7c53024e2` | BSD-3              |
| external/zlib                               | `f29fc757b1f27c182f36790eb0ccc6cf8ec27e8e` | Zlib               |

System libraries (like Arch's own android-tools): `zlib`, `libcrypto`
(OpenSSL — liblp metadata SHA256), `libstdc++`/`libc++`. BoringSSL is **not**
built; nothing here needs TLS. No protobuf, no adb host lib — the host
fastboot chain references neither.

Header-only reuse from the tree: `libavb` (AVB footer constants),
`bootimg.h` (boot/vendor-boot header structs), `libstorage_literals`.

`fastboot --version` reports `PLATFORM_TOOLS_VERSION` via a generated shim
header (default `36.0.0`, the platform-tools series matching the pinned
snapshot; override with `-DPLATFORM_TOOLS_VERSION=...`).

## Build

    ./scripts/fetch-aosp.sh                                          # once: ../aosp-adbd
    cmake -B build-native -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    cmake --build build-native -j$(nproc)
    ./build-native/fastboot_selftest                                 # unit checks (no device)
    bash scripts/test-ux.sh                                          # full UX smoke suite
    makepkg -f                                                       # package (x86_64)

### Windows (cross-compiled; CI-built, like Google's)

Google builds the official Windows `fastboot.exe` with **mingw-w64 GCC, i686**
(soong `x86_windows_host.go`: `-DUSE_MINGW`; `AdbWinApi` pins
`compile_multilib "32"`), not MSVC — the sources are POSIX-flavored
(`getopt_long`, `flock`, `unistd.h`). We reproduce that exactly:

    cmake -B build-win32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
    cmake --build build-win32
    # → build-win32/fastboot.exe (static libstdc++/libgcc, PE32 i386)
    #   + AdbWinApi.dll / AdbWinUsbApi.dll copied next to it (ship all three,
    #     same layout as Google's platform-tools zip)

Same genuine upstream sources and same UX features (colors, progress bar,
Did-you-want, all four languages). Windows-specific substitutions, all
mirroring soong's `windows:` blocks: `usb_windows.cpp` + Google's prebuilt
AdbWinApi (vendored in `win-prebuilt/usb/`, also fetched sparsely by
`fetch-aosp.sh` from `development/host/windows`), `errors_windows.cpp`/
`utf8.cpp` instead of `errors_unix.cpp`, `sockets_windows.cpp` et al. instead
of the unix socket TUs, no `cmsg.cpp`/`event_tag_map.cpp`, zlib compiled from
`external/zlib`, and liblp's `<openssl/sha.h>` satisfied by a 3-function shim
over libavb's SHA256 (`shim/win_compat/`) — same digests, no OpenSSL needed.
mke2fs/make_f2fs are `not_windows` upstream and not built.

Smoke-test the .exe on Linux via wine (`bash scripts/test-ux-wine.sh`); CI
runs it natively on a `windows-latest` runner.

GCC works too (drop the `-DCMAKE_*_COMPILER` flags). Knobs:
`-DFASTBOOT_BUILD_FS_TOOLS=OFF` skips mke2fs/make_f2fs;
`-DFASTBOOT_USE_LIBCXX=ON` links libc++ instead of libstdc++ (upstream soong
host builds use libc++_static, but nothing in this chain needs it — Arch's
android-tools fastboot itself links libstdc++).

## Run

The commands are `fastboot-m` and its alias `fastboot-meowarch` (both symlinks
to the same binary). `fastboot-m` is short; the alias keeps old scripts
working. Neither collides with the distro's android-tools, which owns
`/usr/bin/fastboot` — both packages install side by side.

USB (real phones): works wherever platform-tools works —

    fastboot-m devices                 # bootloader mode
    fastboot-m flash boot boot.img
    fastboot-m -w                      # formats userdata (uses bundled mke2fs/make_f2fs)

If `fastboot-m devices` is empty but `lsusb` sees the phone, it's udev
permissions — install Android udev rules (Arch: `android-udev` AUR) or run
as root.

TCP/UDP fastboot (emulators, network fastboot):

    fastboot-m -s tcp:192.168.1.20:5554 devices

### MeowArchMobile additions

    fastboot-m --list-langs              # en, zh_CN, zh_TW, ja
    fastboot-m --lang=zh_CN devices      # one-shot language override
    LANG=ja_JP.UTF-8 fastboot-m --help   # or via the usual locale variables
    fastboot-m --no-color ...            # plain output (NO_COLOR=1 also works)
    fastboot-m --color ...               # force colors even on a pipe
    fastboot-m --no-progress ...         # no bar (NO_PROGRESS=1 also works)
    fastboot-m --shell-completion=bash   # completion script (also zsh, fish)

Typo handling — errors out immediately (before any `< waiting for any
device >`), with a suggestion when something is close:

    $ fastboot-m flass boot boot.img
    fastboot-m: error: unknown command flass
    Did you want 'flash'?
    Run 'fastboot-m --help' for usage.

Progress bar (tty only): renders **one line, repainted in place** on the
`Sending/Writing/Fetching/Uploading` lines, with percent, bytes, instantaneous
speed (MiB/s, sampled over a short ring buffer) and ETA, then cleans the line
back to upstream's exact `OKAY [x.xxxs]` format. Bootloader INFO chatter hides
the bar, prints, and the bar resumes.

The frame is assembled in memory, measured in *display columns* (CJK counts
two — the translated prologs used to overflow an 80-column terminal and every
frame then stayed on screen), and truncated to the terminal width, re-queried
each frame so resizes are picked up. When the line is too narrow the detail is
dropped in a fixed order — ETA, then the byte counters, then everything but
the percentage — and finally the prolog itself is shortened with an ellipsis.
Bar and field widths are budgeted rather than measured per frame, so the line
does not jitter as the speed text changes length.

Windows: `ENABLE_VIRTUAL_TERMINAL_PROCESSING` is switched on for the console
handle at startup (Windows Terminal and conhost both leave it off, so `\r`
and `\033[K` were being printed as literal text and every frame appended to
the line until it wrapped). Windows terminals also do not export `TERM`, so the
"TERM must be set" gate is POSIX-only; only an explicit `TERM=dumb` opts out.
On a console that cannot do ANSI at all, the bar degrades to printing one
complete line at the end instead of smearing frames.

Byte-parity guarantees (checked in CI): with colors/progress off (any pipe)
and `LANG=en`, `--help`/`--version`/error output match upstream exactly —
except the five extra options appended to the help and the `Did you want`
hint replacing the bare `unknown command` line.

### Shell completions (bash / zsh / fish)

The binary generates its own completion scripts, so the vocabulary always
matches the build — commands and long options come from the same tables that
power Did-you-want, descriptions from the same generated help rows, and
`--lang` completes the compiled-in language list:

    fastboot-m --shell-completion=bash > /usr/share/bash-completion/completions/fastboot-m
    fastboot-m --shell-completion=zsh  > /usr/share/zsh/site-functions/_fastboot-m
    fastboot-m --shell-completion=fish > /usr/share/fish/vendor_completions.d/fastboot-m.fish

Each shell autoloads its completion by *command name*, so the package installs
the same script a second time under `fastboot-meowarch` (`_fastboot-meowarch`,
`fastboot-meowarch.fish`). The scripts themselves register for both names
(`complete -F _fastboot_m fastboot-m fastboot-meowarch`, `#compdef fastboot-m
fastboot-meowarch`, `complete -c fastboot-m -c fastboot-meowarch`).

The Arch package runs exactly these at package() time (that is why the
upstream 2017-era `fastboot.bash` is no longer shipped: it predates half of
today's command set and knows nothing about our options).

What completes: all commands and options before/instead of a command;
partitions for the *first* argument of `flash`/`flash:raw`/`erase`/`format`/
`fetch`; `*.img` files and directories for the *second and later* arguments of
`flash`/`flash:raw` (so `fastboot-m flash boot_a <TAB>` offers `./boot.img`,
not another partition name), any file for `fetch`'s output argument, and
`*.img` for `boot`/`stage`/`wipe-super`; `*.zip` after `update`;
getvar variable names after `getvar`; slots after `--slot`/`--set-active`/
`set_active` (both `--slot a` and `--slot=a` forms); reboot targets after
`reboot`; `flashing`/`gsi`/`snapshot-update` subcommands; languages after
`--lang`; serials of currently connected devices after `-s` (calls
`fastboot devices`).

Deliberately hidden from `--help` (byte-parity); unknown shells exit 1 with
an error. All three scripts are functionally tested in `scripts/test-ux.sh`
(bash/fish drives inline; zsh via `scripts/test-completions.zsh`, skipped
when zsh is absent), including the flash-second-argument file rule.

## Known quirks (all upstream behavior, not build bugs)

* `make_f2fs -S <size>` SIGFPEs below ~64 MiB (division by zero in the
  overprovisioning math) — the system `make_f2fs` from f2fs-tools does the
  same with `-g android`.
* `mke2fs` without `MKE2FS_CONFIG` falls back to `/etc/mke2fs.conf`, which on
  a modern distro (e2fsprogs ≥ 1.47) enables `orphan_file`/`metadata_csum_seed`
  that the 1.46.6-based fork rejects. fastboot always points `MKE2FS_CONFIG`
  at the bundled Android profile, so this only affects manual invocation.

## License

AOSP sources keep their upstream licenses (Apache-2.0 / BSD-3 / GPL-2.0 as
tabled above). This project's own glue — CMakeLists.txt, cmake/, shims,
patches, packaging — is GPL-3.0 (see ../LICENSE).

## Acknowledgements

* **The Android Open Source Project** — fastboot, libsparse, liblp,
  libziparchive, libbase, liblog, and the Android forks of e2fsprogs /
  f2fs-tools.
* **Android Tools (android-tools package maintainers)** — prior art for
  building fastboot off-Android.
