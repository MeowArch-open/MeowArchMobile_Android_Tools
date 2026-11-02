// MeowArchMobile adb standalone build — platform_tools_version stub.
//
// AOSP's build stamps the adb binary with the platform-tools version and the
// build number (soong genrule "platform_tools_version" + libbuildversion);
// neither exists in a standalone build. Keep the same value the fastboot
// package reports, so `adb --version` and `fastboot --version` agree — they
// ship from the same pinned AOSP snapshot.
#pragma once

#define PLATFORM_TOOLS_VERSION "36.0.0"
