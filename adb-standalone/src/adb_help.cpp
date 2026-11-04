/*
 * adb_help.cpp — see adb_help.h. MeowArchMobile glue, not an AOSP file.
 *
 * The table below is the single source of truth for `adb help`: section
 * headers, then entries as {section, entry} message IDs. English text comes
 * from adb_i18n_en.cpp, so the wording matches upstream; the layout is what
 * changes (aligned columns, colours, per-section output).
 */
#include "adb_help.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "fb_color.h"

namespace adbhelp {
namespace {

using adbi18n::Msg;

// Kind of content in a section body.
enum class Body {
    kEntries,   // table entries, printed in the command column
    kOptions,   // the global-options block, verbatim
    kEnv,       // the environment-variables block, verbatim
};

struct Section {
    Msg title;
    const char* keyword;  // what `adb help <keyword>` accepts, always English
    Body body;
};

// Order matches upstream's help().
const Section kSections[] = {
    {adbi18n::kSecGlobalOpts, "options", Body::kOptions},
    {adbi18n::kSecGeneral, "general", Body::kEntries},
    {adbi18n::kSecNetworking, "networking", Body::kEntries},
    {adbi18n::kSecFileTransfer, "transfer", Body::kEntries},
    {adbi18n::kSecShell, "shell", Body::kEntries},
    {adbi18n::kSecInstall, "install", Body::kEntries},
    {adbi18n::kSecDebugging, "debugging", Body::kEntries},
    {adbi18n::kSecSecurity, "security", Body::kEntries},
    {adbi18n::kSecScripting, "scripting", Body::kEntries},
    {adbi18n::kSecInternal, "internal", Body::kEntries},
    {adbi18n::kSecUsb, "usb", Body::kEntries},
    {adbi18n::kSecEnvironment, "environment", Body::kEnv},
    {adbi18n::kSecMeowArch, "meowarch", Body::kEntries},
};
constexpr size_t kSectionCount = sizeof(kSections) / sizeof(kSections[0]);

// Entries per section, in the order upstream prints them.
struct EntryRow {
    Msg section;
    Msg entry;
};
const EntryRow kEntries[] = {
    {adbi18n::kSecGeneral, adbi18n::kHDevices},
    {adbi18n::kSecGeneral, adbi18n::kHHelp},
    {adbi18n::kSecGeneral, adbi18n::kHVersion},

    {adbi18n::kSecNetworking, adbi18n::kHConnect},
    {adbi18n::kSecNetworking, adbi18n::kHDisconnect},
    {adbi18n::kSecNetworking, adbi18n::kHPair},
    {adbi18n::kSecNetworking, adbi18n::kHForward},
    {adbi18n::kSecNetworking, adbi18n::kHReverse},
    {adbi18n::kSecNetworking, adbi18n::kHMdns},

    {adbi18n::kSecFileTransfer, adbi18n::kHPush},
    {adbi18n::kSecFileTransfer, adbi18n::kHPull},
    {adbi18n::kSecFileTransfer, adbi18n::kHSync},

    {adbi18n::kSecShell, adbi18n::kHShell},
    {adbi18n::kSecShell, adbi18n::kHEmu},

    {adbi18n::kSecInstall, adbi18n::kHInstall},
    {adbi18n::kSecInstall, adbi18n::kHInstallMultiple},
    {adbi18n::kSecInstall, adbi18n::kHInstallMultiPackage},
    {adbi18n::kSecInstall, adbi18n::kHUninstall},

    {adbi18n::kSecDebugging, adbi18n::kHBugreport},
    {adbi18n::kSecDebugging, adbi18n::kHJdwp},
    {adbi18n::kSecDebugging, adbi18n::kHLogcat},

    {adbi18n::kSecSecurity, adbi18n::kHDisableVerity},
    {adbi18n::kSecSecurity, adbi18n::kHEnableVerity},
    {adbi18n::kSecSecurity, adbi18n::kHKeygen},
    {adbi18n::kSecSecurity, adbi18n::kHRoot},

    {adbi18n::kSecScripting, adbi18n::kHWaitFor},
    {adbi18n::kSecScripting, adbi18n::kHGetState},
    {adbi18n::kSecScripting, adbi18n::kHGetSerialNo},
    {adbi18n::kSecScripting, adbi18n::kHGetDevpath},
    {adbi18n::kSecScripting, adbi18n::kHRemount},
    {adbi18n::kSecScripting, adbi18n::kHReboot},
    {adbi18n::kSecScripting, adbi18n::kHSideload},
    {adbi18n::kSecScripting, adbi18n::kHUnroot},
    {adbi18n::kSecScripting, adbi18n::kHUsbRestart},
    {adbi18n::kSecScripting, adbi18n::kHTcpip},

    {adbi18n::kSecInternal, adbi18n::kHStartServer},
    {adbi18n::kSecInternal, adbi18n::kHKillServer},
    {adbi18n::kSecInternal, adbi18n::kHReconnect},

    {adbi18n::kSecMeowArch, adbi18n::kOLang},
    {adbi18n::kSecMeowArch, adbi18n::kOListLangs},
    {adbi18n::kSecMeowArch, adbi18n::kONoColor},
    {adbi18n::kSecMeowArch, adbi18n::kOColor},
    {adbi18n::kSecMeowArch, adbi18n::kONoProgress},
    {adbi18n::kSecUsb, adbi18n::kHAttach},
    {adbi18n::kSecUsb, adbi18n::kHDetach},
};

// Global options stay in English: they are flag spellings with short English
// glosses, and a mistranslated flag description is worse than an English one.
// (Same reasoning as platform-tools, which ships them untranslated.)
const char* kGlobalOptions =
    " -a                       listen on all network interfaces, not just localhost\n"
    " -d                       use USB device (error if multiple devices connected)\n"
    " -e                       use TCP/IP device (error if multiple TCP/IP devices available)\n"
    " -s SERIAL                use device with given serial (overrides $ANDROID_SERIAL)\n"
    " -t ID                    use device with given transport id\n"
    " -H                       name of adb server host [default=localhost]\n"
    " -P                       port of adb server [default=5037]\n"
    " -L SOCKET                listen on given socket for adb server\n"
    "                          [default=tcp:localhost:5037]\n"
    " --one-device SERIAL|USB  only allowed with 'start-server' or 'server nodaemon'\n"
    " --exit-on-write-error    exit if stdout is closed\n";

const char* kEnvironment =
    " $ADB_TRACE\n"
    "     comma/space separated list of debug info to log:\n"
    "     all,adb,sockets,packets,rwx,usb,sync,sysdeps,transport,jdwp,services,auth,"
    "fdevent,shell,incremental\n"
    " $ADB_VENDOR_KEYS         colon-separated list of keys (files or directories)\n"
    " $ANDROID_SERIAL          serial number to connect to (see -s)\n"
    " $ANDROID_LOG_TAGS        tags to be used by logcat (see logcat --help)\n"
    " $ADB_LOCAL_TRANSPORT_MAX_PORT max emulator scan port (default 5585, 16 emus)\n"
    " $ADB_MDNS_AUTO_CONNECT   comma-separated list of mdns services to allow auto-connect\n"
    "\n"
    " MeowArchMobile additions:\n"
    " $NO_PROGRESS             disable the transfer progress bar (same as --no-progress)\n"
    " $NO_COLOR                disable colored output (same as --no-color)\n"
    " $ADB_SYNC_LIST_WINDOW    directories listed per round trip when pulling a\n"
    "                          directory [default=256, max=4096; 1 is upstream's\n"
    "                          sequential behaviour]\n"
    " $ADB_SYNC_RECV_WINDOW    small files fetched per round trip when pulling a\n"
    "                          directory [default=512, max=1024]\n"
    " $ADB_SYNC_STATS          print sync request/batch statistics to stderr\n";

const char* kDocsLine =
    "Online documentation: https://android.googlesource.com/platform/packages/modules/adb/"
    "+/refs/heads/main/docs/user/adb.1.md";

// Command column: upstream's longest cell is "wait-for[-TRANSPORT]-STATE..."
// (28 columns); descriptions start at 2 + 28 + 1.
constexpr size_t kCmdCol = 28;
constexpr size_t kIndent = 2;

bool SameIgnoreCase(const char* a, const char* b) {
    if (!a || !b) return false;
    for (; *a && *b; a++, b++) {
        char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a - 'A' + 'a') : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b - 'A' + 'a') : *b;
        if (ca != cb) return false;
    }
    return *a == *b;
}

