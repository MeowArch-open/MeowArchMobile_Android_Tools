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

// getopt_long hit '?': prints a Did-you-want hint for an unknown long option
// (argv token before optind), then returns (caller exits 1 as upstream does).
void UnknownOptionHint(int argc, char** argv, int optind_after);

}  // namespace fbglue
