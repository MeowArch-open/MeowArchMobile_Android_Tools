// MeowArchMobile adb standalone build — fastdeploy API stubs.
//
// Quick Deploy (fastdeploy) is the "patch the APK on the device instead of
// pushing the whole file" install path. Upstream builds it from
// client/fastdeploy.cpp + client/fastdeploycallbacks.cpp, which need
// libandroidfw (frameworks/base — not in the AOSP components this project pins)
// to read AndroidManifest.xml out of the APK, plus the compiled-in agent APKs
// that a soong genrule generates from fastdeploy/deployagent.
//
// Rather than pull in frameworks/base for one optional install mode, the API
// is stubbed: extract_metadata() reports "this APK cannot be fast-deployed",
// which is exactly the branch adb already takes for every APK that lacks the
// required metadata. `adb install --fastdeploy` therefore prints the same
// "Fast Deploy is only compatible with devices of API version 24 or higher,
// ignoring." notice it prints for an older device, and falls back to the normal
// streamed/pushed install. Nothing is silently dropped.
#include <optional>

#include "fastdeploy.h"

void fastdeploy_set_agent_update_strategy(FastDeploy_AgentUpdateStrategy) {}

int get_device_api_level() {
    // 0 < kFastDeployMinApi (24): callers treat this as "this device cannot do
    // fastdeploy" and fall back. No device is ever queried — the transport work
    // for that would only support a path this build cannot run.
    return 0;
}

std::optional<com::android::fastdeploy::APKMetaData> extract_metadata(const char*) {
    return std::nullopt;
}

unique_fd install_patch(int, const char**) {
    return unique_fd(-1);
}

unique_fd apply_patch_on_device(const char*) {
    return unique_fd(-1);
}

int stream_patch(const char*, com::android::fastdeploy::APKMetaData, unique_fd) {
    return -1;
}