// Splits "cmd\tdesc" out of a table entry.
bool SplitEntry(const char* s, std::string* cmd, std::string* desc) {
    const char* tab = strchr(s, '\t');
    if (!tab) return false;
    cmd->assign(s, tab);
    desc->assign(tab + 1);
    return true;
}

// Prints every line of |desc| indented to |first_col|. Callers use it for the
// continuation lines of an entry (the first line is printed beside the command).
void PrintDescLines(const std::string& desc, size_t first_col) {
    size_t start = 0;
    while (true) {
        size_t nl = desc.find('\n', start);
        std::string line =
            desc.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        // Translated tables prefix continuation lines with '\t' (the marker
        // that keeps them distinct from real tabs in "cmd\tdesc").
        if (!line.empty() && line[0] == '\t') line.erase(0, 1);
        fprintf(stdout, "%*s%s\n", (int)first_col, "", line.c_str());
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
}

void PrintEntry(Msg id) {
    const char* text = adbi18n::T(id);
    if (!text || !text[0]) return;
    std::string cmd, desc;
    const size_t desc_col = kIndent + kCmdCol + 1;
    if (!SplitEntry(text, &cmd, &desc)) {
        fprintf(stdout, "%*s%s\n", (int)kIndent, "", text);
        return;
    }
    if (cmd.size() >= kCmdCol) {
        fprintf(stdout, "%*s%s\n", (int)kIndent, "",
                fbcolor::Paint(fbcolor::C::Cyan, cmd).c_str());
        PrintDescLines(desc, desc_col);
        return;
    }
    // Pad the RAW cell (escape bytes would skew the width), then paint it.
    // Only the first description line sits beside the command; continuation
    // lines (entries carry "\n\t" inside the string) line up under it.
    std::string pad = cmd + std::string(kCmdCol - cmd.size(), ' ');
    size_t nl = desc.find('\n');
    std::string first = nl == std::string::npos ? desc : desc.substr(0, nl);
    fprintf(stdout, "%*s%s  %s\n", (int)kIndent, "",
            fbcolor::Paint(fbcolor::C::Cyan, pad).c_str(), first.c_str());
    if (nl != std::string::npos) PrintDescLines(desc.substr(nl + 1), desc_col);
}

void PrintHeader(Msg id) {
    fprintf(stdout, "%s\n", fbcolor::Paint(fbcolor::C::Bold, adbi18n::T(id)).c_str());
}

void PrintBody(const Section& s) {
    switch (s.body) {
        case Body::kOptions:
            fprintf(stdout, "%s", kGlobalOptions);
            break;
        case Body::kEnv:
            fprintf(stdout, "%s", kEnvironment);
            break;
        case Body::kEntries:
            for (const auto& e : kEntries) {
                if (e.section == s.title) PrintEntry(e.entry);
            }
            break;
    }
}

Msg SectionFor(const char* name) {
    if (!name || !name[0]) return adbi18n::kNone;
    for (const auto& s : kSections) {
        if (SameIgnoreCase(name, s.keyword)) return s.title;
        // Accept the localized section name too (minus its trailing colon).
        std::string localized = adbi18n::T(s.title);
        if (!localized.empty() && localized.back() == ':') localized.pop_back();
        if (SameIgnoreCase(name, localized.c_str())) return s.title;
    }
    return adbi18n::kNone;
}

// "Android Debug Bridge version <v>", substituted by hand so a translated
// format with a different word order still works.
void PrintVersionLine(const char* version) {
    if (!version || !version[0]) return;
    std::string line = std::string(adbi18n::T(adbi18n::kHelpUsage));
    size_t pos = line.find("%s");
    if (pos != std::string::npos) line.replace(pos, 2, version);
    fprintf(stdout, "%s\n\n", fbcolor::Paint(fbcolor::C::Bold, line).c_str());
}

}  // namespace

