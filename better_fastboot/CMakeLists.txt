# MeowArchMobile better_fastboot — standalone build
# ============================================================
# Builds AOSP's host `fastboot` tool (system/core/fastboot) as a native
# glibc x86_64 binary — the real, complete upstream fastboot chain, not a
# re-implementation. Compiles the genuine AOSP sources in place; only the
# glue (this file, shims, packaging) is ours.
#
# The AOSP tree is referenced in place (no source copying): pass
#   -DAOSP_ROOT=/path/to/aosp-adbd
# Defaults to ../aosp-adbd relative to this file (shared with adbd-standalone).
#
# Targets:
#   fastboot       — the flashing tool (USB via usb_linux.cpp + TCP/UDP)
#   mke2fs         — Android e2fsprogs fork (`-E android_sparse`), for `fastboot -w`
#   make_f2fs      — Android f2fs-tools fork (sparse + android preset)
#   make_f2fs_casefold — make_f2fs with CONF_CASEFOLD/CONF_PROJID
#
# Design notes (why things are the way they are):
#   * USB backend is the official usb_linux.cpp (usbdevicefs ioctls on
#     /dev/bus/usb/*), exactly what Google's platform-tools ships for Linux.
#     No libusb. Real phones work out of the box (with udev rules / perms).
#   * libbase/liblog/libcutils-sockets/libsparse/libziparchive/liblp/
#     libext4_utils/libdiagnose_usb are compiled from AOSP sources.
#   * zlib and OpenSSL(libcrypto) come from the system, like Arch's own
#     android-tools; the AOSP sources are unmodified. boringssl is NOT built
#     (liblp only needs SHA256, which system libcrypto provides identically).
#   * No protobuf, no adb host lib: the host fastboot chain references neither.
#   * libc++ is optional (FASTBOOT_USE_LIBCXX). Upstream host fastboot links
#     libc++_static, but nothing in this chain has adb's standard-layout
#     asserts, so the default system libstdc++ works (matches Arch's fastboot).
#     Flip the option ON to link libc++ instead.

cmake_minimum_required(VERSION 3.20)
project(better_fastboot LANGUAGES C CXX)

# GCC compatibility (AOSP sources are clang-built in soong):
#   * __builtin_available(android N, *) is a clang extension; shim/compat.h
#     is force-included so GCC takes the same android-30 branch clang does.
#   * liblog's logger.h needs C11 <stdatomic.h> names in C++ mode
#     (shim/gnu_compat/stdatomic.h, see log_static below).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-include "${CMAKE_CURRENT_SOURCE_DIR}/shim/gnu_compat/compat.h")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(FASTBOOT_USE_LIBCXX "link against libc++ instead of the system libstdc++" OFF)
option(FASTBOOT_BUILD_FS_TOOLS "also build mke2fs / make_f2fs (needed for `fastboot -w`)" ON)
if(FASTBOOT_USE_LIBCXX)
    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ---------------------------------------------------------------- paths
