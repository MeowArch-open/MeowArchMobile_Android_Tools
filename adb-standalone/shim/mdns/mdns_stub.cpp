// MeowArchMobile adb standalone build — mDNS API stubs.
//
// Upstream gets mDNS from two backends: openscreen (fetched from
// chromium.googlesource.com, not an AOSP component) and mDNSResponder
// (external/mdnsresponder, a client stub that needs the mdnsd *daemon* running
// and talking over a Unix socket). Neither is buildable in a standalone host
// package, so transport_mdns.cpp and mdnsresponder_client.cpp are not compiled
// and this file provides the same internal API.
//
// Behaviour: mDNS discovery is off. `adb mdns check` / `adb mdns services`
// say so, `tcp:host:port` and `adb connect` still work (they never touch
// mDNS), and `adb pair`/`adb connect` with a bare service name report it could
// not be resolved — what upstream does with ADB_MDNS=0. Nothing pretends to
// have found a device.
#include <optional>
#include <string>

#include "adb_mdns.h"
#include "client/mdns_utils.h"

namespace mdns {

// Upstream's test is `!getenv("ADB_MDNS") || strcmp(..., "0")`. There is no
// backend here, so the answer is always no; the variable cannot turn it on.
bool is_enabled() {
    return false;
}

}  // namespace mdns

void init_mdns_transport_discovery() {}

bool mdns_backend_available() {
    return false;
}

void mdns_cleanup() {}

std::string mdns_check() {
    return "mdns daemon version [unavailable: this build has no mDNS backend]";
}

std::string mdns_list_discovered_services() {
    return "no mDNS services (this build has no mDNS backend)";
}

std::optional<MdnsInfo> mdns_get_connect_service_info(const std::string&) {
    return std::nullopt;
}

std::optional<MdnsInfo> mdns_get_pairing_service_info(const std::string&) {
    return std::nullopt;
}

bool adb_secure_connect_by_service_name(const std::string&) {
    return false;
}

// adb.cpp asks which mDNS implementation is active for `adb server-status`.
bool using_bonjour() {
    return false;
}
