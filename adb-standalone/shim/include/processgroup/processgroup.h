// MeowArchMobile adbd standalone build — processgroup shim.
//
// daemon/jdwp_service.cpp includes <processgroup/processgroup.h> (unguarded,
// even on host builds) and calls only CgroupGetAttributePathForProcess() to
// find the freezer cgroup path of a pid. On Android that comes from
// libprocessgroup; dragging that in (libjsoncpp, libcgrouprc, ...) is far
// more trouble than it is worth for a glibc build. Provide the one symbol —
// at global namespace, matching upstream — and let the caller treat "no
// path" as "not frozen", which is exactly what a host adbd wants.
#pragma once

#include <string>
#include <string_view>
#include <sys/types.h>

bool CgroupGetAttributePathForProcess(std::string_view attr_name, uid_t uid, pid_t pid,
                                      std::string& path);
