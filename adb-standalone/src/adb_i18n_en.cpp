/*
 * adb_i18n_en.cpp — English table. MeowArchMobile glue.
 *
 * The help entries reproduce upstream's wording exactly, so English output
 * stays byte-identical to platform-tools' adb except for the categorised
 * layout (see adb_help.cpp).
 */
#include <iterator>

#include "adb_i18n.h"

namespace adbi18n {
namespace {

const MsgEntry kEnTable[] = {
    // -- help: sections -------------------------------------------------------
    {kHelpUsage, "Android Debug Bridge version %s"},
    {kSecGlobalOpts, "global options:"},
    {kSecGeneral, "general commands:"},
    {kSecNetworking, "networking:"},
    {kSecFileTransfer, "file transfer:"},
    {kSecShell, "shell:"},
    {kSecInstall, "app installation (see also `adb shell cmd package help`):"},
    {kSecDebugging, "debugging:"},
    {kSecSecurity, "security:"},
    {kSecScripting, "scripting:"},
    {kSecInternal, "internal debugging:"},
    {kSecUsb, "usb:"},
    {kSecEnvironment, "environment variables:"},
    // -- help: entries --------------------------------------------------------
    {kHDevices, "devices [-l]\tlist connected devices (-l for long output)"},
    {kHHelp, "help\tshow this help message"},
    {kHVersion, "version\tshow version num"},
    {kHConnect, "connect HOST[:PORT]\tconnect to a device via TCP/IP [default port=5555]"},
    {kHDisconnect, "disconnect [HOST[:PORT]]\tdisconnect from given TCP/IP device\n\t[default port=5555], or all"},
    {kHPair, "pair HOST[:PORT] [PAIRING CODE]\tpair with a device for secure TCP/IP\n\tcommunication"},
    {kHForward, "forward [--no-rebind] LOCAL REMOTE\tforward socket connection"},
    {kHReverse, "reverse [--no-rebind] REMOTE LOCAL\treverse socket connection"},
    {kHMdns, "mdns check\tcheck if mdns discovery is available"},
    {kHPush, "push [--sync] [-z ALGORITHM] [-Z] LOCAL... REMOTE\tcopy local files/directories\n\tto device"},
    {kHPull, "pull [-a] [-z ALGORITHM] [-Z] REMOTE... LOCAL\tcopy files/dirs from device"},
    {kHSync, "sync [-l] [-z ALGORITHM] [-Z] [all|data|...]\tsync a local build from\n\t$ANDROID_PRODUCT_OUT to the device"},
    {kHShell, "shell [-e ESCAPE] [-n] [-Tt] [-x] [COMMAND...]\trun remote shell command"},
    {kHEmu, "emu COMMAND\trun emulator console command"},
    {kHInstall, "install [-lrtsdg] [--instant] PACKAGE\tpush a single package to the device\n\tand install it"},
    {kHUninstall, "uninstall [-k] PACKAGE\tremove this app package from the device"},
    {kHBugreport, "bugreport [PATH]\twrite bugreport to given PATH [default=bugreport.zip]"},
    {kHJdwp, "jdwp\tlist pids of processes hosting a JDWP transport"},
    {kHLogcat, "logcat\tshow device log (logcat --help for more)"},
    {kHWaitFor, "wait-for[-TRANSPORT]-STATE...\twait for device to be in a given state"},
    {kHGetState, "get-state\tprint offline | bootloader | device"},
    {kHRemount, "remount [-R]\tremount partitions read-write"},
    {kHReboot, "reboot [bootloader|recovery|sideload]\treboot the device"},
    {kHSideload, "sideload OTAPACKAGE\tsideload the given full OTA package"},
    {kHRoot, "root\trestart adbd with root permissions"},
    {kHTcpip, "tcpip PORT\trestart adbd listening on TCP on PORT"},
    {kHGetSerialNo, "get-serialno\tprint <serial-number>"},
    {kHGetDevpath, "get-devpath\tprint <device-path>"},
    {kHInstallMultiple, "install-multiple [-lrtsdpg] [--instant] PACKAGE...\tpush multiple\n\tAPKs to the device for a single package and install them"},
    {kHInstallMultiPackage, "install-multi-package [-lrtsdpg] [--instant] PACKAGE...\tpush one\n\tor more packages to the device and install them atomically"},
    {kHKeygen, "keygen FILE\tgenerate adb public/private key; private key stored in FILE"},
    {kHDisableVerity, "disable-verity\tdisable dm-verity checking on userdebug builds"},
    {kHEnableVerity, "enable-verity\tre-enable dm-verity checking on userdebug builds"},
    {kHUnroot, "unroot\trestart adbd without root permissions"},
    {kHUsbRestart, "usb\trestart adbd listening on USB"},
    {kHAttach, "attach\tattach a detached USB device"},
    {kHDetach, "detach\tdetach from a USB device to allow use by other processes"},
    {kHReconnect, "reconnect\tkick connection from host side to force reconnect"},
    {kHStartServer, "start-server\tensure that there is a server running"},
    {kHKillServer, "kill-server\tkill the server if it is running"},
    // -- runtime ---------------------------------------------------------------
    {kPushing, "Pushing"},
    {kPulling, "Pulling"},
    {kFiles, "files"},
    {kErrPrefix, "adb: error: "},
    {kNoDevices, "no devices/emulators found"},
    {kDeviceNotFound, "device '%s' not found"},
    {kMdnsUnavailable, "mDNS discovery is not available in this build\n"
                       "  (use `adb connect IP:PORT` with the address from the device's\n"
                       "  wireless-debugging screen instead)"},
    {kRunHelpHint, "Run 'adb help' for usage."},
    // -- MeowArchMobile options ------------------------------------------------
    {kOLang, "--lang=CODE\tSet interface language (see --list-langs)."},
    {kOListLangs, "--list-langs\tList available interface languages and exit."},
    {kONoColor, "--no-color\tDisable colored output (also NO_COLOR=1)."},
    {kOColor, "--color\tForce colored output even when not a tty."},
    {kONoProgress, "--no-progress\tDisable the transfer progress bar (also NO_PROGRESS=1)."},
    {kLangsHeader, "Available languages (--lang=CODE):"},
    {kLangCurrent, " (current)"},
};

}  // namespace

void RegisterLangEn() { RegisterLang("en", "English", kEnTable, std::size(kEnTable)); }

}  // namespace adbi18n
