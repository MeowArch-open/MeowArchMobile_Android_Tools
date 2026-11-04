/*
 * test_suggest.cpp — checks the Did-you-want scorer directly.
 *
 * Not shipped: a test binary. The CLI-level checks in scripts/test-adb.sh prove
 * the hook fires and is localized; this proves the SCORING rules hold, which is
 * where regressions actually hide: a suggestion that is confidently wrong is
 * worse than none, because it teaches the user to ignore the line, and the
 * thresholds that keep it honest are invisible from the command line unless you
 * enumerate both directions.
 *
 * Usage: test_suggest            (exit 0 = every case held, prints a summary)
 */
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "adb_help.h"
#include "adb_i18n.h"
#include "adb_suggest.h"

static int g_pass = 0, g_fail = 0;

static void ok(const char* what) {
    printf("  ok   %s\n", what);
    g_pass++;
}

static void bad(const std::string& what) {
    printf("  FAIL %s\n", what.c_str());
    g_fail++;
}

// True when |want| is one of the candidates for |input|.
static bool Has(const std::vector<std::string>& cands, const std::string& want) {
    for (const auto& c : cands) {
        if (c == want) return true;
    }
    return false;
}

static std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); i++) {
        if (i) s += ", ";
        s += "'" + v[i] + "'";
    }
    return s.empty() ? "(none)" : s;
}

// The typo -> command pairs a user actually makes: a dropped letter, a doubled
// letter, a transposition, a truncated tail. Each must name its target.
struct Case {
    const char* input;
    const char* want;
};