get_filename_component(AOSP_ROOT_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../aosp-adbd" ABSOLUTE)
set(AOSP_ROOT "${AOSP_ROOT_DEFAULT}" CACHE PATH "AOSP tree to pull fastboot/liblp/... sources from")

set(FASTBOOT "${AOSP_ROOT}/system/core/fastboot")
set(SYSCORE  "${AOSP_ROOT}/system/core")
set(LIBBASE  "${AOSP_ROOT}/system/libbase")
set(LIBLOG   "${AOSP_ROOT}/system/logging/liblog")
set(LIBSPARSE "${AOSP_ROOT}/system/core/libsparse")
set(LIBZIP   "${AOSP_ROOT}/system/libziparchive")
set(LIBLP    "${AOSP_ROOT}/system/core/fs_mgr/liblp")
set(LITERAL  "${AOSP_ROOT}/system/core/fs_mgr/libstorage_literals")
set(EXT4UTIL "${AOSP_ROOT}/system/extras/ext4_utils")
set(AVB      "${AOSP_ROOT}/external/avb")
set(MKBOOTIMG "${AOSP_ROOT}/system/tools/mkbootimg")
set(GTEST    "${AOSP_ROOT}/external/googletest")
set(FMTLIB   "${AOSP_ROOT}/external/fmtlib")
set(E2FSPROGS "${AOSP_ROOT}/external/e2fsprogs")
set(F2FS     "${AOSP_ROOT}/external/f2fs-tools")

if(NOT EXISTS "${FASTBOOT}/Android.bp")
    message(FATAL_ERROR "AOSP_ROOT=${AOSP_ROOT} does not look like an AOSP tree (missing system/core/fastboot/Android.bp)")
endif()
message(STATUS "AOSP tree: ${AOSP_ROOT}")

# ---------------------------------------------------- windows cross build
# cmake/win32.cmake reproduces soong's official Windows host fastboot build
# (mingw-w64 i686 + usb_windows.cpp + Google's prebuilt AdbWinApi). Sets
# WIN32_BUILD and the platform flags; every target below branches on it.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    include(cmake/win32.cmake)
    set(FASTBOOT_BUILD_FS_TOOLS OFF CACHE BOOL "" FORCE)   # not_windows in Android.bp
endif()

# --------------------------------------------------------------- patches
# Source patches for the AOSP tree live in patches/ and are applied to the
# tree in place. Idempotent: rc=1 means "reversed or previously applied"
# (patch -N already skips those), which is fine; anything else is fatal.
# --fuzz=0 is load-bearing: a hunk that only matches with fuzz can be
# re-applied at a shifted offset on every configure, duplicating its block
# (daemon/main.cpp once carried 22 copies of the ADBD_NO_AUTH block that
# way). With fuzz off, a bad hunk fails loudly instead. scripts/
# check-patches.py verifies every patch applies exactly and is idempotent.
#   0001  fastboot_driver_interface.h: include <cstdint> explicitly — the
#         pinned 2025-03 snapshot omits it and newer libstdc++/libc++ headers
#         no longer leak uint32_t into it (soong build broke the same way).
#   0002  super_flash_helper.cpp: include <climits> explicitly (UINT_MAX).
#   0003  MeowArchMobile UX integration: color (fbglue/fb_color), live
#         progress + speed bar (fb_progress via a driver progress callback),
#         localized messages/help (fb_i18n + fb_help), and "Did you want X?"
#         typo suggestions (fb_suggest). Touches fastboot.cpp,
#         fastboot_driver.{h,cpp} and util.cpp only; all behavior lives in
#         src/ (compiled as fastboot_glue below), the patch just wires hooks.
#   0004  usb.h/task.h/udp.h/fastboot.h/tcp.cpp/udp.cpp: include <cstdint>
#         explicitly (same latent-bug class as 0001; clang/libstdc++ leak it,
#         mingw-w64 does not — Windows build needs it, Linux unaffected).
foreach(_patch
        0001-fastboot_driver_interface.h-include-cstdint.patch
        0002-super_flash_helper.cpp-include-climits.patch
        0003-fastboot-ux-color-progress-i18n-suggest.patch
        0004-fastboot-headers-include-cstdint-explicitly.patch)
    # -r /dev/null: when patch -N skips an already-applied hunk it would
    # otherwise litter the tree with .rej files on every re-configure.
    execute_process(
        COMMAND patch -N -s --fuzz=0 --no-backup-if-mismatch -r /dev/null -d "${SYSCORE}" -p1 -i
                "${CMAKE_CURRENT_SOURCE_DIR}/patches/${_patch}"
        RESULT_VARIABLE _patch_rc ERROR_QUIET)
    if(NOT _patch_rc EQUAL 0 AND NOT _patch_rc EQUAL 1)
        message(FATAL_ERROR "Failed to apply ${_patch} to the AOSP tree (rc=${_patch_rc})")
    endif()
endforeach()

# --------------------------------------------------------------- shims
# build/version.h (libbuildversion public header, vendored) +
# platform_tools_version.h (soong genrule stand-in, generated at configure
# time). Implementations in shim/.
#
# PLATFORM_TOOLS_VERSION: the soong genrule reads the release-config value,
# unreachable from here. Default "36.0.0" is the platform-tools series the
# pinned 2025-03 AOSP main snapshot corresponds to; override with
# -DPLATFORM_TOOLS_VERSION=... to match a different dist.
set(PLATFORM_TOOLS_VERSION "36.0.0" CACHE STRING "version string reported by `fastboot --version`")
configure_file(shim/platform_tools_version.h.in
               "${CMAKE_CURRENT_BINARY_DIR}/gen/include/platform_tools_version.h" @ONLY)

add_library(fb_shim STATIC
    shim/build_version.cpp
)
target_include_directories(fb_shim PUBLIC
    shim/include
    "${CMAKE_CURRENT_BINARY_DIR}/gen/include"
)

# ---------------------------------------------------------- system deps
find_package(Threads REQUIRED)
if(WIN32_BUILD)
    # No system zlib/OpenSSL for mingw: zlib is compiled from external/zlib
    # below (zlib_win_static), and liblp's <openssl/sha.h> is satisfied by
    # shim/win_compat/openssl/sha.h (3-function shim over libavb's SHA256,
    # already a fastboot dependency — same digests, no extra library).
    add_library(zlib_win_static STATIC
        ${AOSP_ROOT}/external/zlib/adler32.c
        ${AOSP_ROOT}/external/zlib/compress.c
        ${AOSP_ROOT}/external/zlib/crc32.c
        ${AOSP_ROOT}/external/zlib/deflate.c
        ${AOSP_ROOT}/external/zlib/gzclose.c
        ${AOSP_ROOT}/external/zlib/gzlib.c
        ${AOSP_ROOT}/external/zlib/gzread.c
        ${AOSP_ROOT}/external/zlib/gzwrite.c
        ${AOSP_ROOT}/external/zlib/infback.c
        ${AOSP_ROOT}/external/zlib/inffast.c
        ${AOSP_ROOT}/external/zlib/inflate.c
        ${AOSP_ROOT}/external/zlib/inftrees.c
        ${AOSP_ROOT}/external/zlib/trees.c
        ${AOSP_ROOT}/external/zlib/uncompr.c
        ${AOSP_ROOT}/external/zlib/zutil.c
        # adler32_simd.c / crc32_simd.c / crc_folding.c excluded: they need
        # per-variant SIMD flags soong sets via arch-specific srcs blocks; the
        # scalar paths above are the portable fallback (soong builds the same
        # scalar set for non-x86 host variants).
    )
    target_include_directories(zlib_win_static PUBLIC ${AOSP_ROOT}/external/zlib)
    # soong's libz_defaults cflags that matter for correctness:
    target_compile_definitions(zlib_win_static PRIVATE
        HAVE_HIDDEN ZLIB_CONST _LARGEFILE64_SOURCE)
    set(ZLIB_TARGET zlib_win_static)
else()
    find_package(PkgConfig QUIET)
    find_package(OpenSSL REQUIRED)      # libcrypto: SHA256 for liblp metadata
    if(PkgConfig_FOUND)
        pkg_check_modules(ZLIB IMPORTED_TARGET zlib)
    endif()
    if(NOT ZLIB_FOUND)
        message(FATAL_ERROR "system zlib not found via pkg-config")
    endif()
    set(ZLIB_TARGET PkgConfig::ZLIB)
endif()

# --------------------------------------------------------------- liblog
# Host subset of system/logging/liblog (same set the adbd standalone build uses).
add_library(log_static STATIC
    ${LIBLOG}/log_event_list.cpp
    ${LIBLOG}/log_event_write.cpp
    ${LIBLOG}/logger_name.cpp
    ${LIBLOG}/logger_read.cpp
    ${LIBLOG}/logger_write.cpp
    ${LIBLOG}/logprint.cpp
    ${LIBLOG}/properties.cpp
    $<$<NOT:$<BOOL:${WIN32_BUILD}>>:${LIBLOG}/event_tag_map.cpp>
    # soong: event_tag_map.cpp is not_windows (needs sys/mman.h).
)
target_include_directories(log_static PUBLIC
    ${LIBLOG}/include
    ${LIBBASE}/include
    ${SYSCORE}/libcutils/include
    ${SYSCORE}/libcutils/include/private
    ${SYSCORE}/libutils/include
    ${SYSCORE}/libsystem/include
    ${SYSCORE}/include
)
target_compile_definitions(log_static PRIVATE
    LIBLOG_LOG_TAG=1006
    SNET_EVENT_LOG_TAG=1397638484
    ANDROID_DEBUGGABLE=0
    _GNU_SOURCE
)

# GCC compatibility: liblog's logger.h declares `atomic_int fd` after
# including <stdatomic.h>, which clang exposes in C++ mode but g++ does not.
# Prepend a shim that maps the C11 atomic names onto <atomic> for GCC only.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_include_directories(log_static BEFORE PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/shim/gnu_compat")
endif()

# ------------------------------------------------------------------ fmtlib
# libbase's result.h ErrorfImpl uses fmt::vformat; soong builds external/fmtlib
# (11.0.2) into libbase with -fno-exceptions. Build the same TU here rather
# than linking the system libfmt — the AOSP headers on base_static's include
# path must be read against matching library symbols.
add_library(fmt_static STATIC ${FMTLIB}/src/format.cc)
target_include_directories(fmt_static PUBLIC ${FMTLIB}/include)
# soong compiles fmt with -fno-exceptions and un-defines NDEBUG (fmt falls
# back to assert() checks when exceptions are off).
target_compile_options(fmt_static PRIVATE -fno-exceptions -UNDEBUG)

# -------------------------------------------------------------- libbase
# Soong's windows block swaps errors_unix.cpp for errors_windows.cpp + utf8.cpp
# and excludes cmsg.cpp (needs <sys/socket.h>).
add_library(base_static STATIC
    ${LIBBASE}/abi_compatibility.cpp
    ${LIBBASE}/chrono_utils.cpp
    $<$<NOT:$<BOOL:${WIN32_BUILD}>>:${LIBBASE}/cmsg.cpp>
    ${LIBBASE}/file.cpp
    ${LIBBASE}/hex.cpp
    ${LIBBASE}/logging.cpp
    ${LIBBASE}/mapped_file.cpp
    ${LIBBASE}/parsebool.cpp
    ${LIBBASE}/parsenetaddress.cpp
    ${LIBBASE}/posix_strerror_r.cpp
    ${LIBBASE}/process.cpp
    ${LIBBASE}/properties.cpp
    ${LIBBASE}/result.cpp
    ${LIBBASE}/stringprintf.cpp
    ${LIBBASE}/strings.cpp
    ${LIBBASE}/threads.cpp
    $<IF:$<BOOL:${WIN32_BUILD}>,${LIBBASE}/errors_windows.cpp,${LIBBASE}/errors_unix.cpp>
    $<$<BOOL:${WIN32_BUILD}>:${LIBBASE}/utf8.cpp>
)
target_include_directories(base_static PUBLIC
    ${LIBBASE}/include
    ${FMTLIB}/include
)
target_link_libraries(base_static PUBLIC log_static fmt_static Threads::Threads)

# -------------------------------------------------------- libcutils sockets
# fastboot's socket.cpp uses cutils_socket_t + socket_send_buffers et al.
# Soong's windows block substitutes the *_windows TUs for the unix ones
# (sockets.cpp itself is platform-neutral: getsockname/socket_close).
add_library(cutils_sockets STATIC
    ${SYSCORE}/libcutils/sockets.cpp
    $<$<NOT:$<BOOL:${WIN32_BUILD}>>:${SYSCORE}/libcutils/socket_inaddr_any_server_unix.cpp>
    $<$<NOT:$<BOOL:${WIN32_BUILD}>>:${SYSCORE}/libcutils/socket_local_client_unix.cpp>
    $<$<NOT:$<BOOL:${WIN32_BUILD}>>:${SYSCORE}/libcutils/socket_local_server_unix.cpp>
    $<$<NOT:$<BOOL:${WIN32_BUILD}>>:${SYSCORE}/libcutils/socket_network_client_unix.cpp>
    $<$<NOT:$<BOOL:${WIN32_BUILD}>>:${SYSCORE}/libcutils/sockets_unix.cpp>
    $<$<BOOL:${WIN32_BUILD}>:${SYSCORE}/libcutils/sockets_windows.cpp>
    $<$<BOOL:${WIN32_BUILD}>:${SYSCORE}/libcutils/socket_inaddr_any_server_windows.cpp>
    $<$<BOOL:${WIN32_BUILD}>:${SYSCORE}/libcutils/socket_network_client_windows.cpp>
)
target_include_directories(cutils_sockets PUBLIC
    ${SYSCORE}/libcutils/include
    ${SYSCORE}/libcutils
)
target_link_libraries(cutils_sockets PUBLIC log_static)
target_compile_definitions(cutils_sockets PRIVATE _GNU_SOURCE)
if(WIN32_BUILD)
    target_link_libraries(cutils_sockets PUBLIC ws2_32)
endif()

# --------------------------------------------------------- diagnose_usb
add_library(diagnose_usb STATIC
    ${SYSCORE}/diagnose_usb/diagnose_usb.cpp
)
target_include_directories(diagnose_usb PUBLIC ${SYSCORE}/diagnose_usb/include)
target_link_libraries(diagnose_usb PUBLIC base_static)

# ------------------------------------------------------------- libsparse
add_library(sparse_static STATIC
    ${LIBSPARSE}/backed_block.cpp
    ${LIBSPARSE}/output_file.cpp
    ${LIBSPARSE}/sparse.cpp
    ${LIBSPARSE}/sparse_crc32.cpp
    ${LIBSPARSE}/sparse_err.cpp
    ${LIBSPARSE}/sparse_read.cpp
)
target_include_directories(sparse_static PUBLIC
    ${LIBSPARSE}/include          # <sparse/sparse.h>
    ${LIBSPARSE}                  # internal backed_block.h etc.
)
target_link_libraries(sparse_static PUBLIC base_static ${ZLIB_TARGET})
target_compile_definitions(sparse_static PRIVATE _FILE_OFFSET_BITS=64)

# ---------------------------------------------------------- libziparchive
# Reader only (fastboot uses OpenArchive/FindEntry/ExtractEntryToFile); the
# default libziparchive variant defines INCFS_SUPPORT_DISABLED and needs no
# signal_handling.cpp on a non-bionic host.
add_library(ziparchive_static STATIC
    ${LIBZIP}/zip_archive.cc
    ${LIBZIP}/zip_archive_stream_entry.cc
    ${LIBZIP}/zip_cd_entry_map.cc
    ${LIBZIP}/zip_error.cpp
)
target_include_directories(ziparchive_static PUBLIC
    ${LIBZIP}/include                       # <ziparchive/zip_archive.h>
    ${LIBZIP}                               # internal zip_archive_private.h etc.
    ${LIBZIP}/incfs_support/include         # incfs_support/*.h
    ${GTEST}/googletest/include             # <gtest/gtest_prod.h>
)
target_link_libraries(ziparchive_static PUBLIC base_static log_static ${ZLIB_TARGET})
target_compile_definitions(ziparchive_static PUBLIC
    INCFS_SUPPORT_DISABLED=1
    ZLIB_CONST
    _FILE_OFFSET_BITS=64
)

# ---------------------------------------------------------- libext4_utils
# soong links libext4_utils only on not_windows; liblp includes its header but
# never calls a symbol from it on Windows (verified: nm -u shows no ext4/wipe
# refs), so on Windows we skip the library and just keep the include dir.
if(NOT WIN32_BUILD)
    add_library(ext4_utils_static STATIC
        ${EXT4UTIL}/ext4_utils.cpp
        ${EXT4UTIL}/wipe.cpp
        ${EXT4UTIL}/ext4_sb.cpp
    )
    target_include_directories(ext4_utils_static PUBLIC ${EXT4UTIL}/include)
    target_link_libraries(ext4_utils_static PUBLIC base_static ${ZLIB_TARGET})
    target_compile_definitions(ext4_utils_static PRIVATE _FILE_OFFSET_BITS=64)
    target_compile_options(ext4_utils_static PRIVATE -fno-strict-aliasing)
endif()

# ----------------------------------------------------------------- liblp
add_library(lp_static STATIC
    ${LIBLP}/builder.cpp
    ${LIBLP}/super_layout_builder.cpp
    ${LIBLP}/images.cpp
    ${LIBLP}/partition_opener.cpp
    ${LIBLP}/property_fetcher.cpp
    ${LIBLP}/reader.cpp
    ${LIBLP}/utility.cpp
    ${LIBLP}/writer.cpp
)
target_include_directories(lp_static PUBLIC
    ${LIBLP}/include            # <liblp/liblp.h>
    ${LIBLP}                    # internal reader.h/writer.h/utility.h/images.h
    ${LITERAL}                  # <storage_literals/storage_literals.h>
    ${EXT4UTIL}/include         # <ext4_utils/ext4_utils.h> (utility.cpp)
)
if(WIN32_BUILD)
    # SHA256 comes from shim/win_compat/openssl/sha.h over libavb (compiled
    # into the fastboot_static link below); no OpenSSL, no ext4_utils library.
    target_include_directories(lp_static PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/shim/win_compat"   # <openssl/sha.h> shim
        ${AVB}                                          # <libavb/avb_sha.h>
        ${AVB}/libavb
        ${AVB}/libavb/sha
    )
    target_compile_definitions(lp_static PUBLIC AVB_COMPILATION)
    target_link_libraries(lp_static PUBLIC base_static log_static sparse_static)
else()
    target_link_libraries(lp_static PUBLIC
        base_static log_static sparse_static ext4_utils_static OpenSSL::Crypto)
endif()
target_compile_definitions(lp_static PRIVATE _FILE_OFFSET_BITS=64)

# ------------------------------------------- payload.bin decoders (flash-x)
# `flash-x` extracts a partition image out of an Android payload.bin, so it
# needs the two compressors update_engine uses: xz (REPLACE_XZ) and bzip2
# (REPLACE_BZ). Both come from AOSP's own external/ checkouts rather than the
# system libraries, for two reasons: mingw has no cross liblzma/libbz2 packages
# in Arch's repos (so Windows could not link them at all), and matching AOSP's
# copies keeps decoding behaviour identical to update_engine.
#
# xz-embedded is the kernel-derived decoder AOSP ships as libxz; its userspace
# shim (userspace/xz_config.h) supplies kmalloc/memzero and friends. The BCJ
# filters and CRC64 are enabled exactly as external/xz-embedded/Android.bp does,
# plus XZ_DEC_ANY_CHECK so a payload whose xz stream carries a SHA-256 check
# (which xz-embedded cannot verify) still decodes instead of failing — the
# manifest's own data_sha256_hash is verified separately below.
set(XZEMBEDDED "${AOSP_ROOT}/external/xz-embedded")
set(BZIP2      "${AOSP_ROOT}/external/bzip2")

add_library(xz_dec_static STATIC
    ${XZEMBEDDED}/linux/lib/xz/xz_crc32.c
    ${XZEMBEDDED}/linux/lib/xz/xz_crc64.c
    ${XZEMBEDDED}/linux/lib/xz/xz_dec_stream.c
    ${XZEMBEDDED}/linux/lib/xz/xz_dec_lzma2.c
    ${XZEMBEDDED}/linux/lib/xz/xz_dec_bcj.c
)
target_include_directories(xz_dec_static PUBLIC
    ${XZEMBEDDED}/linux/include/linux   # <xz.h>
    ${XZEMBEDDED}/userspace             # "xz_config.h"
)
# PUBLIC, not PRIVATE: XZ_USE_CRC64 changes what <xz.h> declares (it exposes
# xz_crc64_init), and the caller MUST run that initializer before decoding —
# otherwise the CRC64 table stays all-zero and every stream fails with
# XZ_DATA_ERROR. Same for XZ_DEC_ANY_CHECK, which is what lets a stream carrying
# a SHA-256 check (xz-embedded cannot verify it) still decode instead of being
# rejected outright.
target_compile_definitions(xz_dec_static PUBLIC
    XZ_DEC_X86 XZ_DEC_ARM XZ_DEC_ARMTHUMB XZ_DEC_ARM64
    XZ_USE_CRC64 XZ_DEC_ANY_CHECK XZ_DEC_CONCATENATED)

# soong's libbz (external/bzip2/Android.bp): these seven sources, -O3 -DUSE_MMAP.
add_library(bz2_dec_static STATIC
    ${BZIP2}/blocksort.c
    ${BZIP2}/bzlib.c
    ${BZIP2}/compress.c
    ${BZIP2}/crctable.c
    ${BZIP2}/decompress.c
    ${BZIP2}/huffman.c
    ${BZIP2}/randtable.c
)
target_include_directories(bz2_dec_static PUBLIC ${BZIP2})
target_compile_definitions(bz2_dec_static PRIVATE USE_MMAP)

# ------------------------------------------------------ fastboot glue (UX)
# MeowArchMobile additions compiled alongside the upstream sources: color,
# live progress bar + speed, message catalog (i18n), generated help renderer
# and "Did you want X?" typo suggestions. Patch 0003 wires the upstream tree
# into this library; everything else here is self-contained.
add_library(fastboot_glue STATIC
    src/fb_color.cpp
    src/fb_payload.cpp
    src/fb_progress.cpp
    src/fb_i18n.cpp
    src/fb_i18n_en.cpp
    src/fb_i18n_zh_cn.cpp
    src/fb_i18n_zh_tw.cpp
    src/fb_i18n_ja.cpp
    src/fb_help.cpp
    src/fb_suggest.cpp
    src/fb_completion.cpp
    src/fbglue.cpp
)
target_include_directories(fastboot_glue PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(fastboot_glue PUBLIC
    base_static xz_dec_static bz2_dec_static)
# fb_payload.cpp verifies SHA-256 through <openssl/sha.h>: the system OpenSSL
# on Linux, and shim/win_compat/openssl/sha.h (over libavb, as liblp does) on
# Windows — one API, identical digests, no extra dependency on either platform.
if(WIN32_BUILD)
    target_include_directories(fastboot_glue PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/shim/win_compat"
        ${AVB} ${AVB}/libavb ${AVB}/libavb/sha)
    target_compile_definitions(fastboot_glue PUBLIC AVB_COMPILATION)
    target_link_libraries(fastboot_glue PUBLIC avb_sha_win)
else()
    target_link_libraries(fastboot_glue PUBLIC OpenSSL::Crypto)
endif()
target_compile_options(fastboot_glue PRIVATE -Wall -Wextra -Wformat=2)
# Format-string safety: fb_progress/fb_i18n build format strings by hand and
# hand them to fprintf/StringPrintf; a %s/arg count mismatch reads a garbage
# va_arg and SIGSEGVs (this shipped once — caught by the ASAN job). Real format
# errors are fatal; -Wformat-nonliteral is expected noise because passing
# translated table strings as formats is the design (the selftest asserts
# specifier parity across languages).
target_compile_options(fastboot_glue PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-Werror=format -Wno-format-nonliteral>)

# ------------------------------------------------------------- fastboot
# Common include dirs for the fastboot translation units.
set(FB_INCS
    ${FASTBOOT}                 # "fastboot.h", "transport.h", ...
    ${AVB}                      # <libavb/libavb.h>
    ${MKBOOTIMG}/include/bootimg # <bootimg.h>
    ${SYSCORE}/diagnose_usb/include
)

add_library(fastboot_static STATIC
    ${FASTBOOT}/bootimg_utils.cpp
    ${FASTBOOT}/fastboot_driver.cpp
    ${FASTBOOT}/fastboot.cpp
    ${FASTBOOT}/filesystem.cpp
    ${FASTBOOT}/fs.cpp
    ${FASTBOOT}/socket.cpp
    ${FASTBOOT}/storage.cpp
    ${FASTBOOT}/super_flash_helper.cpp
    ${FASTBOOT}/tcp.cpp
    ${FASTBOOT}/udp.cpp
    ${FASTBOOT}/util.cpp
    ${FASTBOOT}/vendor_boot_img_utils.cpp
    ${FASTBOOT}/task.cpp
    $<IF:$<BOOL:${WIN32_BUILD}>,${FASTBOOT}/usb_windows.cpp,${FASTBOOT}/usb_linux.cpp>
    # soong: windows -> usb_windows.cpp (AdbWinApi), linux -> usb_linux.cpp
    # (usbdevicefs). Both are the genuine upstream backends.
)
target_include_directories(fastboot_static PUBLIC ${FB_INCS})
if(WIN32_BUILD)
    # adb_api.h: sparse development checkout (fetched by scripts/fetch-aosp.sh),
    # with the vendored copy in win-prebuilt/usb as fallback.
    if(EXISTS "${AOSP_ROOT}/development/host/windows/usb/api/adb_api.h")
        target_include_directories(fastboot_static PUBLIC
            "${AOSP_ROOT}/development/host/windows/usb/api")
    else()
        target_include_directories(fastboot_static PUBLIC "${WINPREBUILT}")
    endif()
    # libavb's SHA256 impl backs the <openssl/sha.h> shim (sha256_impl.c +
    # avb_sysdeps_posix.c for avb_memcpy/avb_memset are dependency-free C;
    # the same files soong builds into libavb).
    add_library(avb_sha_win STATIC
        ${AVB}/libavb/sha/sha256_impl.c
        ${AVB}/libavb/avb_sysdeps_posix.c
    )
    target_include_directories(avb_sha_win PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/shim/win_compat"   # <openssl/sha.h> shim
        ${AVB} ${AVB}/libavb ${AVB}/libavb/sha)
    target_compile_definitions(avb_sha_win PRIVATE AVB_COMPILATION)
endif()
target_compile_definitions(fastboot_static PUBLIC
    ANDROID_BASE_UNIQUE_FD_DISABLE_IMPLICIT_CONVERSION
    _FILE_OFFSET_BITS=64
    $<$<NOT:$<BOOL:${WIN32_BUILD}>>:_GNU_SOURCE>
)
target_link_libraries(fastboot_static PUBLIC
    fastboot_glue
    lp_static
    ziparchive_static
    sparse_static
    $<$<NOT:$<BOOL:${WIN32_BUILD}>>:ext4_utils_static>
    $<$<BOOL:${WIN32_BUILD}>:avb_sha_win>
    diagnose_usb
    cutils_sockets
    base_static
    log_static
    fb_shim
    ${ZLIB_TARGET}
    $<$<NOT:$<BOOL:${WIN32_BUILD}>>:OpenSSL::Crypto>
    Threads::Threads
)

add_executable(fastboot ${FASTBOOT}/main.cpp)
target_link_libraries(fastboot PRIVATE fastboot_static)
if(WIN32_BUILD)
    # Google's prebuilt AdbWinApi (development/host/windows/prebuilt/usb,
    # vendored): fastboot.exe links the import lib and loads the DLL at run
    # time, exactly like the official platform-tools Windows dist. The DLLs
    # are copied next to the binary for smoke tests / packaging.
    target_link_libraries(fastboot PRIVATE "${WINPREBUILT}/AdbWinApi.a" ws2_32)
    add_custom_command(TARGET fastboot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${WINPREBUILT}/AdbWinApi.dll" "${WINPREBUILT}/AdbWinUsbApi.dll"
                "$<TARGET_FILE_DIR:fastboot>"
        COMMENT "Copying AdbWinApi.dll / AdbWinUsbApi.dll next to fastboot.exe")
endif()
target_compile_options(fastboot_static PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

install(TARGETS fastboot RUNTIME DESTINATION bin)

# ---------------------------------------------------- self-test (no device)
# Checks suggestion ranking, i18n table integrity (format specifiers must
# match English in every language or StringPrintf would trip at runtime) and
# help-row well-formedness. Run in CI.
add_executable(fastboot_selftest src/fb_selftest.cpp)
target_link_libraries(fastboot_selftest PRIVATE fastboot_glue)

# ---------------------------------------------------------------- fs tools
if(FASTBOOT_BUILD_FS_TOOLS)
    include(cmake/fs_tools.cmake)
endif()
