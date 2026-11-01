/*
 * fb_help.cpp — see fb_help.h. Renders the generated row table with upstream's
 * exact layout:
 *
 *   " " + cmd.ljust(27) + desc-first-line      (cmd shorter than the column)
 *   " " + cmd + "\n" + 28sp + desc lines       (cmd fills/overflows column)
 *   continuation lines: 28 spaces + text
 *
 * When a row carries a message ID and the active language provides text, the
 * entry is rendered from the translated "cmd\tdesc" string instead (commands
 * themselves stay English — they are the literal tokens the tool accepts).
 */
#include "fb_help.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "fb_color.h"
#include "fb_i18n.h"
#include "fb_i18n_en_generated.h"

namespace fbhelp {
namespace {

constexpr size_t kCmdCol = 27;   // command field width (upstream: col 1..28)
constexpr size_t kDescCol = 28;  // description starts at column 29 (0-based 28)

// Split a translated "cmd\tdesc" entry. Returns false when the string has no
// tab (then callers use the generated English row text).
bool SplitEntry(const char* s, std::string* cmd, std::string* desc) {
    const char* tab = strchr(s, '\t');
    if (!tab) return false;
    cmd->assign(s, tab);
    desc->assign(tab + 1);
    return true;
}

void SplitLines(const std::string& desc, std::vector<std::string>* out) {
    size_t start = 0;
    while (true) {
        size_t nl = desc.find('\n', start);
        std::string line = desc.substr(start, nl == std::string::npos ? std::string::npos
                                                                      : nl - start);
        // Translated tables may prefix continuation lines with '\t'; strip it.
        if (!line.empty() && line[0] == '\t') line.erase(0, 1);
        out->push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
}

void PrintEntry(const HelpRow& row) {
    std::string cmd, desc;
    if (row.id != 0) {
        const char* t = fbi18n::T((fbi18n::Msg)row.id);
        if (t && t[0] && SplitEntry(t, &cmd, &desc)) {
            // use translated
        } else {
            cmd = row.a ? row.a : "";
            desc = row.b ? row.b : "";
        }
    } else {
        cmd = row.a ? row.a : "";
        desc = row.b ? row.b : "";
    }

    std::vector<std::string> lines;
    SplitLines(desc, &lines);

    std::string painted_cmd = fbcolor::Paint(fbcolor::C::Cyan, cmd);
    if (cmd.size() >= kCmdCol) {
        // Overflow: command alone on its line, description below.
        fprintf(stdout, " %s\n", painted_cmd.c_str());
        for (const auto& l : lines) {
            fprintf(stdout, "%*s%s\n", (int)kDescCol, "", l.c_str());
        }
    } else {
        // Pad the RAW command (not the painted one — escapes would skew %-Ns).
        std::string pad = cmd + std::string(kCmdCol - cmd.size(), ' ');
        std::string painted_pad = fbcolor::Paint(fbcolor::C::Cyan, pad);
        const char* first = lines.empty() ? "" : lines[0].c_str();
        fprintf(stdout, " %s%s\n", painted_pad.c_str(), first);
        for (size_t i = 1; i < lines.size(); i++) {
            fprintf(stdout, "%*s%s\n", (int)kDescCol, "", lines[i].c_str());
        }
    }
}

}  // namespace

int ShowHelp() {
    for (size_t i = 0; i < kHelpRowCount; i++) {
        const HelpRow& row = kHelpRows[i];
        switch (row.kind) {
            case Usage: {
                const char* text = row.a;
                if (row.id != 0) {
                    const char* t = fbi18n::T((fbi18n::Msg)row.id);
                    if (t && t[0]) text = t;
                }
                fprintf(stdout, "%s\n", fbcolor::Paint(fbcolor::C::Bold, text).c_str());
                break;
            }
            case Blank:
                fprintf(stdout, "\n");
                break;
            case Section: {
                const char* text = row.a;
                if (row.id != 0) {
                    const char* t = fbi18n::T((fbi18n::Msg)row.id);
                    if (t && t[0]) text = t;
                }
                fprintf(stdout, "%s\n", fbcolor::Paint(fbcolor::C::Bold, text).c_str());
                break;
            }
            case Raw:
                fprintf(stdout, "%s\n", row.a ? row.a : "");
                break;
            case Entry:
                PrintEntry(row);
                break;
        }
    }
    // Our extra options (appended to upstream's list, translated like the rest).
    size_t nextras = 0;
    const ExtraOption* extras = ExtraOptions(&nextras);
    for (size_t ei = 0; ei < nextras; ei++) {
        const ExtraOption& e = extras[ei];
        HelpRow row{Entry, (int)e.id, nullptr, nullptr};
        const char* t = fbi18n::T(e.id);
        if (t && t[0]) {
            // PrintEntry splits "cmd\tdesc" from T(id) itself.
            PrintEntry(row);
        } else {
            std::string cmd, desc;
            SplitEntry(e.en, &cmd, &desc);  // English fallback
            HelpRow en{Entry, 0, cmd.c_str(), desc.c_str()};
            PrintEntry(en);
        }
    }
    return 0;
}

const ExtraOption* ExtraOptions(size_t* count) {
    // Single source of truth for the MeowArchMobile-only options: --help
    // renders these and the completion generator (fb_completion.cpp) reads
    // their descriptions from the same table.
    static const ExtraOption kExtras[] = {
        // flash-x is a command, but it has no row in the generated upstream
        // table (that table is transcribed from upstream's show_help(), which
        // stays byte-identical), so it is rendered from the same extras list
        // --help already appends our options from.
        {"flash-x",
         "flash-x PARTITION PAYLOAD [NAME]\tExtract PARTITION from an OTA\n"
         "\tpayload.bin and flash it. NAME names the entry inside\n"
         "\tthe payload; it defaults to PARTITION with any slot\n"
         "\tsuffix removed.",
         fbi18n::kHFlashX},
        {"--lang", "--lang=CODE\tSet interface language (see --list-langs).", fbi18n::kOLang},
        {"--list-langs", "--list-langs\tList available interface languages and exit.",
         fbi18n::kOListLangs},
        {"--no-color", "--no-color\tDisable colored output (also NO_COLOR=1).", fbi18n::kONoColor},
        {"--color", "--color\tForce colored output even when not a tty.", fbi18n::kOColor},
        {"--no-progress",
         "--no-progress\tDisable the transfer progress bar (also NO_PROGRESS=1).",
         fbi18n::kONoProgress},
    };
    *count = sizeof(kExtras) / sizeof(kExtras[0]);
    return kExtras;
}

}  // namespace fbhelp