int main() {
    // The help table is the vocabulary's source, and it is built from the
    // message catalog, so Init() has to run first (the same reason
    // test_progress_layout calls fbcolor::Init()).
    adbi18n::Init("en");

    static const Case kCommands[] = {
        {"sheel", "shell"},        // transposition
        {"shel", "shell"},         // dropped letter
        {"shelll", "shell"},       // doubled letter
        {"pul", "pull"},           // truncation (push also qualifies; list form)
        {"pushh", "push"},
        {"instal", "install"},
        {"bugrepot", "bugreport"},
        {"devicse", "devices"},    // transposition of a whole syllable
        {"remoun", "remount"},
        {"logcta", "logcat"},
        {"sidelad", "sideload"},
        {"keygen2", "keygen"},
    };
    for (const auto& c : kCommands) {
        auto got = adbsuggest::Suggest(c.input, adbsuggest::Kind::Command, 2);
        std::string what = std::string(c.input) + " -> " + Join(got);
        if (Has(got, c.want)) {
            ok(what.c_str());
        } else {
            bad("command '" + std::string(c.input) + "' did not suggest '" + c.want +
                "', got " + Join(got));
        }
    }

    // Options: the leading "--" must not dominate the distance, so a typo one
    // letter off still resolves.
    static const Case kOptions[] = {
        {"--colr", "--color"},
        {"--no-colour", "--no-color"},
        {"--helpp", "--help"},
        {"--one-devic", "--one-device"},
        {"--list-lang", "--list-langs"},
        {"--no-progres", "--no-progress"},
        {"--exit-on-write-erro", "--exit-on-write-error"},
    };
    for (const auto& c : kOptions) {
        auto got = adbsuggest::Suggest(c.input, adbsuggest::Kind::Option, 2);
        if (Has(got, c.want)) {
            ok((std::string(c.input) + " -> " + Join(got)).c_str());
        } else {
            bad("option '" + std::string(c.input) + "' did not suggest '" + c.want +
                "', got " + Join(got));
        }
    }

    // A wholly foreign word must produce NOTHING. This is the property that
    // keeps the hint trustworthy: a wrong guess on every unknown command would
    // be worse than upstream's silence.
    static const char* kForeign[] = {"xyzzy", "qqqqq", "banana", "42", "-"};
    for (const char* f : kForeign) {
        auto got = adbsuggest::Suggest(f, adbsuggest::Kind::Command, 2);
        if (got.empty()) {
            ok((std::string(f) + " -> no suggestion (correct)").c_str());
        } else {
            bad("foreign word '" + std::string(f) + "' got a suggestion: " + Join(got));
        }
    }

    // The nonsense case: a token must never suggest ITSELF. ("Did you want
    // 'shell'?" after a valid `shell` would be absurd.) Note that a known
    // command MAY still return other candidates — `install` scores
    // `install-multiple` as a prefix hit — and that is not a bug: the only
    // caller is the "unknown command" path, which by construction cannot be
    // reached with a known token, and if a future caller scores one from a
    // subcommand position, naming its own extension is the useful answer.
    int self_bad = 0;
    for (const auto& c : adbsuggest::Commands()) {
        if (Has(adbsuggest::Suggest(c, adbsuggest::Kind::Command, 2), c)) self_bad++;
    }
    if (self_bad == 0) {
        ok("no command suggests itself");
    } else {
        bad(std::to_string(self_bad) + " commands suggested themselves");
    }
    int oself_bad = 0;
    for (const auto& o : adbsuggest::Options()) {
        if (Has(adbsuggest::Suggest(o, adbsuggest::Kind::Option, 2), o)) oself_bad++;
    }
    if (oself_bad == 0) {
        ok("no option suggests itself");
    } else {
        bad(std::to_string(oself_bad) + " options suggested themselves");
    }

    // Vocabularies must be non-empty and consistent with the help table, or the
    // scorer is silently ranking against nothing.
    if (!adbsuggest::Commands().empty()) {
        ok(("command vocabulary has " + std::to_string(adbsuggest::Commands().size()) +
            " entries")
                 .c_str());
    } else {
        bad("command vocabulary is EMPTY");
    }
    if (Has(adbsuggest::Commands(), "wait-for-device")) {
        ok("wait-for-* spellings are in the vocabulary");
    } else {
        bad("wait-for-device missing from the vocabulary");
    }
    bool every_help_cmd = true;
    for (const auto& c : adbhelp::Commands()) {
        if (!Has(adbsuggest::Commands(), c.name)) {
            every_help_cmd = false;
            bad("documented command not suggestable: " + c.name);
            break;
        }
    }
    if (every_help_cmd) ok("every documented command is suggestable");

    // Options must include upstream's short flags AND our own long ones, since
    // both can be mistyped in the same position.
    if (Has(adbsuggest::Options(), "-s") && Has(adbsuggest::Options(), "--lang")) {
        ok("option vocabulary covers short flags and our long options");
    } else {
        bad("option vocabulary is missing -s or --lang");
    }
    // Undocumented/internal flags must NOT be suggested: offering them would
    // teach a user to type something `adb help` never mentions.
    if (!Has(adbsuggest::Options(), "--reply-fd") &&
        !Has(adbsuggest::Options(), "--shell-completion")) {
        ok("internal flags are not suggested");
    } else {
        bad("an internal flag leaked into the option vocabulary");
    }

    // Determinism: the same input twice must give the same answer. A hint that
    // changes between runs looks broken even when both answers are fine.
    auto a1 = adbsuggest::Suggest("pul", adbsuggest::Kind::Command, 2);
    auto a2 = adbsuggest::Suggest("pul", adbsuggest::Kind::Command, 2);
    if (Join(a1) == Join(a2)) {
        ok(("same input is stable: " + Join(a1)).c_str());
    } else {
        bad("unstable suggestions: " + Join(a1) + " vs " + Join(a2));
    }

    // EditDistance sanity: it is the whole basis of the ranking, so a couple of
    // known values pin it down (a broken DP would still pass most cases above).
    struct D {
        const char* a;
        const char* b;
        unsigned want;
    };
    static const D kD[] = {{"", "", 0}, {"abc", "abc", 0}, {"abc", "", 3}, {"", "abc", 3},
                           {"kitten", "sitting", 3},
                           // One substitution, not two: "flash"/"flass" differ
                           // only in the last letter. (A wrong expectation here
                           // is exactly what this table is for — it caught one.)
                           {"flash", "flass", 1}, {"shell", "sheel", 1},
                           {"pull", "push", 2}};
    int dbad = 0;
    for (const auto& d : kD) {
        unsigned got = adbsuggest::EditDistance(d.a, d.b);
        if (got != d.want) {
            bad("EditDistance(" + std::string(d.a) + ", " + d.b + ") = " + std::to_string(got) +
                ", want " + std::to_string(d.want));
            dbad++;
        }
    }
    if (dbad == 0) ok("EditDistance matches known values");

    printf("\n== suggest: %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
