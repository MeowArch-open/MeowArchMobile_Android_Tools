# MeowArchMobile adb — standalone build
# ============================================================
# Builds AOSP's host adb client (packages/modules/adb, the `adb` platform-tool)
# as a native glibc binary. This is the *client*, not the daemon: it is what
# runs on your Linux box and talks to a phone, exactly like platform-tools'
# `adb`, but compiled from the pinned AOSP sources instead of a prebuilt.
#
# The AOSP tree is referenced in place (no source copying):
#   -DAOSP_ROOT=/path/to/aosp-adbd      (default: ../aosp-adbd)
#
# What it does NOT build, and why:
#   * mDNS discovery (openscreen + mDNSResponder). soong builds those as
#     separate prebuilt libraries from the chromium.googlesource.com/openscreen
#     checkout and external/mdnsresponder; neither is part of the AOSP tree
#     this project pins, and openscreen is not fetchable as an AOSP component.
#     Instead `patches/0001-adb-host-ux.patch` compiles zero of the mDNS
#     sources, and the client falls back to `adb connect ip:port` /
#     `tcp:<ip>` discovery, which is what the transport layer already does
#     when mDNS is unavailable (ADB_MDNS=0 behaves identically upstream).
#   * fastdeploy / incremental install. They pull libandroidfw (frameworks/base
#     — not in this tree), incfs and liblz4 streaming; both are opt-in paths
#     (`adb install --fastdeploy`, `adb install --incremental`) that upstream
#     itself disables when the device or the host lacks support. Patching them
#     out keeps `adb install` on the fully-supported streamed/legacy path.
#
# Targets:
#   adb         — the host client binary
#
# Why certain things are the way they are (see shim/ and patches/):
#   * libc++ is a hard requirement: adb's types.h static_asserts that Block is
#     standard-layout, and libstdc++'s std::unique_ptr is not.
#   * boringssl is built from the AOSP checkout with ASM off (its vendored
#     CMakeLists mixes every arch's assembly, which breaks cross builds).
#   * protobuf-lite comes from AOSP's external/protobuf (21.12), not the
#     distro's: distro libprotobuf is a libstdc++ build and its symbols do not
#     match this libc++ binary.
#   * libusb: system libusb-1.0 is used (>= 1.0.29 for the superspeed-plus
#     descriptor API adb asks for). AOSP's fork is not needed — the extra
#     symbols it adds are only reached on Android.

cmake_minimum_required(VERSION 3.20)
project(adb-standalone LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ADB_USE_LIBCXX "link against libc++ (required by adb's standard-layout asserts)" ON)
if(ADB_USE_LIBCXX)
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
set(LIBZIP  "${AOSP_ROOT}/system/libziparchive")
set(BSSL    "${AOSP_ROOT}/external/boringssl")

if(NOT EXISTS "${ADB}/Android.bp")
    message(FATAL_ERROR "AOSP_ROOT=${AOSP_ROOT} does not look like an AOSP tree (missing packages/modules/adb/Android.bp)")
endif()
message(STATUS "AOSP tree: ${AOSP_ROOT}")

# --------------------------------------------------------------- patches
# Idempotent: rc=1 means "already applied" (patch -N skips it), which is fine.
#  0001  adb_mdns.h: declare mdns::ServiceInfo locally instead of pulling in
#        client/openscreen/mdns_service_info.h (openscreen is not an AOSP
#        component, so it is absent from the pinned tree), and declare the
#        transport entry point the stub provides. Field-for-field identical, so
#        nothing else has to change.
foreach(_patch
        0001-fdevent.h-include-vector-explicitly.patch
        0002-adb-host-ux.patch
        0003-adb_listeners.cpp-disable-nagle-on-accepted-sockets.patch)
    execute_process(
        COMMAND patch -N -s --fuzz=0 --no-backup-if-mismatch -r /dev/null -d "${ADB}" -p1 -i
                "${CMAKE_CURRENT_SOURCE_DIR}/patches/${_patch}"
        RESULT_VARIABLE _patch_rc ERROR_QUIET)
    if(NOT _patch_rc EQUAL 0 AND NOT _patch_rc EQUAL 1)
        message(FATAL_ERROR "Failed to apply ${_patch} to the AOSP tree (rc=${_patch_rc})")
    endif()
endforeach()

