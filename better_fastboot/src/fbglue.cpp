/*
 * fbglue.cpp — see fbglue.h. The integration layer the AOSP patches call.
 *
 * Design constraint: the patches against system/core are kept minimal and
 * mechanical (a handful of one-line hook calls), so every behavior lives here
 * and can be maintained without touching upstream code again.
 */
#include "fbglue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>

#include "fb_completion.h"
#include "fb_help.h"
#include "fb_progress.h"
#include "fb_suggest.h"

using android::base::StringPrintf;

namespace fbglue {
namespace {

// Short name used in messages the completion generator emits. The installed
// commands are /usr/bin/fastboot-m and its long alias fastboot-meowarch.
constexpr const char* kProgramName = "fastboot-m";

// Join suggestion list: "'a', 'b'" — quotes added per candidate.
std::string QuoteList(const std::vector<std::string>& cands) {
    std::string out;
    for (size_t i = 0; i < cands.size(); i++) {
        if (i) out += ", ";
        out += "'";
        out += cands[i];
        out += "'";
    }
    return out;
}

// "Did you want 'flash'?" / "Did you want one of: ...?" — or nothing when
// there is no close candidate (a completely foreign word gets no guess).
void PrintCommandSuggestion(const std::string& command) {
    auto cands = fbsuggest::Suggest(command, fbsuggest::Kind::Command, 2);
    if (cands.size() == 1) {
        fprintf(stderr, "%s\n",
                fbcolor::Paint(fbcolor::C::Yellow,
                               StringPrintf(_(fbi18n::kDidYouMean), cands[0].c_str()))
                        .c_str());
    } else if (cands.size() > 1) {
        fprintf(stderr, "%s\n",
                fbcolor::Paint(fbcolor::C::Yellow,
                               StringPrintf(_(fbi18n::kDidYouMeanList),
                                            QuoteList(cands).c_str()))
                        .c_str());
    }
}

// Find the argv token that caused getopt's '?': the last "--..." token at or
// before optind-1.
std::string LastLongOption(int argc, char** argv, int optind_after) {
    for (int i = std::min(optind_after - 1, argc - 1); i >= 0; i--) {
        if (argv[i] && strncmp(argv[i], "--", 2) == 0) return argv[i];
    }
    return "";
}

}  // namespace

void EarlyInit(int argc, char** argv) {
    // Scan for our own flags before anything else runs, since getopt will die
    // on unknown options and help/errors printed later must already know the
    // locale and color state.
    std::string lang_override;
    bool color_forced_on = false, color_forced_off = false, progress_off = false;
    bool list_langs = false;
    std::string completion_shell;
    for (int i = 1; i < argc; i++) {
        std::string a(argv[i]);
        if (a.rfind("--lang=", 0) == 0) lang_override = a.substr(7);
        else if (a == "--lang" && i + 1 < argc) lang_override = argv[++i];
        else if (a == "--list-langs") list_langs = true;
        else if (a == "--no-color") color_forced_off = true;
        else if (a == "--color") color_forced_on = true;
        else if (a == "--no-progress") progress_off = true;
        else if (a.rfind("--shell-completion=", 0) == 0) completion_shell = a.substr(19);
        else if (a == "--shell-completion" && i + 1 < argc) completion_shell = argv[++i];
    }

    fbi18n::Init(lang_override);
    // Init() first, always: it is what discovers whether the terminal
    // interprets ANSI at all (and turns on Windows VT processing). Skipping it
    // when --no-color was given used to leave escapes marked unavailable, which
    // silently downgraded the progress bar to its plain fallback.
    fbcolor::Init();
    if (color_forced_off) fbcolor::SetEnabled(false);
    else if (color_forced_on) fbcolor::SetEnabled(true);
    fbprogress::Init();
    if (progress_off) fbprogress::SetEnabled(false);

    if (list_langs) {
        exit(ListLangs());
    }

    // Completion-script generation for packagers. Deliberately absent from
    // --help (keeps the upstream byte-parity guarantee); documented in
    // README.md. Machine-oriented output: exactly the script on stdout,
    // nothing else — no colors, no localization, exit 0/1.
    if (!completion_shell.empty()) {
        std::string script = fbcompletion::Generate(completion_shell);
        if (script.empty()) {
            fprintf(stderr, "%s: error: unknown shell '%s' (want: bash, zsh, fish)\n",
                    kProgramName, completion_shell.c_str());
            exit(1);
        }
        fputs(script.c_str(), stdout);
        exit(0);
    }
}

int ListLangs() {
    size_t count = 0;
    const fbi18n::LangInfo* langs = fbi18n::LangTable(&count);
    printf("%s\n", _(fbi18n::kLangsHeader));
    for (size_t i = 0; i < count; i++) {
        bool current = langs[i].code == fbi18n::Locale();
        printf("  %-8s %s%s\n", langs[i].code, langs[i].native,
               current ? _(fbi18n::kLangCurrent) : "");
    }
    return 0;
}
void UnknownCommandError(const std::string& command) {
    // Match upstream's syntax_error channel and exit code, but replace the
    // terse message with error + suggestion + help pointer.
    fprintf(stderr, "%s%s%s\n", fbcolor::Paint(fbcolor::C::Red, _(fbi18n::kErrPrefix)).c_str(),
            StringPrintf(_(fbi18n::kUnknownCommand), command.c_str()).c_str(), "");
    PrintCommandSuggestion(command);
    fprintf(stderr, "%s\n", fbcolor::Paint(fbcolor::C::Dim, _(fbi18n::kRunHelp)).c_str());
    exit(1);
}

void ValidateFirstCommand(const std::string& command) {
    // Anything the dispatch chain or the pre-dispatch short-circuits accept is
    // passed through; only an outright-unknown token errors early. `format`
    // takes an optional ":type:size" suffix, so match on the prefix.
    if (fbsuggest::IsKnownCommand(command)) return;
    UnknownCommandError(command);
}

void NoCommandError() {
    fprintf(stderr, "%s%s%s\n", fbcolor::Paint(fbcolor::C::Red, _(fbi18n::kErrPrefix)).c_str(),
            _(fbi18n::kNoCommand), "");
    fprintf(stderr, "%s\n", fbcolor::Paint(fbcolor::C::Dim, _(fbi18n::kRunHelp)).c_str());
    exit(1);
}

void UnknownOptionHint(int argc, char** argv, int optind_after) {
    std::string tok = LastLongOption(argc, argv, optind_after);
    if (tok.empty() || tok.size() <= 2) return;
    auto cands = fbsuggest::Suggest(tok, fbsuggest::Kind::Option, 1);
    if (cands.empty()) return;
    fprintf(stderr, "%s\n",
            fbcolor::Paint(fbcolor::C::Yellow,
                           StringPrintf(_(fbi18n::kDidYouMean), cands[0].c_str()))
                    .c_str());
    fprintf(stderr, "%s\n", fbcolor::Paint(fbcolor::C::Dim, _(fbi18n::kRunHelp)).c_str());
}

}  // namespace fbglue
