/*
 * compat.h — force-included into every TU on GCC builds (see CMakeLists).
 *
 * Clang on Linux resolves `__builtin_available(android N, *)` to true (the
 * `*` else-branch means "available on everything else"), which is how the
 * AOSP libbase/liblog sources reach their android-30 code paths here. GCC has
 * no __builtin_available at all and fails to parse `android 30` as an
 * expression, so we provide an equivalent macro. The android-30 branch is the
 * one that is compiled and linked in this standalone build either way (the
 * __android_log_* symbols it uses are defined in the liblog TUs we build), so
 * forcing it true matches the clang behavior we ship.
 */
#pragma once

#include <cstdint>   /* uint16_t/uint64_t — see the note below */
#include <stdint.h>

#if defined(__GNUC__) && !defined(__clang__)
#ifndef __builtin_available
#define __builtin_available(...) true
#endif
#endif

/*
 * _Nonnull / _Nullable are clang keywords (a nullability annotation); GCC does
 * not define them, so `extern const char* _Nonnull x;` — which adb_mdns.h and
 * adb_wifi.cpp use — is a syntax error there. They carry no runtime meaning,
 * so dropping them for GCC loses only a diagnostic clang would have given.
 */
#if !defined(__clang__)
#ifndef _Nonnull
#define _Nonnull
#endif
#ifndef _Nullable
#define _Nullable
#endif
#ifndef _Null_unspecified
#define _Null_unspecified
#endif
#endif

/*
 * Fixed-width integer types, force-included.
 *
 * Several AOSP headers use uint16_t/uint64_t without including <cstdint>:
 * adb_mdns.h (patched in 0002, since it is our own declaration there) and
 * libziparchive's zip_writer.h, which the Linux build only gets away with
 * because libc++ pulls <cstdint> in transitively through <string>. libstdc++
 * (which mingw GCC uses — there is no clang cross compiler for Windows here)
 * does not, so zip_writer.h fails with "'uint16_t' is not a type name".
 *
 * Supplying it here keeps the fix in one place and out of the upstream tree:
 * this header is force-included into every TU of a GCC build.
 */