bool IsSection(const char* name) { return SectionFor(name) != adbi18n::kNone; }

int ListSections() {
    for (const auto& s : kSections) fprintf(stdout, "%s\n", s.keyword);
    return 0;
}

int ShowHelp(const char* version) {
    PrintVersionLine(version);
    for (size_t i = 0; i < kSectionCount; i++) {
        PrintHeader(kSections[i].title);
        PrintBody(kSections[i]);
        fprintf(stdout, "\n");
    }
    fprintf(stdout, "%s\n", kDocsLine);
    return 0;
}

int ShowSection(const char* version, const char* name) {
    Msg title = SectionFor(name);
    if (title == adbi18n::kNone) {
        char msg[512];
        snprintf(msg, sizeof(msg), adbi18n::T(adbi18n::kSecUnknown), name);
        fprintf(stderr, "%s%s\n", adbi18n::T(adbi18n::kErrPrefix), msg);
        return 1;
    }
    PrintVersionLine(version);
    for (const auto& s : kSections) {
        if (s.title != title) continue;
        PrintHeader(s.title);
        PrintBody(s);
        break;
    }
    return 0;
}

// --------------------------------------------------------------------------
// Accessors for the table. The completion generator builds its vocabulary from
// these, so `adb help` and what the shell completes cannot drift apart.

