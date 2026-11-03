# MeowArchMobile adbd — standalone build
# ============================================================
# Builds AOSP adbd (packages/modules/adb daemon) as a native glibc binary,
# for x86_64 (shakedown) and aarch64 (Arch Linux ARM target).
#
# The AOSP tree is referenced in place (no source copying): pass
#   -DAOSP_ROOT=/path/to/aosp-adbd
# Defaults to ../aosp-adbd relative to this file.
#
# Local, vendored, or shim sources live under sources/ and shim/.
#
# Targets:
#   adbd        — the daemon binary
#
# Optional knobs:
#   ADDB_WITH_MDNS=OFF (default) — mdns requires libmdnssd (android-only path
#       in the source; the daemon never calls setup_mdns() off-Android)
#
# Why certain things are the way they are (see shim/ and patches/):
#   * processgroup shim — jdwp_service.cpp includes <processgroup/processgroup.h>
#     unconditionally; we provide the one function it uses (returns false).
#   * build_version / platform_tools_version — soong stamps these at build
#     time; we ship static equivalents.
#   * boringssl built with OPENSSL_NO_ASM=ON: its vendored CMakeLists.txt
#     compiles every architecture's assembly when ASM is enabled, which
#     breaks cross builds. Pure-C fallback paths are fine for adbd.
#   * protobuf-lite comes from the system (protoc + libprotobuf-lite).
#     AOSP pins protobuf 36.x here; keep the system version in sync.
#     Note: the protos MUST be generated with --cpp_out=lite (as soong does);
#     a full-runtime generation drags in google/protobuf/descriptor.h, which
#     pulls <absl/log/log.h> whose LOG()/VLOG() macros then collide with
#     adb_trace.h + libbase logging.

