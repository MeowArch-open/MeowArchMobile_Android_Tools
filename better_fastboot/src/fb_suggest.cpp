/*
 * fb_suggest.cpp — see fb_suggest.h. MeowArchMobile glue, not an AOSP file.
 *
 * Scoring: primarily Levenshtein distance, but tuned with the two cheap
 * affinities that make typo suggestions feel right in practice:
 *   * a prefix match (input is a prefix of the candidate, or vice versa) is a
 *     strong signal — "flas" is almost certainly "flash";
 *   * candidates within distance 2 of the input are plausible; beyond that we
 *     need the prefix/substring affinity to avoid silly suggestions.
 */
#include "fb_suggest.h"

#include <algorithm>
#include <cctype>

namespace fbsuggest {
namespace {

// Command vocabulary: everything the dispatch chain in fastboot.cpp recognizes,
// plus the pre-dispatch tokens (devices/connect/disconnect/help) and the
// FB_CMD_* constants from constants.h. Kept in sync by hand; it only drives
// suggestions, so a missing entry merely means no hint for that word.
const std::vector<std::string> kCommands = {
    // pre-dispatch
    "devices", "connect", "disconnect", "help",
    // dispatch chain
    "getvar", "erase", "format", "signature", "reboot", "reboot-bootloader",
    "reboot-recovery", "reboot-fastboot", "continue", "boot", "flash",
    "flash:raw", "flashall", "update", "set_active", "stage", "get_staged",
    "oem", "flashing", "create-logical-partition", "delete-logical-partition",
    "resize-logical-partition", "snapshot-update", "fetch", "gsi", "wipe-super",
    "upload", "get_staged",
};

// Option vocabulary: the long options in the getopt_long table, with "--".
const std::vector<std::string> kOptions = {
    "--base", "--cmdline", "--disable-verification", "--disable-verity",
    "--disable-super-optimization", "--exclude-dynamic-partitions",
    "--disable-fastboot-info", "--force", "--fs-options", "--header-version",
    "--help", "--kernel-offset", "--os-patch-level", "--os-version",
    "--page-size", "--ramdisk-offset", "--set-active", "--skip-reboot",
    "--skip-secondary", "--slot", "--tags-offset", "--dtb", "--dtb-offset",
    "--unbuffered", "--verbose", "--version",
    // our additions
    "--lang", "--list-langs", "--no-color", "--color", "--no-progress",
};

}  // namespace

const std::vector<std::string>& Commands() { return kCommands; }
const std::vector<std::string>& Options() { return kOptions; }

unsigned EditDistance(const std::string& a, const std::string& b) {
    if (a == b) return 0;
    size_t la = a.size(), lb = b.size();
    if (la == 0) return (unsigned)lb;
    if (lb == 0) return (unsigned)la;

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
    const auto& vocab = (kind == Kind::Command) ? kCommands : kOptions;

    // Normalize: drop a leading "--" for options so distance isn't dominated by
    // the constant prefix; commands are compared as-is (lowercased).
    std::string input = raw_input;
    if (kind == Kind::Option) {
        if (input.rfind("--", 0) == 0) input = input.substr(2);
        if (input.empty()) return {};
    }
    std::string lin = input;
    std::transform(lin.begin(), lin.end(), lin.begin(),
                   [](unsigned char c) { return (char)tolower(c); });

    struct Cand {
        std::string text;
        unsigned dist;
        bool prefix;
    };
    std::vector<Cand> cands;
    for (const auto& word : vocab) {
        std::string lw = word;
        if (kind == Kind::Option && lw.rfind("--", 0) == 0) lw = lw.substr(2);
        std::transform(lw.begin(), lw.end(), lw.begin(),
                       [](unsigned char c) { return (char)tolower(c); });
        if (lw == lin) {
            // Exact match means it's actually a known command — no suggestion
            // needed (the caller wouldn't be here). Skip.
            continue;
        }
        unsigned d = EditDistance(lin, lw);
        bool prefix = (lw.rfind(lin, 0) == 0) || (lin.rfind(lw, 0) == 0);
        // Threshold: close by distance, or a prefix/substring affinity with a
        // non-trivial overlap. Long words tolerate a slightly larger distance.
        unsigned tol = (lw.size() >= 8) ? 3u : 2u;
        bool substring = lw.find(lin) != std::string::npos && lin.size() >= 3;
        if (d <= tol || prefix || substring) {
            cands.push_back(Cand{word, d, prefix});
        }
    }

    // Sort: prefix matches first, then by distance, then alphabetical for
    // stability.
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) {
        if (x.prefix != y.prefix) return x.prefix && !y.prefix;
        if (x.dist != y.dist) return x.dist < y.dist;
        return x.text < y.text;
    });

    std::vector<std::string> out;
    for (size_t i = 0; i < cands.size() && out.size() < max; i++) out.push_back(cands[i].text);
    return out;
}

bool IsKnownCommand(const std::string& command) {
    // `format` accepts ":FS_TYPE[:SIZE]" glued onto the command word
    // (upstream: StartsWith(command, "format")).
    if (command.rfind("format", 0) == 0) {
        if (command.size() == 6 || command[6] == ':') return true;
    }
    for (const auto& w : kCommands) {
        if (w == command) return true;
    }
    return false;
}

}  // namespace fbsuggest
