# cmake/win32.cmake — Windows (mingw-w64 i686) cross build of fastboot.exe
# ============================================================================
# Reproduces Google's official Windows host fastboot build:
#
#   * Same toolchain family as soong's x86_windows_host.go: mingw-w64 GCC,
#     -DUSE_MINGW -D__USE_MINGW_ANSI_STDIO=1 -D_WIN32_WINNT=0x0601
#     -mno-ms-bitfields, 32-bit (i686) target.
#   * Same USB backend: genuine usb_windows.cpp + Google's prebuilt
#     AdbWinApi.dll/.a + AdbWinUsbApi.dll (development/host/windows/prebuilt,
#     vendored in win-prebuilt/usb/; soong pins compile_multilib "32" for
#     AdbWinApi, hence the i686 target). adb_api.h comes from the sparse
#     development checkout fetch-aosp.sh adds (win-prebuilt/usb/ carries a
#     copy so the build also works from a vendored checkout).
#   * Same source sets as soong's windows blocks: libbase swaps
#     errors_unix.cpp -> errors_windows.cpp + utf8.cpp and drops cmsg.cpp;
#     liblog drops event_tag_map.cpp (sys/mman.h); libcutils swaps the
#     unix socket TUs for sockets_windows.cpp + socket_network_client_windows
#     + socket_inaddr_any_server_windows; fastboot excludes libext4_utils
#     (not_windows static_libs in Android.bp) — liblp keeps only the
#     ext4_utils.h include, whose declarations it never calls on Windows.
#   * mke2fs/make_f2fs are not built (fs tools are a not_windows target);
#     `fastboot -w` on Windows shells out to mke2fs.exe if the user has it,
#     exactly like Google's platform-tools dist.
#
# Differences from the Linux build, all forced by the platform:
#   * zlib is compiled from external/zlib (no system zlib for mingw); the
#     x86 SIMD files are skipped (they need arch-specific intrinsics flags
#     soong sets per-variant; the scalar paths are used instead).
#   * liblp's <openssl/sha.h> is satisfied by shim/win_compat/openssl/sha.h,
#     a 3-function shim over libavb's SHA256 (already compiled here; same
#     digests). No OpenSSL for Windows needed.
#   * Threads: winpthreads via -lpthread (toolchain file seeds Threads).
#   * Link: -static -static-libgcc -static-libstdc++ (self-contained exe),
#     plus ws2_32 (soong's windows host_ldlibs) and the AdbWinApi import lib.
#
# Usage:
#   cmake -B build-win32 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -G Ninja
#   cmake --build build-win32
# Output: build-win32/fastboot.exe (+ AdbWinApi.dll / AdbWinUsbApi.dll to
# distribute next to it, as Google's platform-tools does).

set(WIN32_BUILD TRUE)
set(WINPREBUILT "${CMAKE_CURRENT_SOURCE_DIR}/win-prebuilt/usb")

if(NOT EXISTS "${AOSP_ROOT}/development/host/windows/usb/api/adb_api.h"
   AND NOT EXISTS "${WINPREBUILT}/adb_api.h")
    message(FATAL_ERROR
        "Windows build needs adb_api.h — run scripts/fetch-aosp.sh (adds the "
        "sparse development checkout) or restore win-prebuilt/usb/.")
endif()
if(NOT EXISTS "${WINPREBUILT}/AdbWinApi.a")
    message(FATAL_ERROR "win-prebuilt/usb/AdbWinApi.a missing (Google prebuilt import lib)")
endif()

# soong's windowsCflags, verbatim where applicable.
add_compile_definitions(
    USE_MINGW
    WIN32_LEAN_AND_MEAN
    __USE_MINGW_ANSI_STDIO=1
    __STDC_CONSTANT_MACROS
    _WIN32_WINNT=0x0601
    WINVER=0x0601
    _POSIX_THREAD_SAFE_FUNCTIONS     # localtime_r/gmtime_r in mingw <time.h>
)
add_compile_options(-mno-ms-bitfields -Wno-unused-parameter)

# NOTE: the __builtin_available force-include (shim/gnu_compat/compat.h) is
# already applied by the top-level GNU-compiler branch in CMakeLists.txt —
# mingw GCC is a GNU-family compiler, so no duplicate here.
