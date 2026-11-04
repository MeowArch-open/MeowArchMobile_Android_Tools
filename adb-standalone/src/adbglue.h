/*
 * adbglue.h — integration points between upstream adb sources and the
 * MeowArchMobile UX layer (color, progress bar, i18n, categorised help).
 *
 * Not an AOSP file. The AOSP tree is patched (patches/0002) to call into this
 * header at a handful of points; everything else lives in src/.
 */
#pragma once

#include <string>

#include "adb_i18n.h"

namespace adbglue {

// Call at the very top of main(): pre-scans argv for --lang=CODE,
// --list-langs, --no-color/--color and --no-progress (so they take effect
// before anything is printed), then initializes color + i18n + progress
// detection. --list-langs prints and exits.
//
// The MeowArchMobile flags are then REMOVED from argv in place (the array is
// compacted and a new argc returned), because adb's own parser does not know
// them and would report them as an unknown command. The caller passes the
// returned argc on to adb_commandline().
int EarlyInit(int argc, char** argv);

// Prints the language table; returns 0 (usable as an exit code).
int ListLangs();

// The categorised help renderer (see adb_help.h). |version| is the string
// adb_version() returned. `adb help [SECTION]` and `adb help
// --list-sections` are routed here from commandline.cpp.
int ShowHelp(const std::string& version);
int ShowHelpSection(const std::string& version, const char* section);
int ListHelpSections();

// "Did you want 'shell'?" — printed after an unknown command/option error.
//
// |message| is the ENGLISH text adb formatted ("unknown command sheel"), which
// is what makes this work in every language: the hook sits in error_exit_va
// before translation, and upstream's own error line is left byte-identical.
// Nothing is printed when no candidate is close enough, so a wholly foreign
// word gets the terse upstream error and no wrong guess.
void MaybeHintUnknownCommand(const std::string& message);

// Print a completion script for |shell| ("bash", "zsh" or "fish"), completing
// |command| as the program name. Returns 0, or 2 with a diagnostic on stderr
// when the shell is unknown.
int ShellCompletion(const std::string& shell, const std::string& command);
bool IsHelpSection(const char* name);

// Progress bar for the sync loop (client/file_sync_client.cpp). The transfer
// ledger arms it with the file name and expected size, reports bytes as they
// move, and finishes before printing its usual summary line.
void ProgressArm(const std::string& label, uint64_t total);
// Percentage-driven mode (adb bugreport, whose device side reports a fraction
// rather than bytes): bar + "NN%", no byte counter and no speed.
void ProgressArmPercent(const std::string& label);
void ProgressUpdatePercent(int pct);
void ProgressSetLabel(const std::string& label);
void ProgressUpdate(uint64_t done);
void ProgressFinish();
bool ProgressEnabled();

}  // namespace adbglue
