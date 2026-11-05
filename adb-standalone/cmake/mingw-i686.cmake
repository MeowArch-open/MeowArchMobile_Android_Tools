# mingw-i686.cmake — cross toolchain for the Windows adb build.
#
# Google's official Windows adb.exe is built by soong with a 32-bit
# mingw-w64 GCC toolchain (soong cc/config/x86_windows_host.go: -DUSE_MINGW,
# windowsGccTriple i686) and linked against the prebuilt AdbWinApi.dll whose
# soong module pins compile_multilib "32" (development/host/windows/Android.bp).
# MSVC is not an option: the upstream sources are POSIX-flavored (getopt_long,
# flock, unistd.h) and Google does not compile them with MSVC either.
# This toolchain reproduces the official one; win-prebuilt/usb/ carries the
# same prebuilt DLLs + import lib soong links.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

set(TOOLCHAIN_PREFIX i686-w64-mingw32 CACHE STRING "mingw-w64 target triple")

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)
set(CMAKE_AR           ${TOOLCHAIN_PREFIX}-ar)
set(CMAKE_STRIP        ${TOOLCHAIN_PREFIX}-strip)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# winpthreads backs std::thread/std::mutex under mingw; find_package(Threads)
# cannot run its try_compile probes meaningfully in a cross build, so seed the
# result the way soong's -lpthread does.
set(CMAKE_THREAD_LIBS_INIT "-lpthread")
set(CMAKE_HAVE_THREADS_LIBRARY 1)
set(CMAKE_USE_PTHREADS_INIT 1)
set(THREADS_PREFER_PTHREAD_FLAG OFF)

# Self-contained exe: static libstdc++/libgcc/winpthread (Google's platform-
# tools fastboot.exe ships the same way — no runtime redistributable needed).
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
