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

#include <string>

#include "adb_completion.h"
#include "adb_help.h"
#include "adb_progress.h"
#include "fb_color.h"

namespace adbglue {

int EarlyInit(int argc, char** argv) {
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
            // names (adb-m and adb-meowarch) and fish keys its rules on that
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
                                                       : (argv[0] ? argv[0] : "adb-m");
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
