/*
 * fb_i18n.h — message catalog for the standalone fastboot build.
 *
 * Not an AOSP file: MeowArchMobile glue. Upstream fastboot has no i18n; every
 * user-facing string is an English literal. Rather than wrap ~300 upstream
 * call sites in gettext (which would make the tree patch unmaintainable), the
 * strings users actually read are collected here as message IDs and rendered
 * from per-language tables. English text matches upstream wording, so English
 * output stays byte-identical to upstream (CI verifies this).
 *
 * The ID list is an X-macro so adding a message updates the enum and every
 * table's namespace in lockstep. Language tables are arrays of {Msg, text}
 * pairs (portable across clang and GCC — designated initializers with gaps
 * are a clang-only C++ extension); RegisterLang() densifies them at init.
 *
 * Help entries (the kH... / kO... message IDs) are stored as
 * "command\tdescription" where embedded newlines in the description mark
 * continuation lines; fb_help.cpp renders them at upstream's exact column
 * (29).
 *
 * Locale selection (first match wins): --lang=CODE, then LANGUAGE, LC_ALL,
 * LC_MESSAGES, LANG. "zh_CN.UTF-8" normalizes to "zh_CN", then falls back to
 * "zh". Unset / unrecognized / "C" / "POSIX" => English.
 */
#pragma once

#include <inttypes.h>  // PRIx64 used in the kFetching format string

#include <string>

// ---------------------------------------------------------------- message IDs
// Keep this list append-only: indices are positional in every language table.
#define FB_MESSAGES(X)                                                                             \
    /* help: structure */                                                                          \
    X(kHelpUsage)                                                                                  \
    X(kSecFlashing)                                                                                \
    X(kSecBasics)                                                                                  \
    X(kSecLocking)                                                                                 \
    X(kSecAdvanced)                                                                                \
    X(kSecBootImage)                                                                               \
    X(kSecAndroidThings)                                                                           \
    X(kSecOptions)                                                                                 \
    /* help: commands (format: "cmd\tdesc"; "\n" in desc = continuation line) */                  \
    X(kHUpdate)                                                                                    \
    X(kHFlashall)                                                                                  \
    X(kHFlash)                                                                                     \
    X(kHFlashVb)                                                                                   \
    X(kHDtbIf)                                                                                     \
    X(kHDevices)                                                                                   \
    X(kHGetvar)                                                                                    \
    X(kHReboot)                                                                                    \
    X(kHFlashingLock)                                                                              \
    X(kHFlashingCrit)                                                                              \
    X(kHFlashingAbility)                                                                           \
    X(kHErase)                                                                                     \
    X(kHFormat)                                                                                    \
    X(kHSetActive)                                                                                 \
    X(kHOem)                                                                                       \
    X(kHGsi)                                                                                       \
    X(kHWipeSuper)                                                                                 \
    X(kHCreateLp)                                                                                  \
    X(kHDeleteLp)                                                                                  \
    X(kHResizeLp)                                                                                  \
    X(kHSnapCancel)                                                                                \
    X(kHSnapMerge)                                                                                 \
    X(kHFetch)                                                                                     \
    X(kHBoot)                                                                                      \
    X(kHFlashRaw)                                                                                  \
    X(kHStage)                                                                                     \
    X(kHGetStaged)                                                                                 \
    /* help: boot-image options */                                                                 \
    X(kHDtb)                                                                                       \
    X(kHCmdline)                                                                                   \
    X(kHBase)                                                                                      \
    X(kHKernelOffset)                                                                              \
    X(kHRamdiskOffset)                                                                             \
    X(kHTagsOffset)                                                                                \
    X(kHDtbOffset)                                                                                 \
    X(kHPageSize)                                                                                  \
    X(kHHeaderVersion)                                                                             \
    X(kHOsVersion)                                                                                 \
    X(kHOsPatch)                                                                                   \
    /* help: options */                                                                            \
    X(kOWipe)                                                                                      \
    X(kOSerial)                                                                                    \
    X(kOSerialNet)                                                                                 \
    X(kOSparse)                                                                                    \
    X(kOForce)                                                                                     \
    X(kOSlot)                                                                                      \
    X(kOSetActive)                                                                                 \
    X(kOSkipSecondary)                                                                             \
    X(kOSkipReboot)                                                                                \
    X(kODisableVerity)                                                                             \
    X(kODisableVerification)                                                                       \
    X(kODisableSuperOpt)                                                                           \
    X(kOExcludeDynamic)                                                                            \
    X(kODisableFbInfo)                                                                             \
    X(kOFsOptions)                                                                                 \
    X(kOUnbuffered)                                                                                \
    X(kOVerbose)                                                                                   \
    X(kOVersion)                                                                                   \
    X(kOHelp)                                                                                      \
    X(kOLang)                                                                                      \
    X(kOListLangs)                                                                                 \
    X(kONoColor)                                                                                   \
    X(kOColor)                                                                                     \
    X(kONoProgress)                                                                                \
    /* runtime: status / epilog */                                                                 \
    X(kOkay)                                                                                       \
    X(kFailed)                                                                                     \
    X(kFinished)                                                                                   \
    X(kWaitingDevice)                                                                              \
    X(kWaitingFor)                                                                                 \
    X(kWaitingAny)                                                                                 \
    /* runtime: errors */                                                                          \
    X(kErrPrefix)                                                                                  \
    X(kUsagePrefix)                                                                                \
    X(kVerbosePrefix)                                                                              \
    X(kUnknownCommand)                                                                             \
    X(kUnknownOption)                                                                              \
    X(kUnknownPartition)                                                                           \
    X(kDidYouMean)                                                                                 \
    X(kDidYouMeanList)                                                                             \
    X(kRunHelp)                                                                                    \
    X(kNoCommand)                                                                                  \
    X(kExpectedArgument)                                                                           \
    X(kEmptyOemCommand)                                                                            \
    X(kJunkAfterReboot)                                                                            \
    X(kMissingFlashingCommand)                                                                     \
    X(kUnknownFlashingCommand)                                                                     \
    X(kInvalidGsiCommand)                                                                          \
    X(kExpectedSnapshotUpdate)                                                                     \
    X(kUnsupportedFsOption)                                                                        \
    /* driver prologs (printf formats; arguments as upstream) */                                   \
    X(kSending)                                                                                    \
    X(kSendingSparse)                                                                              \
    X(kWriting)                                                                                    \
    X(kErasing)                                                                                    \
    X(kUploading)                                                                                  \
    X(kFetching)                                                                                   \
    X(kBooting)                                                                                    \
    X(kResumingBoot)                                                                               \
    X(kRebooting)                                                                                  \
    X(kRebootingInto)                                                                              \
    X(kCreating)                                                                                   \
    X(kDeleting)                                                                                   \
    X(kResizing)                                                                                   \
    X(kSettingSlot)                                                                                \
    X(kSnapshot)                                                                                   \
    /* progress bar */                                                                             \
    X(kEta)                                                                                        \
    X(kSpeedUnit)                                                                                  \
    /* languages */                                                                                \
    X(kLangsHeader)                                                                                \
    X(kLangCurrent)                                                                                \
    /* end */

