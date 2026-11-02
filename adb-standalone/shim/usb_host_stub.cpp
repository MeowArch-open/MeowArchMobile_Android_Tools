// MeowArchMobile adb standalone build — host usb_cleanup() stub.
//
// client/main.cpp registers atexit(adb_server_cleanup), which calls
// usb_cleanup(). Upstream provides it from usb_linux.cpp / usb_osx.cpp /
// usb_windows.cpp under #if !ADB_HOST; on the host, transports are torn down
// through close_usb_devices() and libusb's own teardown, so the hook is empty
// there too. (usb_init() is real: it comes from client/usb_libusb_hotplug.cpp.)
#include "sysdeps.h"
#include "transport.h"

void usb_cleanup() {}
