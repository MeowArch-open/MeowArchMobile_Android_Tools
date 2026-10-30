// MeowArchMobile fastboot standalone build — android::build::GetBuildNumber().
// Mirrors build/soong/cc/libbuildversion but returns a fixed, honest string:
// there is no soong build number to inject in a standalone build.
#include <build/version.h>

namespace android {
namespace build {

std::string GetBuildNumber() {
    return "meowarch-fastboot";
}

}  // namespace build
}  // namespace android