cmake_minimum_required(VERSION 3.20)
project(adbd-standalone LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# libc++ is a hard requirement, not a taste choice: adb's types.h
# static_asserts that Block etc. are standard-layout, and libstdc++'s
# std::unique_ptr is not (libc++'s is). Arch Linux ARM ships a matching
# libc++, so the target gets it from the distro.
option(ADBD_USE_LIBCXX "link against libc++ (required by adb's standard-layout asserts)" ON)
if(ADBD_USE_LIBCXX)
    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ---------------------------------------------------------------- paths
get_filename_component(AOSP_ROOT_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../aosp-adbd" ABSOLUTE)
set(AOSP_ROOT "${AOSP_ROOT_DEFAULT}" CACHE PATH "AOSP tree to pull adb/libbase/libcutils sources from")

set(ADB     "${AOSP_ROOT}/packages/modules/adb")
set(SYSCORE "${AOSP_ROOT}/system/core")
set(LIBBASE "${AOSP_ROOT}/system/libbase")
set(LIBLOG  "${AOSP_ROOT}/system/logging/liblog")
set(BSSL    "${AOSP_ROOT}/external/boringssl")

if(NOT EXISTS "${ADB}/Android.bp")
    message(FATAL_ERROR "AOSP_ROOT=${AOSP_ROOT} does not look like an AOSP tree (missing packages/modules/adb/Android.bp)")
endif()

message(STATUS "AOSP tree: ${AOSP_ROOT}")

# --------------------------------------------------------------- shims
# Source patches for the AOSP tree live in patches/ and are applied to the
# tree in place. Idempotent: rc=1 means "reversed or previously applied"
# (patch -N already skips those), which is fine; anything else is fatal.
# --fuzz=0 is load-bearing: a hunk that only matches with fuzz can be
# re-applied at a shifted offset on every configure, duplicating its block
# (daemon/main.cpp once carried 22 copies of the ADBD_NO_AUTH block that
# way). With fuzz off, a bad hunk fails loudly instead. scripts/
# check-patches.py verifies every patch applies exactly and is idempotent.
# -r /dev/null: when patch -N skips an already-applied hunk it would
# otherwise litter the AOSP tree with .rej files on every re-configure.
foreach(_patch
        0001-fdevent.h-include-vector-explicitly.patch
        0002-main.cpp-ADBD_NO_AUTH-env-on-host.patch
        0003-main.cpp-enable-usb-ffs-on-glibc.patch)
    execute_process(
        COMMAND patch -N -s --fuzz=0 --no-backup-if-mismatch -r /dev/null -d "${ADB}" -p1 -i
                "${CMAKE_CURRENT_SOURCE_DIR}/patches/${_patch}"
        RESULT_VARIABLE _patch_rc ERROR_QUIET)
    if(NOT _patch_rc EQUAL 0 AND NOT _patch_rc EQUAL 1)
        message(FATAL_ERROR "Failed to apply ${_patch} to the AOSP tree (rc=${_patch_rc})")
    endif()
endforeach()

add_library(meowarch_shim STATIC
    shim/processgroup_shim.cpp
    shim/build_version.cpp
    shim/system_properties_shim.cpp
)
target_include_directories(meowarch_shim PUBLIC
    shim/include
    # <build/version.h> — vendored copy of build/soong/cc/libbuildversion's
    # public header (the soong tree is not a hard checkout requirement).
)

# ------------------------------------------------------------ boringssl
# Built from the AOSP checkout's own CMakeLists. ASM disabled: its generated
# file list mixes every target arch and only the generic C paths matter here.
#
# The generated CMakeLists pins CMAKE_CXX_STANDARD 14, but openssl/span.h
# needs C++17 (std::is_convertible_v, std::string_view). Rather than patching
# the AOSP tree, stage a shadow copy: symlink the sources, rewrite the two
# standard lines, and add_subdirectory the shadow.
set(BSSL_SRC "${BSSL}/src")
set(BSSL_STAGE "${CMAKE_BINARY_DIR}/boringssl-stage")
file(MAKE_DIRECTORY "${BSSL_STAGE}")
if(NOT EXISTS "${BSSL_STAGE}/src")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink "${BSSL_SRC}" "${BSSL_STAGE}/src")
endif()
file(READ "${BSSL}/CMakeLists.txt" BSSL_CML)
string(REPLACE "set(CMAKE_CXX_STANDARD 14)" "set(CMAKE_CXX_STANDARD 17)" BSSL_CML "${BSSL_CML}")
file(WRITE "${BSSL_STAGE}/CMakeLists.txt" "${BSSL_CML}")

set(OPENSSL_NO_ASM ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(${BSSL_STAGE} ${CMAKE_BINARY_DIR}/boringssl EXCLUDE_FROM_ALL)
# Targets: crypto, ssl. Their include dirs are PUBLIC (src/include), so
# <openssl/...> resolves for everything that links them.

# ---------------------------------------------------------- system deps
find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(BROTLI IMPORTED_TARGET libbrotlienc libbrotlidec)
    pkg_check_modules(ZSTD IMPORTED_TARGET libzstd)
    pkg_check_modules(LZ4 IMPORTED_TARGET liblz4)
    pkg_check_modules(LIBCAP IMPORTED_TARGET libcap)
endif()
if(NOT BROTLI_FOUND OR NOT ZSTD_FOUND OR NOT LZ4_FOUND OR NOT LIBCAP_FOUND)
    message(FATAL_ERROR "system brotli/zstd/lz4/libcap not found via pkg-config")
endif()

# -------------------------------------------------- protobuf (from AOSP)
# NOT the system protobuf: distro libprotobuf is compiled against libstdc++,
# and our code links libc++ (required by adb's Block standard-layout
# assertions), so the symbols don't match. AOSP's external/protobuf (21.12)
# predates the abseil coupling, builds clean with any stdlib, and gives us a
# protoc whose generated code matches the runtime library exactly.
set(PROTOBUF_ROOT "${AOSP_ROOT}/external/protobuf")
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)

# Cross builds can't run an aarch64 protoc: point HOST_PROTOC at a protoc from
# a native build of this same project (its output is identical for both
# architectures) and skip building our own.
set(HOST_PROTOC "" CACHE FILEPATH "prebuilt host protoc (required for cross builds)")
if(HOST_PROTOC)
    set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
    set(PROTOC_EXE "${HOST_PROTOC}")
    set(PROTOBUF_LITE_TARGET libprotobuf-lite)
    set(PROTOBUF_LITE_LIB libprotobuf-lite)
    if(NOT EXISTS "${HOST_PROTOC}")
        message(FATAL_ERROR "HOST_PROTOC=${HOST_PROTOC} does not exist — build the native tree first")
    endif()
endif()

add_subdirectory(${PROTOBUF_ROOT} ${CMAKE_BINARY_DIR}/protobuf EXCLUDE_FROM_ALL)
# AOSP's protobuf 21.12: stubs/common.cc includes "config.h" unconditionally
# (autotools generates it; the CMake build here never does). Provide a
# minimal one on the include path of protobuf's own targets.
foreach(pb_target libprotobuf libprotobuf-lite)
    set_property(TARGET ${pb_target} APPEND PROPERTY INCLUDE_DIRECTORIES
                 "${CMAKE_CURRENT_SOURCE_DIR}/cmake/protobuf-compat")
endforeach()
if(NOT HOST_PROTOC)
    foreach(pb_target protoc libprotoc)
        set_property(TARGET ${pb_target} APPEND PROPERTY INCLUDE_DIRECTORIES
                     "${CMAKE_CURRENT_SOURCE_DIR}/cmake/protobuf-compat")
    endforeach()
endif()
if(NOT HOST_PROTOC)
    set(PROTOC_EXE $<TARGET_FILE:protoc>)
endif()
set(PROTOBUF_LITE_TARGET libprotobuf-lite)
set(PROTOBUF_LITE_LIB libprotobuf-lite)

set(PROTO_GEN_DIR "${CMAKE_BINARY_DIR}/protogen")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

# adb protos (lite)
set(ADB_PROTOS adb_known_hosts.proto key_type.proto pairing.proto app_processes.proto)
set(ADB_PROTO_SRCS "")
set(ADB_PROTO_HDRS "")
foreach(p ${ADB_PROTOS})
    get_filename_component(p_name "${p}" NAME_WE)
    set(out_cc "${PROTO_GEN_DIR}/${p_name}.pb.cc")
    set(out_h  "${PROTO_GEN_DIR}/${p_name}.pb.h")
    add_custom_command(
        OUTPUT "${out_cc}" "${out_h}"
        COMMAND ${PROTOC_EXE}
                --proto_path=${ADB}/proto
                --cpp_out=lite:${PROTO_GEN_DIR}
                ${ADB}/proto/${p}
        DEPENDS ${ADB}/proto/${p}
        COMMENT "protoc ${p}"
        VERBATIM)
    list(APPEND ADB_PROTO_SRCS "${out_cc}")
    list(APPEND ADB_PROTO_HDRS "${out_h}")
endforeach()

add_library(adb_protos STATIC ${ADB_PROTO_SRCS} ${ADB_PROTO_HDRS})
if(NOT HOST_PROTOC)
    add_dependencies(adb_protos protoc)
endif()
target_include_directories(adb_protos PUBLIC "${PROTO_GEN_DIR}")
# The generated headers must be read against the SAME protobuf headers that
# built the runtime (AOSP 21.12, from ${PROTOBUF_ROOT}/src). Without this the
# compiler silently picks /usr/include/google/protobuf (system protobuf 36.x),
# whose port_def.inc no longer defines PROTOBUF_VERSION — every version guard
# then fails with a misleading "generated by a newer protoc" error, and
# PROTOBUF_NAMESPACE_OPEN is undefined too.
target_include_directories(adb_protos SYSTEM PUBLIC "${PROTOBUF_ROOT}/src")
target_link_libraries(adb_protos PUBLIC ${PROTOBUF_LITE_LIB})

# --------------------------------------------------------------- liblog
# Host subset of system/logging/liblog (see its Android.bp: liblog_sources +
# not_windows event_tag_map.cpp).
add_library(log_static STATIC
    ${LIBLOG}/log_event_list.cpp
    ${LIBLOG}/log_event_write.cpp
    ${LIBLOG}/logger_name.cpp
    ${LIBLOG}/logger_read.cpp
    ${LIBLOG}/logger_write.cpp
    ${LIBLOG}/logprint.cpp
    ${LIBLOG}/properties.cpp
    ${LIBLOG}/event_tag_map.cpp
)
target_include_directories(log_static PUBLIC
    ${LIBLOG}/include
    ${LIBBASE}/include            # liblog headers include android-base in places
    ${SYSCORE}/libcutils/include  # <cutils/list.h> (logprint.cpp)
    ${SYSCORE}/libcutils/include/private  # <private/android_filesystem_config.h> (logger_write.cpp)
    ${AOSP_ROOT}/system/core/libutils/include  # <utils/FastStrcmp.h> (event_tag_map.cpp)
    ${AOSP_ROOT}/system/core/libsystem/include # <system/graphics.h> via utils/ThreadDefs.h
    ${AOSP_ROOT}/system/core/include           # <cutils/*.h> legacy dir (logprint/list.h chain)
)
target_compile_definitions(log_static PRIVATE
    LIBLOG_LOG_TAG=1006
    SNET_EVENT_LOG_TAG=1397638484
    ANDROID_DEBUGGABLE=0
    _GNU_SOURCE
)

# -------------------------------------------------------------- libbase
# Full host subset from system/libbase's Android.bp (linux target).
add_library(base_static STATIC
    ${LIBBASE}/abi_compatibility.cpp
    ${LIBBASE}/chrono_utils.cpp
    ${LIBBASE}/cmsg.cpp
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
    ${LIBBASE}/errors_unix.cpp
)
target_include_directories(base_static PUBLIC
    ${LIBBASE}/include
    ${AOSP_ROOT}/external/fmtlib/include
)
target_link_libraries(base_static PUBLIC log_static Threads::Threads)

# -------------------------------------------------------- libcutils bits
# Only what adbd actually pulls in on a glibc host:
#   libcutils_sockets (sockets.cpp + unix socket impls)  — cutils/sockets.h users
#   fs_config.cpp                                        — libadbd_fs
add_library(cutils_sockets STATIC
    ${SYSCORE}/libcutils/sockets.cpp
    ${SYSCORE}/libcutils/socket_inaddr_any_server_unix.cpp
    ${SYSCORE}/libcutils/socket_local_client_unix.cpp
    ${SYSCORE}/libcutils/socket_local_server_unix.cpp
    ${SYSCORE}/libcutils/socket_network_client_unix.cpp
    ${SYSCORE}/libcutils/sockets_unix.cpp
)
target_include_directories(cutils_sockets PUBLIC
    ${SYSCORE}/libcutils/include
    ${SYSCORE}/libcutils            # private "android_get_control_env.h"
)
target_link_libraries(cutils_sockets PUBLIC log_static)
target_compile_definitions(cutils_sockets PRIVATE _GNU_SOURCE)

add_library(cutils_fsconfig STATIC
    ${SYSCORE}/libcutils/fs_config.cpp
)
target_include_directories(cutils_fsconfig PUBLIC
    ${SYSCORE}/libcutils/include
    ${SYSCORE}/libcutils/include/private
    ${LIBBASE}/include
)
target_link_libraries(cutils_fsconfig PUBLIC log_static base_static)
target_compile_definitions(cutils_fsconfig PRIVATE _GNU_SOURCE)

# ------------------------------------------------------ libcrypto_utils
add_library(crypto_utils STATIC
    ${SYSCORE}/libcrypto_utils/android_pubkey.cpp
)
target_include_directories(crypto_utils PUBLIC
    ${SYSCORE}/libcrypto_utils/include
)
target_link_libraries(crypto_utils PUBLIC crypto base_static)

# ------------------------------------------------------------ libasyncio
add_library(asyncio STATIC
    ${SYSCORE}/libasyncio/AsyncIO.cpp
)
target_include_directories(asyncio PUBLIC ${SYSCORE}/libasyncio/include)

# --------------------------------------------------------- diagnose_usb
add_library(diagnose_usb STATIC
    ${SYSCORE}/diagnose_usb/diagnose_usb.cpp
)
target_include_directories(diagnose_usb PUBLIC ${SYSCORE}/diagnose_usb/include)
target_link_libraries(diagnose_usb PUBLIC base_static)

# ------------------------------------------------------------ libadbd_fs
add_library(adbd_fs STATIC
    ${ADB}/libs/libadbd_fs/adbd_fs.cpp
)
target_include_directories(adbd_fs PUBLIC ${ADB}/libs/libadbd_fs/include)
target_link_libraries(adbd_fs PUBLIC base_static cutils_fsconfig log_static)

# ----------------------------------------------------------- libadbd_auth
add_library(adbd_auth STATIC sources/adbd_auth/adbd_auth.cpp)
target_include_directories(adbd_auth PUBLIC sources/adbd_auth/include)
target_link_libraries(adbd_auth PUBLIC base_static cutils_sockets log_static)

# ------------------------------------------------------------ adb core
# Common include dirs for everything adb: the adb root, its sysdeps/, proto
# headers, and the pairing/crypto/tls/adbconnection public headers.
set(ADB_COMMON_INCS
    ${ADB}
    ${ADB}/include
    ${ADB}/crypto/include
    ${ADB}/tls/include
    ${ADB}/pairing_auth/include
    ${ADB}/pairing_connection/include
    ${ADB}/libs/adbconnection/include
)

add_library(adb_core STATIC
    # libadb_srcs (Android.bp)
    ${ADB}/adb.cpp
    ${ADB}/adb_io.cpp
    ${ADB}/adb_listeners.cpp
    ${ADB}/adb_mdns.cpp
    ${ADB}/adb_trace.cpp
    ${ADB}/adb_unique_fd.cpp
    ${ADB}/adb_utils.cpp
    ${ADB}/apacket_reader.cpp
    ${ADB}/fdevent/fdevent.cpp
    ${ADB}/fdevent/fdevent_epoll.cpp
    ${ADB}/services.cpp
    ${ADB}/sockets.cpp
    ${ADB}/socket_spec.cpp
    ${ADB}/sysdeps/env.cpp
    ${ADB}/sysdeps/errno.cpp
    ${ADB}/sysdeps_unix.cpp
    ${ADB}/sysdeps/posix/network.cpp
    ${ADB}/transport.cpp
    ${ADB}/transport_fd.cpp
    ${ADB}/types.cpp
    # libadb_sysdeps (sysdeps/env.cpp already listed above — soong compiles it
    # twice, once per library; one copy is enough here)
    # crypto / tls / pairing
    ${ADB}/crypto/key.cpp
    ${ADB}/crypto/rsa_2048_key.cpp
    ${ADB}/crypto/x509_generator.cpp
    ${ADB}/tls/adb_ca_list.cpp
    ${ADB}/tls/tls_connection.cpp
    ${ADB}/pairing_auth/aes_128_gcm.cpp
    ${ADB}/pairing_auth/pairing_auth.cpp
    ${ADB}/pairing_connection/pairing_connection.cpp
    # adbconnection server (jdwp transport)
    ${ADB}/libs/adbconnection/adbconnection_server.cpp
    ${ADB}/libs/adbconnection/common.cpp
)
target_include_directories(adb_core PUBLIC ${ADB_COMMON_INCS})
# adb.cpp includes <build/version.h> (vendored under shim/include) and
# <platform_tools_version.h> (our shim header).
target_include_directories(adb_core SYSTEM PUBLIC
    shim/include
)
target_compile_definitions(adb_core PUBLIC
    ADB_HOST=0
    ANDROID_BASE_UNIQUE_FD_DISABLE_IMPLICIT_CONVERSION=1
    _GNU_SOURCE
    # sysdeps/uio.h uses IOV_MAX without including <limits.h>; on glibc that
    # macro lives behind __IOV_MAX (bits/uio_lim.h, pulled in by <sys/uio.h>).
    IOV_MAX=__IOV_MAX
)
target_link_libraries(adb_core PUBLIC
    adb_protos
    adbd_auth
    base_static
    log_static
    cutils_sockets
    crypto_utils
    crypto
    ssl
    diagnose_usb
    Threads::Threads
    ${CMAKE_DL_LIBS}
)
# b64_pton (auth.cpp) — glibc libresolv; forkpty (shell_service) — libutil
target_link_libraries(adb_core PUBLIC resolv util)

# ----------------------------------------------------------- adbd daemon
add_executable(adbd
    # libadbd_core daemon additions
    ${ADB}/daemon/adb_wifi.cpp
    ${ADB}/daemon/auth.cpp
    ${ADB}/daemon/jdwp_service.cpp
    ${ADB}/daemon/logging.cpp
    ${ADB}/daemon/transport_socket_server.cpp
    # USB (FunctionFS) transport. soong builds these for Android only, but the
    # sources are plain Linux (kernel AIO + functionfs); patch 0003 unguards
    # the usb_init() call site in main.cpp, and the property shim (meowarch_shim)
    # covers PropertyMonitor's bionic-only __system_property_wait.
    ${ADB}/daemon/usb.cpp
    ${ADB}/daemon/usb_ffs.cpp
    ${ADB}/daemon/property_monitor.cpp
    # libadbd_services (host set; android-only sources excluded by their
    # own #if defined(__ANDROID__) guards in soong's target.android srcs)
    ${ADB}/daemon/file_sync_service.cpp
    ${ADB}/daemon/services.cpp
    ${ADB}/daemon/shell_service.cpp
    ${ADB}/daemon/tradeinmode.cpp
    ${ADB}/shell_service_protocol.cpp
    # adbd binary
    ${ADB}/daemon/main.cpp
)
target_compile_definitions(adbd PRIVATE _GNU_SOURCE)
target_link_libraries(adbd PRIVATE
    adb_core
    meowarch_shim
    adbd_fs
    asyncio
    adb_protos
    base_static
    log_static
    cutils_sockets
    crypto
    ssl
    diagnose_usb
    PkgConfig::BROTLI
    PkgConfig::ZSTD
    PkgConfig::LZ4
    PkgConfig::LIBCAP
    Threads::Threads
    ${PROTOBUF_LITE_LIB}
)

# Warnings: match adb_defaults' -Wall -Wextra but keep -Werror off — a
# standalone toolchain (gcc 16 / clang 23) trips on warnings soong never saw.
target_compile_options(adb_core PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_compile_options(adbd     PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

# usb_ffs.cpp builds descriptor tables with designated initializers narrowed
# from uint32 (cpu_to_le32(...)) to __u8/__le16 fields. On bionic that folds
# to a constant expression; glibc's htole32 goes through an inline function,
# so clang flags it as a hard narrowing error. The values are small constants
# by construction — downgrade to a warning for this file only.
set_source_files_properties(${ADB}/daemon/usb_ffs.cpp PROPERTIES
    COMPILE_OPTIONS "-Wno-c++11-narrowing")

install(TARGETS adbd RUNTIME DESTINATION bin)