namespace fbi18n {

enum Msg {
    // kNone reserves 0 as the "no message id" sentinel used by generated
    // help rows — the first real ID must never be 0.
    kNone = 0,
#define FB_X(id) id,
    FB_MESSAGES(FB_X)
#undef FB_X
    kNumMessages,
};

// One table entry: a message ID and its text. Tables are plain arrays of
// these pairs (NOT designated initializers — GCC's C++ front end rejects
// sparse/reordered [id]= designators), looked up by linear scan at Init()
// time into a dense per-language array.
struct MsgEntry {
    Msg id;
    const char* text;
};

// Initialize from the environment (or an explicit --lang override, which may be
// empty to mean "use the environment"). Safe to call more than once.
void Init(const std::string& lang_override = "");

// Localized text for |id|. Never returns nullptr; falls back to English for a
// missing entry so a partial table degrades instead of crashing.
const char* T(Msg id);

// Resolved locale code as normalized by Init() ("en", "zh_CN", ...).
const std::string& Locale();

// Registry of shipped languages, for --list-langs. Each entry is
// {code, native name}. English is always first.
struct LangInfo {
    const char* code;
    const char* native;
};
const LangInfo* LangTable(size_t* count);

// Called from the static initializer of each fb_i18n_<lang>.cpp. Not part of
// the public API — language tables use it to register themselves. |count| is
// the number of entries in |table| (std::size at the call site).
void RegisterLang(const char* code, const char* native, const MsgEntry* table, size_t count);

}  // namespace fbi18n

// Shorthand used across the glue code.
inline const char* _(fbi18n::Msg id) { return fbi18n::T(id); }
