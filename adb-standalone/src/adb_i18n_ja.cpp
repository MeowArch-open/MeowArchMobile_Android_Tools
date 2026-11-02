/*
 * adb_i18n_ja.cpp — MeowArchMobile glue (generated from adb_i18n_en.cpp).
 */
#include <iterator>

#include "adb_i18n.h"

namespace adbi18n {
namespace {

const MsgEntry kTable[] = {
    {kHelpUsage, "Android Debug Bridge バージョン %s"},
    {kSecGlobalOpts, "グローバルオプション:"},
    {kSecGeneral, "一般コマンド:"},
    {kSecNetworking, "ネットワーク:"},
    {kSecFileTransfer, "ファイル転送:"},
    {kSecShell, "shell:"},
    {kSecInstall, "アプリのインストール (`adb shell cmd package help` も参照):"},
    {kSecDebugging, "デバッグ:"},
    {kSecSecurity, "セキュリティ:"},
    {kSecScripting, "スクリプト:"},
    {kSecInternal, "内部デバッグ:"},
    {kSecUsb, "usb:"},
    {kSecEnvironment, "環境変数:"},
    {kHDevices, "devices [-l]\t接続中のデバイスを一覧表示 (-l で詳細)"},
    {kHHelp, "help\tこのヘルプを表示"},
    {kHVersion, "version\tバージョン番号を表示"},
    {kHConnect, "connect HOST[:PORT]\tTCP/IP でデバイスに接続 [既定ポート=5555]"},
    {kHDisconnect, "disconnect [HOST[:PORT]]\t指定した TCP/IP デバイス\n\t[既定ポート=5555]、または全てを切断"},
    {kHPair, "pair HOST[:PORT] [ペアリングコード]\t安全な TCP/IP 通信のため\n\tデバイスとペアリング"},
    {kHForward, "forward [--no-rebind] LOCAL REMOTE\tsocket 接続を転送"},
    {kHReverse, "reverse [--no-rebind] REMOTE LOCAL\t逆方向 socket 接続"},
    {kHMdns, "mdns check\tmDNS 探索が利用可能か確認"},
    {kHPush, "push [--sync] [-z ALGORITHM] [-Z] LOCAL... REMOTE\tローカルの\n\tファイル/ディレクトリをデバイスへコピー"},
    {kHPull, "pull [-a] [-z ALGORITHM] [-Z] REMOTE... LOCAL\tデバイスからコピー"},
    {kHSync, "sync [-l] [-z ALGORITHM] [-Z] [all|data|...]\t$ANDROID_PRODUCT_OUT\n\tのビルドをデバイスへ同期"},
    {kHShell, "shell [-e ESCAPE] [-n] [-Tt] [-x] [COMMAND...]\tリモートシェルを実行"},
    {kHEmu, "emu COMMAND\tエミュレータコンソールコマンドを実行"},
    {kHInstall, "install [-lrtsdg] [--instant] PACKAGE\t単一パッケージを送って\n\tインストール"},
    {kHUninstall, "uninstall [-k] PACKAGE\tこのアプリを削除"},
    {kHBugreport, "bugreport [PATH]\tバグレポートを PATH に書き出し [既定=bugreport.zip]"},
    {kHJdwp, "jdwp\tJDWP トランスポートを持つプロセスの pid を一覧"},
    {kHLogcat, "logcat\tデバイスログを表示 (詳細は logcat --help)"},
    {kHWaitFor, "wait-for[-TRANSPORT]-STATE...\t指定状態になるまで待機"},
    {kHGetState, "get-state\toffline | bootloader | device を出力"},
    {kHRemount, "remount [-R]\tパーティションを読み書き可能で再マウント"},
    {kHReboot, "reboot [bootloader|recovery|sideload]\tデバイスを再起動"},
    {kHSideload, "sideload OTAPACKAGE\t指定した完全な OTA パッケージを sideload"},
    {kHRoot, "root\tadbd を root 権限で再起動"},
    {kHTcpip, "tcpip PORT\tadbd を TCP の PORT で待ち受け再起動"},
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
    {kHStartServer, "start-server\tサーバーが動作していることを保証"},
    {kHKillServer, "kill-server\t動作中ならサーバーを終了"},
    {kPushing, "転送中"},
    {kPulling, "受信中"},
    {kFiles, "ファイル"},
    {kErrPrefix, "adb: エラー: "},
    {kNoDevices, "デバイス/エミュレータが見つかりません"},
    {kDeviceNotFound, "デバイス '%s' が見つかりません"},
    {kMdnsUnavailable, "このビルドには mDNS 探索が含まれていません\n  （デバイスのワイヤレスデバッグ画面のアドレスを使い\n  `adb connect IP:PORT` で接続してください）"},
    {kRunHelpHint, "'adb help' で使い方を確認してください。"},
    {kOLang, "--lang=CODE\t表示言語を設定 (--list-langs 参照)。"},
    {kOListLangs, "--list-langs\t利用可能な言語を一覧して終了。"},
    {kONoColor, "--no-color\t色付き出力を無効化 (NO_COLOR=1 でも可)。"},
    {kOColor, "--color\t端末でなくても色付き出力を強制。"},
    {kONoProgress, "--no-progress\t転送プログレスバーを無効化 (NO_PROGRESS=1 でも可)。"},
    {kLangsHeader, "利用可能な言語 (--lang=CODE):"},
    {kLangCurrent, " (現在)"},
};

}  // namespace

void RegisterLangJa() { RegisterLang("ja", "日本語", kTable, std::size(kTable)); }

}  // namespace adbi18n
