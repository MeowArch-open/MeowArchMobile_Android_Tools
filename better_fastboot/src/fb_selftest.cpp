/*
 * fb_selftest.cpp — self-contained checks for the MeowArchMobile UX glue
 * (suggestion ranking, i18n table integrity, help-row round-trip). Built as
 * the `fastboot_selftest` target and run in CI; needs no device.
 */
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "fb_i18n.h"
#include "fb_suggest.h"
#include "fb_i18n_en_generated.h"

namespace {

int g_failures = 0;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
            g_failures++;                                                                          \
        }                                                                                          \
    } while (0)

std::vector<std::string> Sug(const std::string& in, fbsuggest::Kind k, size_t max = 2) {
    return fbsuggest::Suggest(in, k, max);
}

bool Has(const std::vector<std::string>& v, const std::string& want) {
    for (const auto& s : v)
        if (s == want) return true;
    return false;
}

void TestSuggest() {
    // The canonical typo from the goal: "flass" -> "flash".
    auto a = Sug("flass", fbsuggest::Kind::Command);
    CHECK(!a.empty() && a[0] == "flash", "flass -> flash (got %zu cands, first=%s)", a.size(),
          a.empty() ? "" : a[0].c_str());

    // Prefix affinity: "flas" offers the whole flash family, flash first. The
    // cap is deliberately wide so adding another flash-* command (flash-x
    // arrived this way) does not silently push an existing candidate out of a
    // two-slot window and fail the check for the wrong reason.
    auto b = Sug("flas", fbsuggest::Kind::Command, 6);
    CHECK(Has(b, "flash") && Has(b, "flashall"), "flas -> {flash,flashall}");
    CHECK(Has(b, "flash-x"), "flas -> {flash-x}");
    CHECK(!b.empty() && b[0] == "flash", "flas best is flash");

    // The canonical typo still resolves to exactly one candidate: flash-x is
    // distance 4 from "flass", past the tolerance, so it does not crowd in.
    auto a2 = Sug("flass", fbsuggest::Kind::Command, 6);
    CHECK(a2.size() == 1 && a2[0] == "flash", "flass -> only flash (got %zu)", a2.size());

    // Totally foreign word: no suggestion (must not hallucinate a match).
    auto c = Sug("xyzzy", fbsuggest::Kind::Command);
    CHECK(c.empty(), "xyzzy -> (none), got %zu", c.size());

    // Exact command: Suggest skips it (caller wouldn't ask), so not offered.
    auto d = Sug("flash", fbsuggest::Kind::Command);
    CHECK(!Has(d, "flash"), "flash does not suggest itself");

    // Option typos.
    auto e = Sug("--slott", fbsuggest::Kind::Option, 1);
    CHECK(!e.empty() && e[0] == "--slot", "--slott -> --slot");
    auto f = Sug("--dtbb", fbsuggest::Kind::Option, 1);
    CHECK(!f.empty() && f[0] == "--dtb", "--dtbb -> --dtb");

    // IsKnownCommand: real commands pass, format[:...] prefix passes, typos fail.
    CHECK(fbsuggest::IsKnownCommand("flash"), "flash is known");
    CHECK(fbsuggest::IsKnownCommand("format:ext4:1G"), "format:ext4:1G is known");
    CHECK(fbsuggest::IsKnownCommand("devices"), "devices is known");
    CHECK(!fbsuggest::IsKnownCommand("flass"), "flass is NOT known");
    CHECK(!fbsuggest::IsKnownCommand("formatted"), "formatted is NOT known");

    // EditDistance sanity.
    CHECK(fbsuggest::EditDistance("flash", "flass") == 1, "d(flash,flass)==1");
    CHECK(fbsuggest::EditDistance("", "abc") == 3, "d('',abc)==3");
    CHECK(fbsuggest::EditDistance("kitten", "sitting") == 3, "d(kitten,sitting)==3");
}

