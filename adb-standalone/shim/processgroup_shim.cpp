// MeowArchMobile adbd standalone build — processgroup shim implementation.
// See the header for why this exists.
#include <processgroup/processgroup.h>

bool CgroupGetAttributePathForProcess(std::string_view, uid_t, pid_t, std::string&) {
    // No cgroup hierarchy management on a glibc host: report the attribute as
    // unavailable. is_process_in_freezer() in jdwp_service.cpp then reports
    // "not frozen", which is the correct answer for a plain Linux host.
    return false;
}
