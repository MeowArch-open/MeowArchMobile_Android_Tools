/*
 * adb_i18n_zh_cn.cpp — MeowArchMobile glue (generated from adb_i18n_en.cpp).
 */
#include <iterator>

#include "adb_i18n.h"

namespace adbi18n {
namespace {

const MsgEntry kTable[] = {
    {kHelpUsage, "Android 调试桥版本 %s"},
    {kSecGlobalOpts, "全局选项："},
    {kSecGeneral, "通用命令："},
    {kSecNetworking, "网络："},
    {kSecFileTransfer, "文件传输："},
    {kSecShell, "shell："},
    {kSecInstall, "应用安装（另见 `adb shell cmd package help`）："},
    {kSecDebugging, "调试："},
    {kSecSecurity, "安全："},
    {kSecScripting, "脚本："},
    {kSecInternal, "内部调试："},
    {kSecUsb, "USB："},
    {kSecEnvironment, "环境变量："},
    {kHDevices, "devices [-l]\t列出已连接的设备（-l 输出详细信息）"},
    {kHHelp, "help\t显示此帮助信息"},
    {kHVersion, "version\t显示版本号"},
    {kHConnect, "connect HOST[:PORT]\t通过 TCP/IP 连接设备 [默认端口=5555]"},
    {kHDisconnect, "disconnect [HOST[:PORT]]\t断开给定的 TCP/IP 设备\n\t[默认端口=5555]，或全部断开"},
    {kHPair, "pair HOST[:PORT] [配对码]\t与设备配对以进行安全的 TCP/IP\n\t通信"},
    {kHForward, "forward [--no-rebind] 本地 远端\t转发 socket 连接"},
    {kHReverse, "reverse [--no-rebind] 远端 本地\t反向 socket 连接"},
    {kHMdns, "mdns check\t检查 mDNS 发现是否可用"},
    {kHPush, "push [--sync] [-z 算法] [-Z] 本地... 远端\t把本地文件/目录\n\t复制到设备"},
    {kHPull, "pull [-a] [-z 算法] [-Z] 远端... 本地\t从设备复制文件/目录"},
    {kHSync, "sync [-l] [-z 算法] [-Z] [all|data|...]\t把 $ANDROID_PRODUCT_OUT\n\t中的本地构建同步到设备"},
    {kHShell, "shell [-e 转义符] [-n] [-Tt] [-x] [命令...]\t执行远端 shell 命令"},
    {kHEmu, "emu 命令\t执行模拟器控制台命令"},
    {kHInstall, "install [-lrtsdg] [--instant] 软件包\t把单个安装包推送到设备\n\t并安装"},
    {kHUninstall, "uninstall [-k] 软件包\t从设备卸载该应用"},
    {kHBugreport, "bugreport [路径]\t把错误报告写入给定路径 [默认=bugreport.zip]"},
    {kHJdwp, "jdwp\t列出承载 JDWP 传输的进程 pid"},
    {kHLogcat, "logcat\t显示设备日志（更多见 logcat --help）"},
    {kHWaitFor, "wait-for[-传输]-状态...\t等待设备进入指定状态"},
    {kHGetState, "get-state\t打印 offline | bootloader | device"},
    {kHRemount, "remount [-R]\t以读写方式重新挂载分区"},
    {kHReboot, "reboot [bootloader|recovery|sideload]\t重启设备"},
    {kHSideload, "sideload OTA包\tsideload 给定的完整 OTA 包"},
    {kHRoot, "root\t以 root 权限重启 adbd"},
    {kHTcpip, "tcpip 端口\t重启 adbd 并在该 TCP 端口监听"},
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
    {kHStartServer, "start-server\t确保服务端正在运行"},
    {kHKillServer, "kill-server\t如果服务端正在运行则结束它"},
    {kErrPrefix, "adb: 错误: "},
    {kOLang, "--lang=CODE\t设置界面语言（参见 --list-langs）。"},
    {kOListLangs, "--list-langs\t列出可用的界面语言并退出。"},
    {kONoColor, "--no-color\t禁用彩色输出（也可用 NO_COLOR=1）。"},
    {kOColor, "--color\t即使不是终端也强制彩色输出。"},
    {kONoProgress, "--no-progress\t禁用传输进度条（也可用 NO_PROGRESS=1）。"},
    {kSecMeowArch, "meowarch（MeowArchMobile 附加项）："},
    {kSecUnknown, "未知的帮助分节 '%s'（试试 `adb help --list-sections`）"},
    {kLangsHeader, "可用语言（--lang=CODE）："},
    {kLangCurrent, "（当前）"},
};

}  // namespace

void RegisterLangZhCn() { RegisterLang("zh_CN", "简体中文", kTable, std::size(kTable)); }

}  // namespace adbi18n
