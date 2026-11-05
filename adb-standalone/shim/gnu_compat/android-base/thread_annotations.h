/*
 * shim/gnu_compat/android-base/thread_annotations.h
 *
 * GCC-neutral replacement for libbase's thread_annotations.h, put on the
 * include path AHEAD of system/libbase/include for GCC builds (see the
 * mingw branch in CMakeLists.txt; the Linux build uses clang and gets the
 * real header).
 *
 * Why: every macro here expands to `__attribute__((capability(x)))` and
 * friends. Those are clang-only thread-safety-analysis attributes. Clang
 * accepts them in any position; GCC parses `__attribute__((x))` as a
 * statement attribute inside a function *definition* and errors with
 * "attributes are not allowed in a function definition" the moment a source
 * writes `static void f(int) EXCLUDES(mutex)` — which adb_listeners.cpp,
 * transport.cpp and fdevent.h all do. The Linux build never sees this because
 * it compiles with clang.
 *
 * So every macro becomes empty. Nothing is lost: these annotations only feed
 * clang's static thread-safety analysis, they generate no code, and a GCC
 * build was never going to run that analysis. ScopedLockAssertion is kept
 * (minus its annotations) because libbase's own logging/property code
 * instantiates it, so dropping the class would break the link.
 *
 * The macro set is the complete one from the upstream header — a macro left
 * undefined would turn into an unknown identifier at every use site, which is
 * worse than the annotation itself.
 */
#pragma once

#include <mutex>

#define THREAD_ANNOTATION_ATTRIBUTE__(x)

#define CAPABILITY(x)
#define SHARED_CAPABILITY(...)
#define SCOPED_CAPABILITY

#define GUARDED_BY(x)
#define PT_GUARDED_BY(x)

#define ACQUIRED_BEFORE(...)
#define ACQUIRED_AFTER(...)

#define REQUIRES(...)
#define REQUIRES_SHARED(...)
#define EXCLUSIVE_LOCKS_REQUIRED(...)
#define SHARED_LOCKS_REQUIRED(...)

#define LOCKS_EXCLUDED(...)
#define EXCLUDES(...)

#define ACQUIRE(...)
#define ACQUIRE_SHARED(...)

#define RELEASE(...)
#define RELEASE_SHARED(...)

#define TRY_ACQUIRE(...)
#define TRY_ACQUIRE_SHARED(...)

#define ASSERT_CAPABILITY(x)
#define ASSERT_SHARED_CAPABILITY(x)
#define RETURN_CAPABILITY(x)

#define EXCLUSIVE_LOCK_FUNCTION(...)
#define EXCLUSIVE_TRYLOCK_FUNCTION(...)
#define SHARED_LOCK_FUNCTION(...)
#define SHARED_TRYLOCK_FUNCTION(...)
#define UNLOCK_FUNCTION(...)

#define SCOPED_LOCKABLE
#define LOCK_RETURNED(x)
#define NO_THREAD_SAFETY_ANALYSIS

// Same class, same semantics at runtime (there are none: the annotations are
// compile-time only), minus the two attributes GCC cannot parse.
namespace android {
namespace base {

class ScopedLockAssertion {
 public:
    explicit ScopedLockAssertion(std::mutex&) {}
    ~ScopedLockAssertion() {}
};

}  // namespace base
}  // namespace android
