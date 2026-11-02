/*
 * adb_i18n.h — message catalog for the standalone adb client.
 *
 * Not an AOSP file: MeowArchMobile glue. Same design as better_fastboot's
 * fb_i18n: an X-macro ID list, one {id, text} table per language registered at
 * init, English fallback for anything a table omits. Kept separate from the
 * fastboot catalog because the two tools share no message text beyond a few
 * words, and a shared header would drag fastboot's ~250 IDs into adb.
 *
 * Locale selection (first match wins): --lang=CODE, then LANGUAGE, LC_ALL,
 * LC_MESSAGES, LANG. "zh_CN.UTF-8" normalizes to "zh_CN", then "zh". Unset or
 * unrecognized => English.
 */
#pragma once

#include <stddef.h>

#include <string>

#define ADB_MESSAGES(X)                                                                        \
    /* --help sections and structure */                                                        \
    X(kHelpUsage)                                                                              \
    X(kSecGlobalOpts)                                                                          \
    X(kSecGeneral)                                                                             \
    X(kSecNetworking)                                                                          \
    X(kSecFileTransfer)                                                                        \
    X(kSecShell)                                                                               \
    X(kSecInstall)                                                                             \
    X(kSecDebugging)                                                                           \
    X(kSecSecurity)                                                                            \
    X(kSecScripting)                                                                           \
    X(kSecInternal)                                                                            \
    X(kSecUsb)                                                                                 \
    X(kSecEnvironment)                                                                         \
    /* --help entries (cmd\tdescription) */                                                    \
    X(kHDevices)                                                                               \
    X(kHHelp)                                                                                  \
    X(kHVersion)                                                                               \
    X(kHConnect)                                                                               \
    X(kHDisconnect)                                                                            \
    X(kHPair)                                                                                  \
    X(kHForward)                                                                               \
    X(kHReverse)                                                                               \
    X(kHMdns)                                                                                  \
    X(kHPush)                                                                                  \
    X(kHPull)                                                                                  \
    X(kHSync)                                                                                  \
    X(kHShell)                                                                                 \
    X(kHEmu)                                                                                   \
    X(kHInstall)                                                                               \
    X(kHUninstall)                                                                             \
    X(kHBugreport)                                                                             \
    X(kHJdwp)                                                                                  \
    X(kHLogcat)                                                                                \
    X(kHWaitFor)                                                                               \
    X(kHGetState)                                                                              \
    X(kHRemount)                                                                               \
    X(kHReboot)                                                                                \
    X(kHSideload)                                                                              \
    X(kHRoot)                                                                                  \
    X(kHTcpip)                                                                                 \
    X(kHGetSerialNo)                                                                           \
    X(kHGetDevpath)                                                                            \
    X(kHInstallMultiple)                                                                       \
    X(kHInstallMultiPackage)                                                                   \
    X(kHKeygen)                                                                                \
    X(kHDisableVerity)                                                                         \
    X(kHEnableVerity)                                                                          \
    X(kHUnroot)                                                                                \
    X(kHUsbRestart)                                                                            \
    X(kHAttach)                                                                                \
    X(kHDetach)                                                                                \
    X(kHReconnect)                                                                             \
    X(kHStartServer)                                                                           \
    X(kHKillServer)                                                                            \
    /* runtime: transfer bar */                                                                \
    X(kPushing)                                                                                \
    X(kPulling)                                                                                \
    X(kFiles)                                                                                  \
    /* runtime: errors and hints */                                                            \
    X(kErrPrefix)                                                                              \
    X(kNoDevices)                                                                              \
    X(kDeviceNotFound)                                                                         \
    X(kMdnsUnavailable)                                                                        \
    X(kRunHelpHint)                                                                            \
    /* MeowArchMobile options */                                                               \
    X(kOLang)                                                                                  \
    X(kOListLangs)                                                                             \
    X(kONoColor)                                                                               \
    X(kOColor)                                                                                 \
    X(kONoProgress)                                                                            \
    X(kLangsHeader)                                                                            \
    X(kLangCurrent)                                                                            \
    /* end */

namespace adbi18n {

enum Msg {
    kNone = 0,
#define ADB_X(id) id,
    ADB_MESSAGES(ADB_X)
#undef ADB_X
    kNumMessages,
};

struct MsgEntry {
    Msg id;
    const char* text;
};

void Init(const std::string& lang_override = "");
const char* T(Msg id);
const std::string& Locale();

struct LangInfo {
    const char* code;
    const char* native;
};
const LangInfo* LangTable(size_t* count);

void RegisterLang(const char* code, const char* native, const MsgEntry* table, size_t count);

// Runtime error strings.
//
// adb's ~150 error_exit()/perror_exit() call sites each pass a literal format
// string, so translating them would mean editing every one. Instead the single
// funnel (error_exit_va in client/adb_client.cpp) looks the format up here by
// exact match and prints the active language's text if there is one, otherwise
// the original English. Format specifiers (%s, %d) are preserved, so the
// vfprintf arguments still line up.
//
// TranslateError: same job for a message that has ALREADY been formatted (the
// device-not-found text, say, comes out of transport.cpp as a finished string,
// not a format+args pair). The English text is matched against the table with
// each specifier acting as a wildcard, the captured values are substituted into
// the target language, and "" is returned when nothing matches — callers then
// print the original.
std::string TranslateError(const std::string& text);

}  // namespace adbi18n

inline const char* _(adbi18n::Msg id) { return adbi18n::T(id); }
