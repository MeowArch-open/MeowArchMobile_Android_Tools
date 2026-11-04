/*
 * adb_suggest.h — "Did you want X?" typo suggestions for adb.
 *
 * Not an AOSP file: MeowArchMobile glue, the counterpart of better_fastboot's
 * fb_suggest. Upstream reports an unknown command as one terse line
 * ("adb: unknown command sheel") and stops; a single mistyped letter then
 * costs a trip to `adb help`, which is 100+ lines of text. This module ranks
 * the known vocabulary by edit distance plus prefix/substring affinity so the
 * error can name the command the user almost certainly meant.
 *
 * Rendering only: it never prints, never decides to show help and never exits.
 * It returns candidate strings for adbglue to format through the i18n
 * kDidYouMean / kDidYouMeanList templates.
 *
 * The vocabulary is SHARED with the completion generator (adb_completion.cpp
 * calls Commands()/Options() from here) so a command can never be completable
 * but unsuggestable, or the reverse. That is the same single-source-of-truth
 * rule the completion scripts already follow against `adb help`.
 */
#pragma once

#include <string>
#include <vector>

namespace adbsuggest {

enum class Kind { Command, Option };

// Every command adb accepts, including the wait-for-* state spellings that
// upstream documents as a pattern rather than a literal list.
const std::vector<std::string>& Commands();
// Every option adb accepts: the long MeowArchMobile options plus upstream's
// short flags, each exactly as typed.
const std::vector<std::string>& Options();

// Rank |input| against the vocabulary for |kind|. Returns up to |max|
// candidates best-first, or nothing when no candidate is close enough to be
// worth showing (a wholly foreign word gets no guess, which is better than a
// wrong one). For Kind::Option a leading "--" is stripped before scoring so
// the constant prefix cannot dominate the distance.
std::vector<std::string> Suggest(const std::string& input, Kind kind, size_t max = 2);

// Levenshtein distance (exposed for tests).
unsigned EditDistance(const std::string& a, const std::string& b);

}  // namespace adbsuggest