void TestI18n() {
    // English baseline: format specifiers in prologs must match upstream so
    // StringPrintf stays valid, and OKAY/FAILED stay verbatim.
    fbi18n::Init("en");
    CHECK(fbi18n::Locale() == "en", "locale en");
    CHECK(!strcmp(fbi18n::T(fbi18n::kOkay), "OKAY"), "en OKAY");
    CHECK(!strcmp(fbi18n::T(fbi18n::kFailed), "FAILED"), "en FAILED");
    CHECK(!strcmp(fbi18n::T(fbi18n::kSending), "Sending '%s' (%zu KB)"), "en kSending format");

    // Every non-English table must carry the SAME format specifiers in the
    // prolog/error strings as English (a mismatch would crash StringPrintf at
    // runtime). Check the specifier substring set on the key format IDs.
    const fbi18n::Msg fmt_ids[] = {fbi18n::kSending,      fbi18n::kSendingSparse,
                                   fbi18n::kFinished,     fbi18n::kUnknownCommand,
                                   fbi18n::kDidYouMean,   fbi18n::kFetching,
                                   fbi18n::kRebootingInto, fbi18n::kSettingSlot};
    auto spec_set = [](const char* s) {
        std::string out;
        for (const char* p = s; *p; p++) {
            if (*p == '%') {
                // capture the conversion: %[flags][width][.prec]conv or %zu
                const char* q = p + 1;
                while (*q && (strchr("-+ #0", *q) || (*q >= '0' && *q <= '9') || *q == '.')) q++;
                if (*q == 'z') q++;          // %zu
                if (*q) { out.push_back(*q); p = q; }
            }
        }
        return out;
    };

    size_t nlangs = 0;
    const fbi18n::LangInfo* langs = fbi18n::LangTable(&nlangs);
    CHECK(nlangs >= 4, "at least en/zh_CN/zh_TW/ja registered (got %zu)", nlangs);

    for (size_t li = 0; li < nlangs; li++) {
        fbi18n::Init(langs[li].code);
        for (auto id : fmt_ids) {
            fbi18n::Init("en");
            std::string en = spec_set(fbi18n::T(id));
            fbi18n::Init(langs[li].code);
            std::string lo = spec_set(fbi18n::T(id));
            CHECK(en == lo, "lang %s msg %d specifiers '%s' != en '%s'", langs[li].code, (int)id,
                  lo.c_str(), en.c_str());
        }
    }

    // zh_CN actually localizes the runtime strings (not silently English).
    fbi18n::Init("zh_CN");
    CHECK(fbi18n::Locale() == "zh_CN", "locale zh_CN");
    CHECK(strcmp(fbi18n::T(fbi18n::kUnknownCommand), "unknown command %s") != 0,
          "zh_CN kUnknownCommand is translated");
    CHECK(strstr(fbi18n::T(fbi18n::kUnknownCommand), "%s") != nullptr,
          "zh_CN kUnknownCommand keeps %%s");

    // Locale fallback: unknown code -> English; base-language fallback works.
    fbi18n::Init("fr_FR");
    CHECK(fbi18n::Locale() == "en", "fr_FR falls back to en");
    fbi18n::Init("zh");
    CHECK(fbi18n::Locale() == "zh_CN" || fbi18n::Locale() == "zh",
          "zh resolves to a Chinese table (got %s)", fbi18n::Locale().c_str());
}

void TestHelpRows() {
    // The generated help table must be well-formed: usage row first, every
    // Entry has a non-null command, and IDs (when set) are in range.
    CHECK(fbhelp::kHelpRowCount > 50, "help rows present (%zu)", fbhelp::kHelpRowCount);
    CHECK(fbhelp::kHelpRows[0].kind == fbhelp::Usage, "first row is usage");
    for (size_t i = 0; i < fbhelp::kHelpRowCount; i++) {
        const auto& r = fbhelp::kHelpRows[i];
        if (r.kind == fbhelp::Entry) {
            CHECK(r.a && r.a[0], "entry %zu has a command", i);
            CHECK(r.id >= 0 && r.id < fbi18n::kNumMessages, "entry %zu id in range", i);
        }
    }
}

}  // namespace

int main() {
    TestSuggest();
    TestI18n();
    TestHelpRows();
    if (g_failures == 0) {
        printf("fastboot_selftest: all checks passed\n");
        return 0;
    }
    printf("fastboot_selftest: %d check(s) FAILED\n", g_failures);
    return 1;
}
