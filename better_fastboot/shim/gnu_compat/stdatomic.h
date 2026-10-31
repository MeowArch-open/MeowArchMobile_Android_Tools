/*
 * stdatomic.h shim for GCC C++ builds.
 *
 * liblog's logger.h declares `atomic_int fd;` after including <stdatomic.h>.
 * Clang provides those C11 names in C++ mode; GCC's <stdatomic.h> is C-only
 * (the C++ macros/types simply do not exist), so the pinned snapshot's liblog
 * fails to compile with g++ ("atomic_int is not a type name").
 *
 * This shim is prepended to the include path of log_static ONLY when the
 * compiler is GNU, and maps the handful of C11 atomic names liblog's headers
 * actually use onto <atomic>. The translation units we compile (logger_read/
 * logger_write and friends) only need the *type* — the atomic_load/
 * atomic_exchange call sites live in logd_reader/logd_writer/pmsg_reader,
 * which this standalone build does not compile.
 */
#pragma once

#if defined(__cplusplus)

#include <atomic>

typedef std::atomic<int> atomic_int;
typedef std::atomic<unsigned int> atomic_uint;
typedef std::atomic<long> atomic_long;
typedef std::atomic<int32_t> atomic_int_least32_t;
typedef std::atomic<uint32_t> atomic_uint_least32_t;

using std::memory_order;
using std::memory_order_relaxed;
using std::memory_order_consume;
using std::memory_order_acquire;
using std::memory_order_release;
using std::memory_order_acq_rel;
using std::memory_order_seq_cst;

// Function forms used by logd_reader/logd_writer/pmsg_reader (not compiled in
// this project, but mapped for completeness so the shim is safe if sources
// are added later).
template <typename T>
static inline T atomic_load(const std::atomic<T>* a) {
    return a->load();
}
template <typename T>
static inline T atomic_exchange(std::atomic<T>* a, T desired) {
    return a->exchange(desired);
}
template <typename T>
static inline T atomic_exchange_explicit(std::atomic<T>* a, T desired, memory_order mo) {
    return a->exchange(desired, mo);
}
template <typename T>
static inline T atomic_fetch_add_explicit(std::atomic<T>* a, T arg, memory_order mo) {
    return a->fetch_add(arg, mo);
}

#else  /* C: fall through to the real header */
#include_next <stdatomic.h>
#endif
