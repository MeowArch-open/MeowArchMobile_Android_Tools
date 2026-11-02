// Minimal config.h stand-in for AOSP's protobuf 21.12 cmake build.
// Upstream generates this from configure; the CMake build never needed it
// except that stubs/common.cc includes it unconditionally.
#define HAVE_PTHREAD 1
#define HAVE_ZLIB 0
