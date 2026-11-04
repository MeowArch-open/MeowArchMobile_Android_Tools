/*
 * adb_suggest.cpp — see adb_suggest.h. MeowArchMobile glue, not an AOSP file.
 *
 * Scoring is the one better_fastboot's fb_suggest already proved in practice:
 * Levenshtein distance, with two cheap affinities that make typo hints feel
 * right rather than merely nearest —
 *   * a prefix relation is a strong signal ("shel" is almost certainly
 *     "shell"), so prefix matches sort first regardless of distance;
 *   * candidates within a small distance are plausible, and the tolerance
 *     grows by one for long words, where a single transposition plus a
 *     missing letter is still obviously the same command.
 * Everything else is rejected outright: a wrong guess is worse than none,
 * because it teaches the user to ignore the line.
 */
#include "adb_suggest.h"

#include <algorithm>
#include <cctype>

#include "adb_help.h"

namespace adbsuggest {
namespace {

// Commands the help table documents, plus the wait-for spellings upstream
// writes as a pattern (`wait-for[-TRANSPORT]-STATE...`) rather than a list.
// This is the SAME function the completion generator uses, so a command can
// never be completable but unsuggestable.
std::vector<std::string> BuildCommands() {
    std::vector<std::string> out;
    for (const auto& c : adbhelp::Commands()) {
        if (std::find(out.begin(), out.end(), c.name) == out.end()) out.push_back(c.name);
    }
    static const char* kWaitStates[] = {"device", "recovery", "rescue", "sideload",
                                        "bootloader", "disconnect"};
    for (const char* st : kWaitStates) {
        out.push_back(std::string("wait-for-") + st);
        out.push_back(std::string("wait-for-usb-") + st);
        out.push_back(std::string("wait-for-local-") + st);
        out.push_back(std::string("wait-for-any-") + st);
    }
    return out;
}

// Options: everything adb_help documents (our five long options plus
// upstream's short flags), plus the long options that live in the options
// BLOCK rather than as table entries. Only GLOBAL options belong here — that
// is the position an unrecognized token can be in when adb's parser gives up
// (it `break`s out of the flag loop and treats the rest as a command, so
// `madb --slott` reports "unknown command --slott" and one hook covers both).
std::vector<std::string> BuildOptions() {
    std::vector<std::string> out = adbhelp::Options();
    // Deliberately NOT --reply-fd (only ever passed by the client when it forks
    // the server, so a human cannot meaningfully typo it) and NOT
    // --shell-completion (an escape hatch that exits before parsing and is not
    // documented in `adb help`; suggesting an undocumented flag would be noise).
    static const char* kExtra[] = {"--help", "--version", "--one-device",
                                   "--exit-on-write-error"};
    for (const char* o : kExtra) {
        std::string s(o);
        if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    }
    return out;
}

std::string Lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return (char)tolower(c); });
    return out;
}

// Drops the leading "--" so a constant two-character prefix cannot dominate
// the distance and make every long option look equally far away.
std::string Bare(const std::string& opt) {
    return opt.rfind("--", 0) == 0 ? opt.substr(2) : opt;
}

}  // namespace

const std::vector<std::string>& Commands() {
    static const std::vector<std::string> cmds = BuildCommands();
    return cmds;
}

const std::vector<std::string>& Options() {
    static const std::vector<std::string> opts = BuildOptions();
    return opts;
}

unsigned EditDistance(const std::string& a, const std::string& b) {
    if (a == b) return 0;
    size_t la = a.size(), lb = b.size();
    if (la == 0) return (unsigned)lb;
    if (lb == 0) return (unsigned)la;

    // Two rolling rows: the vocabularies are small but this is called once per
    // candidate, and a full matrix would be pointless work for a hint.
    std::vector<unsigned> prev(lb + 1), cur(lb + 1);
    for (size_t j = 0; j <= lb; j++) prev[j] = (unsigned)j;
    for (size_t i = 1; i <= la; i++) {
        cur[0] = (unsigned)i;
        for (size_t j = 1; j <= lb; j++) {
            unsigned cost = (a[i - 1] == b[j - 1]) ? 0u : 1u;
            cur[j] = std::min(std::min(cur[j - 1] + 1, prev[j] + 1), prev[j - 1] + cost);
        }
        std::swap(prev, cur);
    }
    return prev[lb];
}

std::vector<std::string> Suggest(const std::string& raw_input, Kind kind, size_t max) {
    const bool is_option = (kind == Kind::Option);
    const auto& vocab = is_option ? Options() : Commands();

    const std::string lin = Lower(is_option ? Bare(raw_input) : raw_input);
    if (lin.empty()) return {};

    struct Cand {
        std::string text;
        unsigned dist;
        bool prefix;
    };
    std::vector<Cand> cands;
    for (const auto& word : vocab) {
        const std::string lw = Lower(is_option ? Bare(word) : word);
        // An exact hit means the token is actually known — the caller would not
        // be asking. Skip rather than "suggest" what they already typed.
        if (lw == lin) continue;

        const unsigned d = EditDistance(lin, lw);
        const bool prefix = (lw.rfind(lin, 0) == 0) || (lin.rfind(lw, 0) == 0);
        // Tolerance grows for long words: "install-multi-packge" is one letter
        // off a 20-character command and nobody doubts which one it is.
        const unsigned tol = (lw.size() >= 8) ? 3u : 2u;
        const bool substring = lw.find(lin) != std::string::npos && lin.size() >= 3;
        if (d <= tol || prefix || substring) cands.push_back(Cand{word, d, prefix});
    }

    // Prefix first (a truncation is the most common typo of all), then nearest
    // distance, then alphabetical so the same input always gives the same
    // answer — a hint that changes between runs looks broken.
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) {
        if (x.prefix != y.prefix) return x.prefix && !y.prefix;
        if (x.dist != y.dist) return x.dist < y.dist;
        return x.text < y.text;
    });

    std::vector<std::string> out;
    for (size_t i = 0; i < cands.size() && out.size() < max; i++) out.push_back(cands[i].text);
    return out;
}

}  // namespace adbsuggest