# --------------------------------------------------------------- shims
add_library(meowarch_shim STATIC
    shim/processgroup_shim.cpp
    shim/build_version.cpp
    shim/mdns/mdns_stub.cpp
    shim/fastdeploy/fastdeploy_stub.cpp
    shim/usb_host_stub.cpp
)
target_include_directories(meowarch_shim PUBLIC
    shim/include
    ${ADB}                      # "adb_mdns.h", "client/mdns_utils.h", "fastdeploy.h"
    "${CMAKE_BINARY_DIR}/protogen"
)
# fastdeploy_stub.cpp implements the API declared in client/fastdeploy.h, which
# lives one directory down (adb sources include each other as "fastdeploy.h").
target_include_directories(meowarch_shim PUBLIC ${ADB}/client)
target_link_libraries(meowarch_shim PUBLIC adb_protos base_static)
# adb_mdns.h only declares the host-side API (MdnsInfo, the connect helpers,
# what main.cpp and socket_spec.cpp call) under #if ADB_HOST.
target_compile_definitions(meowarch_shim PUBLIC ADB_HOST=1 _GNU_SOURCE)

# --------------------------------------------------------- MeowArchMobile UX
# Colors, the push/pull progress bar, the message catalog and the categorised
# help renderer. Same layout as better_fastboot's glue library: everything the
# AOSP tree is patched to call lives here, so the patch stays a one-line hook.
add_library(adb_glue STATIC
    src/fb_color.cpp
    src/adb_progress.cpp
    src/adb_i18n.cpp
    src/adb_i18n_en.cpp
    src/adb_i18n_zh_cn.cpp
    src/adb_i18n_zh_tw.cpp
    src/adb_i18n_ja.cpp
    src/adb_help.cpp
    src/adb_completion.cpp
    src/adb_suggest.cpp
    src/adbglue.cpp
)
target_include_directories(adb_glue PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(adb_glue PUBLIC base_static)
target_compile_options(adb_glue PRIVATE -Wall -Wextra -Wformat=2)
target_compile_options(adb_glue PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-Werror=format -Wno-format-nonliteral>)

# ------------------------------------------------- test-only binaries
# Drives the progress bar directly so scripts/test-progress-layout.py can check
# its layout deterministically: a real transfer over loopback finishes before
# enough frames are emitted to prove the bar's column is stable.
add_executable(test_progress_layout tests/test_progress_layout.cpp)
target_link_libraries(test_progress_layout PRIVATE adb_glue)
set_target_properties(test_progress_layout PROPERTIES EXCLUDE_FROM_ALL TRUE)

# The Did-you-want scorer. The CLI checks in scripts/test-adb.sh prove the hook
# fires; this one enumerates the scoring rules in BOTH directions (typos must
# resolve, foreign words must not, known commands must not suggest themselves),
# which is where a wrong-but-confident hint would come from.
add_executable(test_suggest tests/test_suggest.cpp)
target_link_libraries(test_suggest PRIVATE adb_glue)
set_target_properties(test_suggest PROPERTIES EXCLUDE_FROM_ALL TRUE)

# ------------------------------------------------------------ boringssl
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

# ---------------------------------------------------------- system deps
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LZ4 IMPORTED_TARGET liblz4)
    pkg_check_modules(ZSTD IMPORTED_TARGET libzstd)
    pkg_check_modules(BROTLI IMPORTED_TARGET libbrotlienc libbrotlidec)
    pkg_check_modules(LIBUSB IMPORTED_TARGET libusb-1.0)
endif()
if(NOT LZ4_FOUND OR NOT ZSTD_FOUND OR NOT BROTLI_FOUND OR NOT LIBUSB_FOUND)
    message(FATAL_ERROR "system lz4/zstd/brotli/libusb-1.0 not found via pkg-config")
endif()

# -------------------------------------------------- protobuf (from AOSP)
set(PROTOBUF_ROOT "${AOSP_ROOT}/external/protobuf")
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)

set(HOST_PROTOC "" CACHE FILEPATH "prebuilt host protoc (required for cross builds)")
if(HOST_PROTOC)
    set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
    set(PROTOC_EXE "${HOST_PROTOC}")
    if(NOT EXISTS "${HOST_PROTOC}")
        message(FATAL_ERROR "HOST_PROTOC=${HOST_PROTOC} does not exist — build the native tree first")
    endif()
endif()

add_subdirectory(${PROTOBUF_ROOT} ${CMAKE_BINARY_DIR}/protobuf EXCLUDE_FROM_ALL)
foreach(pb_target libprotobuf libprotobuf-lite)
    set_property(TARGET ${pb_target} APPEND PROPERTY INCLUDE_DIRECTORIES
                 "${CMAKE_CURRENT_SOURCE_DIR}/cmake/protobuf-compat")
endforeach()
if(NOT HOST_PROTOC)
    foreach(pb_target protoc libprotoc)
        set_property(TARGET ${pb_target} APPEND PROPERTY INCLUDE_DIRECTORIES
                     "${CMAKE_CURRENT_SOURCE_DIR}/cmake/protobuf-compat")
    endforeach()
    set(PROTOC_EXE $<TARGET_FILE:protoc>)
endif()
set(PROTOBUF_LITE_LIB libprotobuf-lite)

set(PROTO_GEN_DIR "${CMAKE_BINARY_DIR}/protogen")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

