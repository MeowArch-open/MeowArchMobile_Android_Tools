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

#if defined(__GNUC__) && !defined(__clang__)
#ifndef __builtin_available
#define __builtin_available(...) true
#endif
#endif
