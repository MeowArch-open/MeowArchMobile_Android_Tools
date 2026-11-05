# cmake/win32.cmake — Windows (mingw-w64 i686) cross build of adb.exe
# ============================================================================
# Reproduces Google's official Windows host adb build, the same way
# cmake/win32.cmake in better_fastboot does for fastboot.exe:
#
#   * Same toolchain family as soong's x86_windows_host.go: mingw-w64 GCC,
#     -DUSE_MINGW -D__USE_MINGW_ANSI_STDIO=1 -D_WIN32_WINNT=0x0601
#     -mno-ms-bitfields, 32-bit (i686) target. MSVC is not an option — the
#     upstream sources are POSIX-flavored and Google builds them with mingw.
#   * Same USB backend: client/usb_windows.cpp + Google's prebuilt
#     AdbWinApi.dll/.a + AdbWinUsbApi.dll (win-prebuilt/usb/, the same files
#     better_fastboot vendors; soong pins compile_multilib "32" for AdbWinApi,
#     hence the i686 target). adb_api.h comes from the sparse development
#     checkout or from the vendored copy.
#   * Same source swaps as packages/modules/adb/Android.bp's windows blocks:
#     libadb_windows_srcs (fdevent_poll, sysdeps_win32, sysdeps/win32/{errno,
#     stat}) replace libadb_posix_srcs (sysdeps_unix, sysdeps/posix/network)
#     and libadb_linux_srcs (fdevent_epoll); libbase swaps errors_unix ->
#     errors_windows + utf8.cpp and drops cmsg.cpp; libcutils swaps the unix
#     socket TUs for the windows ones; liblog drops event_tag_map.cpp.
#   * adb_defaults' windows host_ldlibs verbatim: -lws2_32 -lgdi32 -luserenv
#     -liphlpapi. Its cflags are applied with one deliberate deviation:
#     UNICODE/_UNICODE go on adb's own targets only, not globally, because
#     libbase/file.cpp passes a narrow char* to GetModuleFileName. See the
#     comment below add_compile_definitions().
#   * Entry point is sysdeps_win32.cpp's wmain(), which converts argv from
#     UTF-16 to UTF-8 and calls our main() — hence ldflags -municode.
#
# Differences from the Linux build, all forced by the platform:
#   * Every third-party library is compiled from AOSP's external/ instead of
#     being found by pkg-config: Arch has no mingw cross packages for zlib,
#     lz4, zstd or brotli, so there is nothing to find. These are the same
#     copies soong builds (external/zlib, external/lz4, external/zstd,
#     external/brotli), so compression behaviour matches the Linux binary.
#   * libusb is compiled from external/libusb's Windows backend. Soong links
#     libusb into the Windows adb too (usb_libusb*.cpp are in the unconditional
#     host source list), and ADB_LIBUSB=1 then selects it at runtime; the
#     default on Windows is the AdbWinApi backend (is_libusb_enabled() returns
#     false under _WIN32 unless the env var says otherwise).
#   * Threads: winpthreads via -lpthread (the toolchain file seeds Threads).
#   * Link: -static -static-libgcc -static-libstdc++ so the exe is
#     self-contained; Google's platform-tools adb.exe ships the same way.
#
# Usage:
#   cmake -B build-win32 -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake \
#         -DHOST_PROTOC="$PWD/build-native/protobuf/protoc"
#   cmake --build build-win32
# HOST_PROTOC is mandatory for a cross build: protoc has to run on the build
# machine, not the target one. Build the native tree first.
# Output: build-win32/adb.exe (+ AdbWinApi.dll / AdbWinUsbApi.dll copied next
# to it, as Google's platform-tools dist does).

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

# soong's windowsCflags (cc/config/x86_windows_host.go) plus the parts of
# adb_defaults' windows cflags that are safe to apply to EVERY target,
# third-party included.
#
# UNICODE/_UNICODE are deliberately NOT here. adb_defaults sets them so that a
# narrow-string call to CreateFile fails to compile, but libbase's file.cpp
# passes a char* to GetModuleFileName — it only builds when UNICODE is off. So
# those two go on adb's own targets (see adb_sysdeps in CMakeLists.txt, which
# propagates them PUBLIC to adb_core and adb) and not globally.
add_compile_definitions(
    USE_MINGW
    __USE_MINGW_ANSI_STDIO=1
    __STDC_CONSTANT_MACROS
    _WIN32_WINNT=0x0601
    WINVER=0x0601
    _POSIX_THREAD_SAFE_FUNCTIONS # localtime_r/gmtime_r in mingw <time.h>
    # soong sets this in windowsCflags, globally, and it MUST be global rather
    # than per-target on this platform. mingw's <sys/stat.h> picks the struct
    # layout AND the wstat entry point from it:
    #   without: sizeof(struct stat)=48, st_size at 20, wstat -> _wstat64i32
    #   with 64: sizeof(struct stat)=56, st_size at 24, wstat -> _wstat64
    # sysdeps/win32/stat.cpp's adb_stat() fills the struct (compiled in
    # adb_sysdeps) while client/file_sync_client.cpp reads it (compiled in the
    # adb target, which inherited _FILE_OFFSET_BITS=64 from ziparchive_static's
    # PUBLIC definitions). If the two disagree the reader takes st_size from the
    # wrong offset — a directory push then reads a garbage size, resize()s a
    # Block to it and dies with std::bad_alloc. Verified: exactly the failure a
    # win32 directory push had, while single-file pushes (which never go
    # through local_build_list's lstat) worked.
    #
    # On x86_64/aarch64 Linux this is a non-issue: glibc's struct stat is
    # already 64-bit, so the flag cannot desynchronize two TUs. On 32-bit
    # Windows it can, and this is the line that stops it.
    _FILE_OFFSET_BITS=64
)
# WIN32_LEAN_AND_MEAN and NOMINMAX are deliberately NOT global. Both only
# affect adb's own C++ sources and both break third-party code here:
#   * NOMINMAX removes windows.h's min()/max() macros, and libusb's
#     windows_usbdk.c calls min() expecting the macro (compile error
#     "implicit declaration of function 'min'").
#   * WIN32_LEAN_AND_MEAN drops headers zlib/libusb expect.
# adb_sysdeps carries both PUBLIC for the win32 case instead, which is what
# reaches adb_core and the adb binary — the same scoping trick UNICODE uses.
add_compile_options(-mno-ms-bitfields
                    -Wno-ignored-attributes   # libusb's __stdcall variadic
                    -Wno-thread-safety        # clang-only annotations
                    -Wno-unused-parameter)

# NOTE: the __builtin_available force-include (shim/gnu_compat/compat.h) and the
# GCC-neutral <android-base/thread_annotations.h> are applied by the GNU-compiler
# branch in CMakeLists.txt — mingw GCC is a GNU-family compiler, so no duplicate
# here.