# adb protos. The client needs the full runtime, not lite: commandline.cpp
# prints AdbServerStatus / AppProcesses with google::protobuf::TextFormat.
# fastdeploy/proto/ApkEntry.proto is compiled too because fastdeploy.h includes
# fastdeploy/proto/ApkEntry.pb.h — the API is stubbed (see shim/fastdeploy/),
# but the type its signature uses must exist.
set(ADB_PROTOS adb_host.proto adb_known_hosts.proto app_processes.proto key_type.proto
              pairing.proto)
set(ADB_PROTO_FASTDEPLOY fastdeploy/proto/ApkEntry.proto)
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
                --cpp_out=${PROTO_GEN_DIR}
                ${ADB}/proto/${p}
        DEPENDS ${ADB}/proto/${p}
        COMMENT "protoc ${p}"
        VERBATIM)
    list(APPEND ADB_PROTO_SRCS "${out_cc}")
    list(APPEND ADB_PROTO_HDRS "${out_h}")
endforeach()

# fastdeploy's proto is addressed by its in-tree path (fastdeploy.h includes
# <fastdeploy/proto/ApkEntry.pb.h>), so protoc runs with the adb root as
# proto_path and the generated header lands at the same relative path.
foreach(p ${ADB_PROTO_FASTDEPLOY})
    # protoc writes <relative-input-path>.pb.cc, so strip the .proto suffix and
    # keep the directory (fastdeploy/proto/ApkEntry.pb.cc).
    get_filename_component(p_dir "${p}" DIRECTORY)
    get_filename_component(p_name "${p}" NAME_WE)
    set(out_cc "${PROTO_GEN_DIR}/${p_dir}/${p_name}.pb.cc")
    set(out_h  "${PROTO_GEN_DIR}/${p_dir}/${p_name}.pb.h")
    get_filename_component(out_dir "${out_cc}" DIRECTORY)
    file(MAKE_DIRECTORY "${out_dir}")
    add_custom_command(
        OUTPUT "${out_cc}" "${out_h}"
        COMMAND ${PROTOC_EXE}
                --proto_path=${ADB}
                --cpp_out=${PROTO_GEN_DIR}
                ${ADB}/${p}
        DEPENDS ${ADB}/${p}
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
# Generated headers must be read against the SAME protobuf headers that built
# the runtime; without ${PROTOBUF_ROOT}/src the compiler silently picks a
# system protobuf whose port_def.inc no longer defines PROTOBUF_VERSION.
target_include_directories(adb_protos SYSTEM PUBLIC "${PROTOBUF_ROOT}/src")
target_link_libraries(adb_protos PUBLIC libprotobuf)

# --------------------------------------------------------------- liblog
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
    ${LIBBASE}/include
    ${SYSCORE}/libcutils/include
    ${SYSCORE}/libcutils/include/private
    ${AOSP_ROOT}/system/core/libutils/include
    ${AOSP_ROOT}/system/core/libsystem/include
    ${AOSP_ROOT}/system/core/include
)
target_compile_definitions(log_static PRIVATE
    LIBLOG_LOG_TAG=1006
    SNET_EVENT_LOG_TAG=1397638484
    ANDROID_DEBUGGABLE=0
    _GNU_SOURCE
)

# -------------------------------------------------------------- libbase
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
    ${SYSCORE}/libcutils
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
add_library(crypto_utils STATIC ${SYSCORE}/libcrypto_utils/android_pubkey.cpp)
target_include_directories(crypto_utils PUBLIC ${SYSCORE}/libcrypto_utils/include)
target_link_libraries(crypto_utils PUBLIC crypto base_static)

# --------------------------------------------------------- diagnose_usb
add_library(diagnose_usb STATIC ${SYSCORE}/diagnose_usb/diagnose_usb.cpp)
target_include_directories(diagnose_usb PUBLIC ${SYSCORE}/diagnose_usb/include)
target_link_libraries(diagnose_usb PUBLIC base_static)

# ----------------------------------------------------------- libziparchive
# Needed by incremental_utils.cpp (both the incremental server and the client
# side of `adb install --incremental` open the APK as a zip).
add_library(ziparchive_static STATIC
    ${LIBZIP}/zip_archive.cc
    ${LIBZIP}/zip_archive_stream_entry.cc
    ${LIBZIP}/zip_cd_entry_map.cc
    ${LIBZIP}/zip_error.cpp
    ${LIBZIP}/zip_writer.cc
)
target_include_directories(ziparchive_static PUBLIC
    ${LIBZIP}/include
    ${LIBZIP}
    ${LIBZIP}/incfs_support/include
    ${AOSP_ROOT}/external/googletest/googletest/include
)
target_link_libraries(ziparchive_static PUBLIC base_static log_static ZLIB::ZLIB)
target_compile_definitions(ziparchive_static PUBLIC
    INCFS_SUPPORT_DISABLED=1
    ZLIB_CONST
    _FILE_OFFSET_BITS=64
)

