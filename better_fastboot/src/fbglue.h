/*
 * fbglue.h — integration points between upstream fastboot sources and the
 * MeowArchMobile UX features (color, progress bar, i18n, typo suggestions).
 *
 * Not an AOSP file. The AOSP tree is patched (patches/0003..0006) to call
 * into this header; everything else here is self-contained glue.
 */
#pragma once

#include <string>

#include <android-base/stringprintf.h>

#include "fb_color.h"
#include "fb_i18n.h"
#include "fb_payload.h"

namespace fbglue {

// Call at the very top of FastBootTool::Main(): pre-scans argv for
// --lang=CODE/--no-color/--color/--no-progress/--list-langs (so they take
// effect before getopt runs and before anything is printed), initializes
// color + i18n + progress detection. --list-langs prints and exits here.
void EarlyInit(int argc, char** argv);

// Prints the language table; returns 0 (used as a Main() return value).
int ListLangs();

// Localized printf-style prolog text for the driver. The translated format
// string MUST keep the same conversion specifiers as the English original
// (StringPrintf validates them).
template <typename... Args>
std::string Prolog(fbi18n::Msg id, Args... args) {
    return android::base::StringPrintf(fbi18n::T(id), args...);
}

// "fastboot: error: unknown command 'X'" + Did-you-want + run-help hint.
// Exits(1) like upstream's syntax_error.
[[noreturn]] void UnknownCommandError(const std::string& command);

// Pre-pass over the leading command token, run BEFORE open_device() blocks on
// device discovery: upstream only reaches the "unknown command" error after a
// device connects, so a typo like `fastboot flass` would otherwise hang on
// "< waiting for any device >". Known commands (and the `format[:...]` prefix
// form) pass through untouched; anything else errors out with a suggestion.
void ValidateFirstCommand(const std::string& command);

// "no command" usage error + one-line usage + run-help hint. Exits(1).
[[noreturn]] void NoCommandError();

// One extracted partition image from a payload.bin, plus the localized error
// when extraction failed. The image is an RAII temp file: it unlinks itself
// when the handle goes out of scope, i.e. after the flash that consumed it.
struct PayloadExtraction {
    fbpayload::Image image;
    std::string error;  // empty on success; already localized, ready to print
    bool ok() const { return image.valid(); }
};

// `flash-x PARTITION PAYLOAD [NAME]`: pull one partition image out of an OTA
// payload.bin into a temp file so the caller can flash it through the normal
// path. Prints its own "Extracting ..." prolog and progress bar, then the
// OKAY/FAILED epilog, matching how every other command reports. NAME selects
// the entry inside the payload; when empty, PARTITION is used with any slot
// suffix stripped (so `flash-x boot_a payload.bin` finds "boot").
//
// This is the part that keeps upstream untouched: the caller hands the returned
// path to its own FlashTask, so sparse re-splitting, logical-partition resize,
// vbmeta rewriting and slot handling all stay upstream behavior.
PayloadExtraction ExtractFromPayload(const std::string& target_partition,
                                     const std::string& payload_path, const std::string& inner);

// getopt_long hit '?': prints a Did-you-want hint for an unknown long option
// (argv token before optind), then returns (caller exits 1 as upstream does).
void UnknownOptionHint(int argc, char** argv, int optind_after);

}  // namespace fbglue
