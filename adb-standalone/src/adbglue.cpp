/*
 * adbglue.cpp — see adbglue.h. The integration layer the AOSP patches call.
 *
 * Same design constraint as better_fastboot's fbglue: the patches against the
 * AOSP tree stay mechanical one-line hooks, so every behavior lives here and
 * can be maintained without touching upstream code again.
 */
#include "adbglue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include <android-base/stringprintf.h>

#include "adb_completion.h"
#include "adb_help.h"
#include "adb_progress.h"
#include "adb_suggest.h"
#include "fb_color.h"

namespace adbglue {
namespace {

// The name the user actually typed, so the "run '<name> help'" hint matches
// their invocation (the package installs two: madb and adb-meowarch). Set from
// argv[0] in EarlyInit; the fallback keeps the hint sensible if it never ran.
std::string g_program_name = "madb";

// npos must not be compared with std::max: it is SIZE_MAX, so max() would pick
// the "absent" sentinel over a real separator and return the whole path.
// (Caught by `madb sheel` printing "Run './build-native/adb help'".)
std::string Basename(const std::string& path) {
    size_t slash = path.find_last_of('/');
    size_t bslash = path.find_last_of('\\');
    size_t cut = (slash == std::string::npos)   ? bslash
                 : (bslash == std::string::npos) ? slash
                                                 : std::max(slash, bslash);
    return (cut == std::string::npos) ? path : path.substr(cut + 1);
}

// Drop a trailing ".exe" so the Windows hint reads "Run 'adb help'", not
// "Run 'adb.exe help'" — the user typed neither spelling's extension.
std::string StripExe(std::string name) {
    if (name.size() > 4) {
        std::string tail = name.substr(name.size() - 4);
        for (auto& c : tail) c = (char)tolower((unsigned char)c);
        if (tail == ".exe") name.erase(name.size() - 4);
    }
    return name;
}

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

// Renders one hint line in yellow, matching better_fastboot's phrasing so the
// two tools read as one family.
void PrintHint(const std::string& text) {
    fprintf(stderr, "%s\n", fbcolor::Paint(fbcolor::C::Yellow, text).c_str());
}

}  // namespace

void MaybeHintUnknownCommand(const std::string& message) {
    // Upstream's only unrecognized-token path is this one message: adb's parser
    // `break`s out of its flag loop and treats whatever is left as a command,
    // so an unknown OPTION also arrives here (as "unknown command --slott").
    // Matching the English source text keeps the hook at the single error
    // funnel and leaves every other error alone.
    static const char kPrefix[] = "unknown command ";
    if (message.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) return;
    const std::string token = message.substr(sizeof(kPrefix) - 1);
    if (token.empty()) return;

    // A token starting with '-' can only be an option, so score it against the
    // option vocabulary; anything else is scored as a command. Guessing across
    // both would offer "-s" for a mistyped command, which is never useful.
    const bool is_option = token[0] == '-';
    auto cands = adbsuggest::Suggest(token, is_option ? adbsuggest::Kind::Option
                                                      : adbsuggest::Kind::Command,
                                     2);
    if (cands.empty()) return;

    if (cands.size() == 1) {
        PrintHint(android::base::StringPrintf(_(adbi18n::kDidYouMean), cands[0].c_str()));
    } else {
        PrintHint(android::base::StringPrintf(_(adbi18n::kDidYouMeanList),
                                              QuoteList(cands).c_str()));
    }
    PrintHint(fbcolor::Paint(fbcolor::C::Dim,
                             android::base::StringPrintf(_(adbi18n::kRunHelp),
                                                         g_program_name.c_str())));
}

int EarlyInit(int argc, char** argv) {
    if (argc > 0 && argv[0] && argv[0][0]) g_program_name = StripExe(Basename(argv[0]));
    // Scan for our own flags before anything else runs: getopt in adb's main()
    // rejects unknown options, and help/errors printed later must already know
    // the locale and color state.
    std::string lang_override;
    std::string completion_shell;
    std::string completion_command;
    bool color_forced_on = false, color_forced_off = false, progress_off = false;
    bool list_langs = false;
    for (int i = 1; i < argc; i++) {
        std::string a(argv[i]);
        if (a.rfind("--lang=", 0) == 0) lang_override = a.substr(7);
        else if (a == "--lang" && i + 1 < argc) lang_override = argv[++i];
        else if (a == "--list-langs") list_langs = true;
        else if (a == "--no-color") color_forced_off = true;
        else if (a == "--color") color_forced_on = true;
        else if (a == "--no-progress") progress_off = true;
        else if (a.rfind("--shell-completion=", 0) == 0) {
            completion_shell = a.substr(19);
            // An optional next token names the command the script should
            // complete for: the package installs the same binary under two
            // names (madb and adb-meowarch) and fish keys its rules on that
            // name, so one generation per alias is needed.
            if (i + 1 < argc && argv[i + 1][0] != '-') completion_command = argv[++i];
        }
        else if (a == "--shell-completion" && i + 1 < argc) completion_shell = argv[++i];
    }

    // Machine output: a script another program will source. Print it and exit
    // before any colour/locale decision can leak into it.
    if (!completion_shell.empty()) {
        // Prefer the explicit name; argv[0] may carry a build-tree path
        // ("./build-native/adb"), which is not what a shell will type.
        std::string name = !completion_command.empty() ? completion_command
                                                       : (argv[0] ? argv[0] : "madb");
        exit(ShellCompletion(completion_shell, name));
    }

    adbi18n::Init(lang_override);
    // Init() first, always: it discovers whether the terminal interprets ANSI
    // at all (and turns on Windows VT processing). Skipping it when --no-color
    // was given would leave escapes marked unavailable and silently downgrade
    // the progress bar.
    fbcolor::Init();
    if (color_forced_off) fbcolor::SetEnabled(false);
    else if (color_forced_on) fbcolor::SetEnabled(true);

    adbprogress::Init();
    if (progress_off) adbprogress::SetEnabled(false);

    if (list_langs) exit(ListLangs());

    // Drop our own flags from argv: adb's parser does not know them and would
    // report an unknown command. Compact in place, preserving argv[0] (the
    // program name) at index 0.
    int out = 1;
    for (int i = 1; i < argc; i++) {
        std::string a(argv[i]);
        bool drop = a == "--no-color" || a == "--color" || a == "--no-progress" ||
                    a == "--list-langs" || a.rfind("--lang=", 0) == 0 ||
                    a.rfind("--shell-completion=", 0) == 0;
        if (!drop && a == "--lang" && i + 1 < argc) {
            // consume the separate-argument form as well
            i++;
            drop = true;
        }
        if (!drop && a == "--shell-completion" && i + 1 < argc) {
            i++;
            drop = true;
        }
        if (!drop) argv[out++] = argv[i];
    }
    argv[out] = nullptr;
    return out;
}

int ListLangs() {
    size_t count = 0;
    const adbi18n::LangInfo* langs = adbi18n::LangTable(&count);
    printf("%s\n", _(adbi18n::kLangsHeader));
    for (size_t i = 0; i < count; i++) {
        bool current = langs[i].code == adbi18n::Locale();
        printf("  %-8s %s%s\n", langs[i].code, langs[i].native,
               current ? _(adbi18n::kLangCurrent) : "");
    }
    return 0;
}

int ShowHelp(const std::string& version) {
    // Help goes to stdout, so colour has to follow stdout's terminal state.
    fbcolor::InitForStdout();
    return adbhelp::ShowHelp(version.c_str());
}

int ShowHelpSection(const std::string& version, const char* section) {
    fbcolor::InitForStdout();
    return adbhelp::ShowSection(version.c_str(), section);
}

int ListHelpSections() { return adbhelp::ListSections(); }

int ShellCompletion(const std::string& shell, const std::string& command) {
    std::string script = adbcompletion::Generate(shell, command);
    if (script.empty()) {
        fprintf(stderr, "%sunknown shell '%s' (supported: %s)\n",
                fbcolor::Paint(fbcolor::C::Red, "error: ").c_str(), shell.c_str(),
                adbcompletion::SupportedShells());
        return 2;
    }
    fwrite(script.data(), 1, script.size(), stdout);
    return 0;
}

bool IsHelpSection(const char* name) { return adbhelp::IsSection(name); }

void ProgressArm(const std::string& label, uint64_t total) { adbprogress::Arm(label, total); }
void ProgressSetLabel(const std::string& label) { adbprogress::SetLabel(label); }
void ProgressUpdate(uint64_t done) { adbprogress::Update(done); }
void ProgressFinish() { adbprogress::Finish(); }
void ProgressArmPercent(const std::string& label) { adbprogress::ArmPercent(label); }
void ProgressUpdatePercent(int pct) { adbprogress::UpdatePercent(pct); }
bool ProgressEnabled() { return adbprogress::Enabled(); }

}  // namespace adbglue