# ------------------------------------------------------------ libadb_sysdeps
add_library(adb_sysdeps STATIC
    ${ADB}/sysdeps_unix.cpp
    ${ADB}/sysdeps/posix/network.cpp
)
target_include_directories(adb_sysdeps PUBLIC ${ADB})
target_link_libraries(adb_sysdeps PUBLIC base_static log_static)
target_compile_definitions(adb_sysdeps PUBLIC
    ADB_HOST=1
    _GNU_SOURCE
    IOV_MAX=__IOV_MAX
)

# ------------------------------------------------------------ adb core
# Common include dirs for everything adb: the adb root, its sysdeps/, proto
# headers, and the pairing/crypto/tls public headers.
set(ADB_COMMON_INCS
    ${ADB}
    ${ADB}/include
    ${ADB}/crypto/include
    ${ADB}/tls/include
    ${ADB}/pairing_auth/include
    ${ADB}/pairing_connection/include
)

add_library(adb_core STATIC
    # libadb_srcs
    ${ADB}/adb.cpp
    ${ADB}/adb_io.cpp
    ${ADB}/adb_listeners.cpp
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
    ${ADB}/transport.cpp
    ${ADB}/transport_fd.cpp
    ${ADB}/types.cpp
    # crypto / tls / pairing (shared with the daemon build)
    ${ADB}/crypto/key.cpp
    ${ADB}/crypto/rsa_2048_key.cpp
    ${ADB}/crypto/x509_generator.cpp
    ${ADB}/tls/adb_ca_list.cpp
    ${ADB}/tls/tls_connection.cpp
    ${ADB}/pairing_auth/aes_128_gcm.cpp
    ${ADB}/pairing_auth/pairing_auth.cpp
    ${ADB}/pairing_connection/pairing_connection.cpp
    # host-only client pieces that belong to the core (adb.cpp calls into them)
    ${ADB}/client/auth.cpp
    ${ADB}/client/adb_wifi.cpp
    ${ADB}/client/detach.cpp
    ${ADB}/client/usb_libusb.cpp
    ${ADB}/client/usb_libusb_device.cpp
    ${ADB}/client/usb_libusb_hotplug.cpp
    ${ADB}/client/usb_libusb_inhouse_hotplug.cpp
    ${ADB}/client/transport_usb.cpp
    ${ADB}/client/usb_linux.cpp
    ${ADB}/client/transport_emulator.cpp
    ${ADB}/client/pairing/pairing_client.cpp
)
target_include_directories(adb_core PUBLIC ${ADB_COMMON_INCS})
target_include_directories(adb_core SYSTEM PUBLIC
    shim/include
    ${AOSP_ROOT}/external/libusb   # <libusb/libusb.h> — adb includes it by that path
)
target_compile_definitions(adb_core PUBLIC
    ADB_HOST=1
    ANDROID_BASE_UNIQUE_FD_DISABLE_IMPLICIT_CONVERSION=1
    _GNU_SOURCE
    IOV_MAX=__IOV_MAX
)
target_link_libraries(adb_core PUBLIC
    adb_glue
    adb_protos
    adb_sysdeps
    base_static
    log_static
    cutils_sockets
    cutils_fsconfig
    ziparchive_static
    crypto_utils
    crypto
    ssl
    diagnose_usb
    PkgConfig::LZ4
    PkgConfig::ZSTD
    PkgConfig::BROTLI
    PkgConfig::LIBUSB
    Threads::Threads
    ${CMAKE_DL_LIBS}
)
# b64_pton — glibc libresolv; forkpty — libutil
target_link_libraries(adb_core PUBLIC resolv util)

# ------------------------------------------------------------ adb binary
add_executable(adb
    ${ADB}/client/main.cpp
    ${ADB}/client/adb_client.cpp
    ${ADB}/client/bugreport.cpp
    ${ADB}/client/commandline.cpp
    ${ADB}/client/console.cpp
    ${ADB}/client/file_sync_client.cpp
    ${ADB}/client/line_printer.cpp
    ${ADB}/client/adb_install.cpp
    ${ADB}/client/incremental.cpp
    ${ADB}/client/incremental_server.cpp
    ${ADB}/client/incremental_utils.cpp
    ${ADB}/shell_service_protocol.cpp
)
target_compile_definitions(adb PRIVATE ADB_HOST=1 _GNU_SOURCE)
target_link_libraries(adb PRIVATE
    adb_core
    meowarch_shim
    base_static
    log_static
    crypto
    ssl
    Threads::Threads
)
target_compile_options(adb_core PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_compile_options(adb      PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

install(TARGETS adb RUNTIME DESTINATION bin)
