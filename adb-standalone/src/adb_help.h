/*
 * adb_help.h — categorised, colored, localized `adb help`.
 *
 * Not an AOSP file: MeowArchMobile glue. Upstream prints one flat block: a
 * section header followed by every entry, with no visual separation and no
 * colour. This renders the same information from a per-entry table, in the
 * same section order, with:
 *   * each section in bold and the command column in cyan (colors only when
 *     stderr/stdout is a terminal that supports them — see fb_color),
 *   * the command column aligned so the description starts at one column,
 *   * `adb help <category>` / `--list-sections` to print one section or the
 *     list of them, which the flat dump cannot do at all,
 *   * translations from adb_i18n (en / zh_CN / zh_TW / ja).
 *
 * The entry text keeps upstream's wording, so an English `adb help` reads the
 * same as platform-tools' apart from the layout.
 */
#pragma once

#include <stddef.h>

#include <string>
#include <vector>

#include "adb_i18n.h"

namespace adbhelp {

// The help table, exposed so the completion generator and `adb help` share one
// source of truth (a command that is documented but not completable, or the
// reverse, is how the two drift apart).
struct CommandInfo {
    std::string name;     // bare command or option: "push", "--lang=CODE" -> "--lang"
    std::string args;     // everything after the name in the cmd cell
    std::string desc;     // English description
    std::string section;  // section keyword ("transfer", "shell", ...)
};

// Commands only (no options), in help order.
std::vector<CommandInfo> Commands();

// Long options as documented, normalized to their bare spelling:
// "--lang=CODE" -> "--lang", "--set-active[=SLOT]" -> "--set-active".
std::vector<std::string> Options();

// Section keywords, in help order.
std::vector<std::string> SectionKeywords();

// Print the whole help text to stdout. |version| is what adb_version()
// returned (first line). Returns 0 (a Main() return value).
int ShowHelp(const char* version);

// Print one section only, matched case-insensitively against the section name
// in the active language and against the English keywords (so `adb help
// networking` works in every locale). Returns 0 on success, 1 when unknown
// (a message naming the sections is printed).
int ShowSection(const char* version, const char* name);

// Print just the section names, one per line.
int ListSections();

// True when |name| is a section keyword in the active language or English.
bool IsSection(const char* name);

}  // namespace adbhelp
