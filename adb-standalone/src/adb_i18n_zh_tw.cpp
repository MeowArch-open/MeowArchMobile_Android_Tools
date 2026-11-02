/*
 * adb_i18n_zh_tw.cpp — MeowArchMobile glue (generated from adb_i18n_en.cpp).
 */
#include <iterator>

#include "adb_i18n.h"

namespace adbi18n {
namespace {

const MsgEntry kTable[] = {
    {kHelpUsage, "Android 偵錯橋版本 %s"},
    {kSecGlobalOpts, "全域選項："},
    {kSecGeneral, "一般命令："},
    {kSecNetworking, "網路："},
    {kSecFileTransfer, "文件传输："},
    {kSecShell, "shell："},
    {kSecInstall, "應用程式安裝（另見 `adb shell cmd package help`）："},
    {kSecDebugging, "偵錯："},
    {kSecSecurity, "安全性："},
    {kSecScripting, "腳本："},
    {kSecInternal, "內部偵錯："},
    {kSecUsb, "USB："},
    {kSecEnvironment, "環境變數："},
    {kHDevices, "devices [-l]\t列出已連線的裝置（-l 輸出詳細資訊）"},
    {kHHelp, "help\t顯示此說明訊息"},
    {kHVersion, "version\t顯示版本號"},
    {kHConnect, "connect HOST[:PORT]\t透過 TCP/IP 連線裝置 [預設埠=5555]"},
    {kHDisconnect, "disconnect [HOST[:PORT]]\t中斷指定的 TCP/IP 裝置\n\t[預設埠=5555]，或全部中斷"},
    {kHPair, "pair HOST[:PORT] [配對碼]\t與裝置配對以進行安全的 TCP/IP\n\t通訊"},
    {kHForward, "forward [--no-rebind] 本機 遠端\t轉送 socket 連線"},
    {kHReverse, "reverse [--no-rebind] 遠端 本機\t反向 socket 連線"},
    {kHMdns, "mdns check\t檢查 mDNS 探索是否可用"},
    {kHPush, "push [--sync] [-z 演算法] [-Z] 本機... 遠端\t將本機檔案/目錄\n\t複製到裝置"},
    {kHPull, "pull [-a] [-z 演算法] [-Z] 遠端... 本機\t從裝置複製檔案/目錄"},
    {kHSync, "sync [-l] [-z 算法] [-Z] [all|data|...]\t把 $ANDROID_PRODUCT_OUT\n\t中的本地构建同步到设备"},
    {kHShell, "shell [-e 跳脫字元] [-n] [-Tt] [-x] [命令...]\t執行遠端 shell 命令"},
    {kHEmu, "emu 命令\t執行模擬器主控台命令"},
    {kHInstall, "install [-lrtsdg] [--instant] 套件\t將單一安裝包推送到裝置\n\t並安裝"},
    {kHUninstall, "uninstall [-k] 套件\t從裝置解除安裝該應用程式"},
    {kHBugreport, "bugreport [路徑]\t將錯誤報告寫入指定路徑 [預設=bugreport.zip]"},
    {kHJdwp, "jdwp\t列出承載 JDWP 傳輸的處理程序 pid"},
    {kHLogcat, "logcat\t顯示裝置日誌（詳見 logcat --help）"},
    {kHWaitFor, "wait-for[-傳輸]-狀態...\t等待裝置進入指定狀態"},
    {kHGetState, "get-state\t打印 offline | bootloader | device"},
    {kHRemount, "remount [-R]\t以讀寫方式重新掛載分割區"},
    {kHReboot, "reboot [bootloader|recovery|sideload]\t重新啟動裝置"},
    {kHSideload, "sideload OTA包\tsideload 给定的完整 OTA 包"},
    {kHRoot, "root\t以 root 权限重启 adbd"},
    {kHTcpip, "tcpip 埠\t重新啟動 adbd 並在該 TCP 埠監聽"},
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
    {kPushing, "正在推送"},
    {kPulling, "正在拉取"},
    {kFiles, "个文件"},
    {kErrPrefix, "adb: 错误: "},
    {kNoDevices, "找不到裝置/模擬器"},
    {kDeviceNotFound, "找不到裝置 '%s'"},
    {kMdnsUnavailable, "此組建不包含 mDNS 探索\n  （請用 `adb connect IP:埠`，位址見裝置的無線偵錯頁面）"},
    {kRunHelpHint, "執行 'adb help' 查看用法。"},
    {kOLang, "--lang=CODE\t设置界面语言（参见 --list-langs）。"},
    {kOListLangs, "--list-langs\t列出可用的界面语言并退出。"},
    {kONoColor, "--no-color\t禁用彩色输出（也可用 NO_COLOR=1）。"},
    {kOColor, "--color\t即使不是终端也强制彩色输出。"},
    {kONoProgress, "--no-progress\t禁用传输进度条（也可用 NO_PROGRESS=1）。"},
    {kLangsHeader, "可用語言（--lang=CODE）："},
    {kLangCurrent, "（目前）"},
};

}  // namespace

void RegisterLangZhTw() { RegisterLang("zh_TW", "繁體中文", kTable, std::size(kTable)); }

}  // namespace adbi18n
