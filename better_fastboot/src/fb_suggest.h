/*
 * fb_suggest.h — "Did you want X?" typo suggestions.
 *
 * Not an AOSP file: MeowArchMobile glue. When the user types an unknown
 * command or option, upstream dumps a terse error (and, for commands, a wall
 * of nothing). We instead rank the known vocabulary by edit distance and
 * prefix/substring affinity and offer the closest match(es).
 *
 * The suggestion is a *rendering* concern only: this module never decides to
 * print help, never exits — it returns candidate strings for the caller to
 * format via the i18n kDidYouMean / kDidYouMeanList templates.
 */
#pragma once

#include <string>
#include <vector>

namespace fbsuggest {

enum class Kind { Command, Option };

// Known command vocabulary (mirrors the dispatch chain + pre-dispatch tokens).
const std::vector<std::string>& Commands();
// Known option vocabulary (long options, with the leading "--").
const std::vector<std::string>& Options();

// Rank |input| (a mistyped command/option, already stripped of any leading
// '--') against the vocabulary for |kind|. Returns up to |max| candidates in
// best-first order; empty when nothing is close enough to be worth showing.
// A candidate qualifies only if its score beats a threshold tuned so that
// "flass"->"flash" and "flas"->"flash" match but "xyzzy"->nothing does.
std::vector<std::string> Suggest(const std::string& input, Kind kind, size_t max = 2);

// True when |command| is something the dispatch chain accepts verbatim: an
// exact vocabulary hit, or the `format[:FS[:SIZE]]` prefix form. Used by the
// pre-device-open validation so known commands are never disturbed.
bool IsKnownCommand(const std::string& command);

// Levenshtein distance (exposed for tests).
unsigned EditDistance(const std::string& a, const std::string& b);

}  // namespace fbsuggest