namespace {

// Split one entry's English text ("cmd cell\tdescription") into a bare command
// name, its argument cell and the description. Returns false when the entry has
// no tab (a header-ish line) or is empty.
bool ParseEntry(const char* text, std::string* name, std::string* args,
                std::string* desc) {
    if (!text || !*text) return false;
    std::string s(text);
    size_t tab = s.find('\t');
    if (tab == std::string::npos) return false;
    std::string cmd = s.substr(0, tab);
    *desc = s.substr(tab + 1);
    // A newline inside the description continues it on the next line; keep the
    // first line only for a completion description.
    size_t nl = desc->find('\n');
    if (nl != std::string::npos) *desc = desc->substr(0, nl);

    size_t sp = cmd.find_first_of(" \t");
    if (sp == std::string::npos) {
        *name = cmd;
        *args = "";
    } else {
        *name = cmd.substr(0, sp);
        *args = cmd.substr(sp + 1);
    }
    // Normalize an option to its bare spelling: "--lang=CODE" -> "--lang",
    // "--set-active[=SLOT]" -> "--set-active".
    size_t cut = name->find_first_of("=[<");
    if (cut != std::string::npos) *name = name->substr(0, cut);
    return !name->empty();
}

const char* SectionKeyword(Msg section) {
    for (const auto& s : kSections) {
        if (s.title == section) return s.keyword;
    }
    return "";
}

}  // namespace

std::vector<CommandInfo> Commands() {
    std::vector<CommandInfo> out;
    for (const auto& row : kEntries) {
        const char* text = adbi18n::T(row.entry);
        std::string name, args, desc;
        if (!ParseEntry(text, &name, &args, &desc)) continue;
        if (name[0] == '-') continue;  // an option, not a command
        CommandInfo ci;
        ci.name = name;
        ci.args = args;
        ci.desc = desc;
        ci.section = SectionKeyword(row.section);
        out.push_back(std::move(ci));
    }
    return out;
}

std::vector<std::string> Options() {
    std::vector<std::string> out;
    // The five MeowArchMobile options are table entries.
    for (const auto& row : kEntries) {
        std::string name, args, desc;
        if (!ParseEntry(adbi18n::T(row.entry), &name, &args, &desc)) continue;
        if (name.size() > 2 && name.rfind("--", 0) == 0) out.push_back(name);
    }
    // Plus upstream's short flags, which are documented in the options block
    // rather than as entries. These are spellings, never translated.
    static const char* kShort[] = {"-a", "-d", "-e", "-s", "-t", "-H", "-P", "-L", "-h"};
    for (const char* f : kShort) out.push_back(f);
    return out;
}

std::vector<std::string> SectionKeywords() {
    std::vector<std::string> out;
    for (const auto& s : kSections) out.push_back(s.keyword);
    return out;
}

}  // namespace adbhelp