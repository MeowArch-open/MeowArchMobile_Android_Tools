/*
 * fb_completion.cpp — see fb_completion.h.
 *
 * `fastboot --shell-completion=bash|zsh|fish` prints a ready-to-source
 * completion script. Generating it from the binary keeps the vocabulary in
 * lockstep with what the binary actually accepts: commands and long options
 * come from fbsuggest's tables (the same source of truth behind Did-you-want),
 * descriptions come from the generated upstream help rows, and the language
 * list comes from the compiled i18n registry. Package builds call this once
 * per shell and install the output into the standard vendor dirs.
 *
 * Completion model (all three shells):
 *   * before any command: commands + options
 *   * cur starting with '-': options (short + long)
 *   * option values: -s -> live `devices` serials, --slot/-a/--set-active ->
 *     slots, --lang -> compiled-in languages, --fs-options -> feature names
 *     (also in the --opt=value form)
 *   * per-command arguments: partitions (flash/erase/format/fetch), getvar
 *     variable names, reboot targets, flashing/gsi/snapshot-update
 *     subcommands, and *.img / *.zip file filters where upstream's own
 *     bash completion had them
 */
#include "fb_completion.h"

#include <string.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "fb_help.h"
#include "fb_i18n.h"
#include "fb_i18n_en_generated.h"  // fbhelp::kHelpRows (upstream help text)
#include "fb_suggest.h"

namespace fbcompletion {
namespace {

// ---------------------------------------------------------------- vocabulary

// Deduplicated command list, in fbsuggest's order (dispatch order-ish).
std::vector<std::string> CommandList() {
    std::vector<std::string> out;
    for (const auto& c : fbsuggest::Commands()) {
        if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
    }
    return out;
}

// First token of a help "cmd args" cell, normalized to the bare command /
// option name: "format[:FS_TYPE[:SIZE]] PARTITION" -> "format",
// "--set-active[=SLOT]" -> "--set-active", "--lang=CODE" -> "--lang".
std::string NormKey(const std::string& a) {
    size_t sp = a.find_first_of(" \t");
    std::string tok = (sp == std::string::npos) ? a : a.substr(0, sp);
    size_t br = tok.find('[');
    if (br != std::string::npos) tok = tok.substr(0, br);
    size_t eq = tok.find('=');
    if (eq != std::string::npos) tok = tok.substr(0, eq);
    return tok;
}

std::string FirstLine(const std::string& b) {
    size_t nl = b.find('\n');
    return nl == std::string::npos ? b : b.substr(0, nl);
}

// key -> one-line description, from the generated upstream help rows plus our
// five MeowArchMobile-only options (fb_help::ExtraOptions — same source
// --help renders, so the two can never drift apart).
std::string DescFor(const std::string& key) {
    for (size_t i = 0; i < fbhelp::kHelpRowCount; i++) {
        const auto& r = fbhelp::kHelpRows[i];
        if (r.kind != fbhelp::Entry || !r.a || !r.b) continue;
        if (NormKey(r.a) == key) return FirstLine(r.b);
    }
    size_t nextras = 0;
    const fbhelp::ExtraOption* extras = fbhelp::ExtraOptions(&nextras);
    for (size_t i = 0; i < nextras; i++) {
        if (extras[i].key != key) continue;
        // en text is "cmd\tdesc"; take the desc's first line.
        const char* tab = strchr(extras[i].en, '\t');
        if (!tab) continue;
        return FirstLine(tab + 1);
    }
    return "";
}

std::string LangList() {
    size_t n = 0;
    const fbi18n::LangInfo* langs = fbi18n::LangTable(&n);
    std::string out;
    for (size_t i = 0; i < n; i++) {
        if (i) out += " ";
        out += langs[i].code;
    }
    return out;
}

std::string Join(const std::vector<std::string>& v, const char* sep = " ") {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

std::string OptionsList() { return Join(fbsuggest::Options()); }

// fish single-quoted string escaping.
std::string FishQuote(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\' || c == '\'') out += '\\';
        out += c;
    }
    return out;
}

// ---------------------------------------------------------------- word lists

const char* kShortOpts = "-a -h -l -s -S -v -w";
// Options that consume the next argv token (command-position scanner skips
// them and their value).
const char* kValueOpts =
    "-s -S --slot --lang --base --cmdline --dtb --fs-options --header-version "
    "--kernel-offset --os-patch-level --os-version --page-size --ramdisk-offset "
    "--tags-offset --dtb-offset --set-active";
const char* kPartitions =
    "boot bootloader cache dtb dtbo init_boot modem odm odm_dlkm oem product pvmfw "
    "radio recovery super system system_dlkm system_ext userdata vbmeta "
    "vbmeta_system vendor vendor_boot vendor_dlkm vendor_kernel_boot";
const char* kGetvars =
    "all current-slot has-slot: is-logical: is-userspace max-download-size "
    "partition-size: partition-type: product slot-count super-partition-name "
    "unlocked version";
const char* kSlots = "a b all other";
const char* kRebootTargets = "bootloader recovery fastboot";
const char* kFlashingSubs = "lock unlock lock_critical unlock_critical get_unlock_ability";
const char* kGsiSubs = "wipe disable status";
const char* kSnapshotSubs = "cancel merge";
const char* kFsOptions = "casefold projid compress";

// ------------------------------------------------------------------- bash

std::string BashScript() {
    std::ostringstream o;
    o << R"BASHRAW(# bash completion for fastboot-meowarch.
# GENERATED by `fastboot-meowarch --shell-completion=bash` — do not edit by
# hand; regenerate so the vocabulary always matches the binary.
# Install as /usr/share/bash-completion/completions/fastboot-meowarch

_fastboot_meowarch() {
    local cur prev i cmd cmd_i nargs w
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    COMPREPLY=()

    local cmds=')BASHRAW";
    o << Join(CommandList());
    o << R"BASHRAW('
    local opts=')BASHRAW";
    o << OptionsList() << " " << kShortOpts;
    o << R"BASHRAW('
    local partitions=')BASHRAW";
    o << kPartitions;
    o << R"BASHRAW('
    local getvars=')BASHRAW";
    o << kGetvars;
    o << R"BASHRAW('
    local slots=')BASHRAW";
    o << kSlots;
    o << R"BASHRAW('
    local langs=')BASHRAW";
    o << LangList();
    o << R"BASHRAW('

    # serials of connected fastboot devices (for -s)
    _fbma_serials() {
        "${COMP_WORDS[0]}" devices 2>/dev/null | awk '{print $1}'
    }

    # option values, both `--opt val` and `--opt=val` forms
    case "$prev" in
        -s)
            COMPREPLY=( $(compgen -W "$(_fbma_serials)" -- "$cur") )
            return 0 ;;
        -S) return 0 ;;
        --slot | --set-active | -a)
            COMPREPLY=( $(compgen -W "$slots" -- "$cur") )
            return 0 ;;
        --lang)
            COMPREPLY=( $(compgen -W "$langs" -- "$cur") )
            return 0 ;;
        --fs-options)
            COMPREPLY=( $(compgen -W ")BASHRAW";
    o << kFsOptions;
    o << R"BASHRAW(" -- "$cur") )
            return 0 ;;
    esac
    case "$cur" in
        --lang=*)
            COMPREPLY=( $(compgen -W "$langs" -- "${cur#--lang=}") )
            COMPREPLY=( "${COMPREPLY[@]/#/--lang=}" )
            return 0 ;;
        --slot=* | --set-active=*)
            COMPREPLY=( $(compgen -W "$slots" -- "${cur#*=}") )
            COMPREPLY=( "${COMPREPLY[@]/#/${cur%%=*}=}" )
            return 0 ;;
        --fs-options=*)
            COMPREPLY=( $(compgen -W ")BASHRAW";
    o << kFsOptions;
    o << R"BASHRAW(" -- "${cur#--fs-options=}") )
            COMPREPLY=( "${COMPREPLY[@]/#/--fs-options=}" )
            return 0 ;;
    esac

    # completing an option token itself
    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
        return 0
    fi

    # locate the command: first non-option word, skipping option values
    cmd=""; cmd_i=0
    for ((i = 1; i < COMP_CWORD; i++)); do
        case " ${kValueOptsList} " in
            *" ${COMP_WORDS[i]} "*) ((i++)); continue ;;
        esac
        [[ "${COMP_WORDS[i]}" == -* ]] && continue
        cmd="${COMP_WORDS[i]}"; cmd_i=$i
        break
    done

    if [[ -z "$cmd" ]]; then
        COMPREPLY=( $(compgen -W "$cmds $opts" -- "$cur") )
        return 0
    fi

    # count consumed non-option args after the command
    nargs=0
    for ((i = cmd_i + 1; i < COMP_CWORD; i++)); do
        w="${COMP_WORDS[i]}"
        [[ "$w" == -* ]] && continue
        ((nargs++))
    done

    _fbma_img_files() {
        if [[ $(type -t compopt) == builtin ]]; then
            compopt -o filenames
            COMPREPLY+=( $(compgen -f -X '!*.img' -- "$cur") $(compgen -d -- "$cur") )
        else
            COMPREPLY+=( $(compgen -f -X '!*.img' -- "$cur") )
        fi
    }

    case "$cmd" in
        flash)
            if ((nargs == 0)); then
                COMPREPLY=( $(compgen -W "$partitions" -- "$cur") )
            else
                _fbma_img_files
            fi ;;
        erase | format)
            COMPREPLY=( $(compgen -W "$partitions" -- "$cur") ) ;;
        fetch)
            if ((nargs == 0)); then
                COMPREPLY=( $(compgen -W "$partitions" -- "$cur") )
            else
                [[ $(type -t compopt) == builtin ]] && compopt -o filenames
                COMPREPLY=( $(compgen -f -- "$cur") )
            fi ;;
        getvar)
            COMPREPLY=( $(compgen -W "$getvars" -- "$cur") ) ;;
        reboot)
            COMPREPLY=( $(compgen -W ")BASHRAW";
    o << kRebootTargets;
    o << R"BASHRAW(" -- "$cur") ) ;;
        flashing)
            COMPREPLY=( $(compgen -W ")BASHRAW";
    o << kFlashingSubs;
    o << R"BASHRAW(" -- "$cur") ) ;;
        gsi)
            COMPREPLY=( $(compgen -W ")BASHRAW";
    o << kGsiSubs;
    o << R"BASHRAW(" -- "$cur") ) ;;
        snapshot-update)
            COMPREPLY=( $(compgen -W ")BASHRAW";
    o << kSnapshotSubs;
    o << R"BASHRAW(" -- "$cur") ) ;;
        set_active)
            COMPREPLY=( $(compgen -W "$slots" -- "$cur") ) ;;
        update)
            [[ $(type -t compopt) == builtin ]] && compopt -o filenames
            COMPREPLY=( $(compgen -f -X '!*.zip' -- "$cur") $(compgen -d -- "$cur") ) ;;
        boot | stage | flash:raw | wipe-super)
            _fbma_img_files ;;
    esac
    return 0
}

complete -F _fastboot_meowarch fastboot-meowarch
)BASHRAW";
    std::string s = o.str();
    // splice the value-option list into the scanner (kept as a placeholder so
    // the raw literal above stays readable)
    const std::string ph = "${kValueOptsList}";
    size_t p = s.find(ph);
    if (p != std::string::npos) s.replace(p, ph.size(), kValueOpts);
    return s;
}

// -------------------------------------------------------------------- zsh

// zsh parses a bare `case` pattern starting with '-' as an option flag, so
// dash-prefixed patterns must be parenthesized: (-s|-S).
std::string ZshPattern(const std::string& words) {
    std::string s = words;
    std::replace(s.begin(), s.end(), ' ', '|');
    return "(" + s + ")";
}

std::string ZshScript() {
    std::ostringstream o;
    o << "#compdef fastboot-meowarch\n"
         "# zsh completion for fastboot-meowarch.\n"
         "# GENERATED by `fastboot-meowarch --shell-completion=zsh` — do not edit by\n"
         "# hand; regenerate so the vocabulary always matches the binary.\n"
         "# Install as /usr/share/zsh/site-functions/_fastboot-meowarch\n\n"
         "_fastboot_meowarch() {\n"
         "  local -a cmds opts shorts partitions getvars\n"
         "  cmds=(";
    for (const auto& c : CommandList()) o << c << " ";
    o << ")\n  opts=(";
    for (const auto& op : fbsuggest::Options()) o << op << " ";
    o << ")\n  shorts=(" << kShortOpts << ")\n"
      << "  partitions=(" << kPartitions << ")\n"
      << "  getvars=(" << kGetvars << ")\n"
      << "  local cur=${words[CURRENT]} prev=${words[CURRENT-1]}\n"
      << "  local i cmd='' cmd_i=0 nargs=0\n\n"
      << "  # option values\n"
      << "  case $prev in\n"
      << "    (-s)\n"
      << "      local -a serials\n"
      << "      serials=(${(f)\"$(${words[1]} devices 2>/dev/null | awk '{print $1}')\"})\n"
      << "      (( ${#serials} )) && compadd -a serials\n"
      << "      return ;;\n"
      << "    (-S) return ;;\n"
      << "    (--slot|--set-active|-a) compadd -- " << kSlots << "; return ;;\n"
      << "    (--lang) compadd -- " << LangList() << "; return ;;\n"
      << "    (--fs-options) compadd -- " << kFsOptions << "; return ;;\n"
      << "  esac\n"
      << "  case $cur in\n"
      << "    (--lang=*) compadd -Q -P '--lang=' -- " << LangList() << "; return ;;\n"
      << "    (--slot=*) compadd -Q -P '--slot=' -- " << kSlots << "; return ;;\n"
      << "    (--set-active=*) compadd -Q -P '--set-active=' -- " << kSlots << "; return ;;\n"
      << "    (--fs-options=*) compadd -Q -P '--fs-options=' -- " << kFsOptions << "; return ;;\n"
      << "  esac\n\n"
      << "  if [[ $cur == -* ]]; then\n"
      << "    compadd -a opts shorts\n"
      << "    return\n"
      << "  fi\n\n"
      << "  # locate the command (skip options and their values)\n"
      << "  for ((i = 2; i < CURRENT; i++)); do\n"
      << "    case ${words[i]} in\n"
      << "      " << ZshPattern(kValueOpts) << " ((i++)); continue ;;\n"
      << "    esac\n"
      << "    [[ ${words[i]} == -* ]] && continue\n"
      << "    cmd=${words[i]}; cmd_i=$i; break\n"
      << "  done\n\n"
      << "  if [[ -z $cmd ]]; then\n"
      << "    compadd -a cmds opts shorts\n"
      << "    return\n"
      << "  fi\n\n"
      << "  for ((i = cmd_i + 1; i < CURRENT; i++)); do\n"
      << "    [[ ${words[i]} == -* ]] && continue\n"
      << "    ((nargs++))\n"
      << "  done\n\n"
      << "  case $cmd in\n"
      << "    flash)\n"
      << "      if ((nargs == 0)); then compadd -a partitions\n"
      << "      else _files -g '*.img'; fi ;;\n"
      << "    erase|format) compadd -a partitions ;;\n"
      << "    fetch)\n"
      << "      if ((nargs == 0)); then compadd -a partitions\n"
      << "      else _files; fi ;;\n"
      << "    getvar) compadd -a getvars ;;\n"
      << "    reboot) compadd -- " << kRebootTargets << " ;;\n"
      << "    flashing) compadd -- " << kFlashingSubs << " ;;\n"
      << "    gsi) compadd -- " << kGsiSubs << " ;;\n"
      << "    snapshot-update) compadd -- " << kSnapshotSubs << " ;;\n"
      << "    set_active) compadd -- " << kSlots << " ;;\n"
      << "    update) _files -g '*.zip' ;;\n"
      << "    boot|stage|flash:raw|wipe-super) _files -g '*.img' ;;\n"
      << "  esac\n"
      << "}\n\n"
      << "_fastboot_meowarch \"$@\"\n";
    return o.str();
}

// ------------------------------------------------------------------- fish

std::string FishScript() {
    std::ostringstream o;
    o << "# fish completion for fastboot-meowarch.\n"
         "# GENERATED by `fastboot-meowarch --shell-completion=fish` — do not edit\n"
         "# by hand; regenerate so the vocabulary always matches the binary.\n"
         "# Install as /usr/share/fish/vendor_completions.d/fastboot-meowarch.fish\n\n"
         "function __fbma_devices -d 'Serials of connected fastboot devices'\n"
         "    set -l fb (commandline -opc)[1]\n"
         "    command $fb devices 2>/dev/null | awk '{print $1}'\n"
         "end\n\n";

    // commands (subcommand position) with upstream help descriptions
    for (const auto& c : CommandList()) {
        o << "complete -c fastboot-meowarch -f -n '__fish_use_subcommand' -a '" << c << "'";
        std::string d = DescFor(c);
        if (!d.empty()) o << " -d '" << FishQuote(d) << "'";
        o << "\n";
    }
    o << "\n";

    // options with descriptions + typed arguments
    struct OptArg {
        const char* opt;   // with leading dashes
        const char* args;  // nullptr = free-form value, "" = no value
    };
    for (const auto& op : fbsuggest::Options()) {
        o << "complete -c fastboot-meowarch -l '" << op.substr(2) << "'";
        const char* args = nullptr;
        if (op == "--slot" || op == "--set-active") args = kSlots;
        else if (op == "--lang") args = nullptr;  // handled below with -xa langs
        else if (op == "--fs-options") args = kFsOptions;
        else if (op == "--disable-verification" || op == "--disable-verity" ||
                 op == "--disable-super-optimization" || op == "--exclude-dynamic-partitions" ||
                 op == "--disable-fastboot-info" || op == "--force" || op == "--skip-reboot" ||
                 op == "--skip-secondary" || op == "--unbuffered" || op == "--version" ||
                 op == "--help" || op == "--list-langs" || op == "--no-color" ||
                 op == "--color" || op == "--no-progress")
            args = "";
        if (op == "--lang") {
            o << " -r -f -xa '" << LangList() << "'";
        } else if (args && args[0]) {
            o << " -r -f -xa '" << args << "'";
        } else if (args) {
            o << "";  // flag: no argument spec needed
        } else {
            o << " -r";  // takes a value, free-form
        }
        std::string d = DescFor(op);
        if (!d.empty()) o << " -d '" << FishQuote(d) << "'";
        o << "\n";
    }
    // short options
    o << "complete -c fastboot-meowarch -s w -d '" << FishQuote(DescFor("-w")) << "'\n";
    o << "complete -c fastboot-meowarch -s s -r -f -xa '(__fbma_devices)' -d 'Serial (USB or tcp/udp host[:port])'\n";
    o << "complete -c fastboot-meowarch -s S -r -d 'Sparse-limit SIZE[K|M|G]'\n";
    o << "complete -c fastboot-meowarch -s a -d 'Set active slot before rebooting'\n";
    o << "complete -c fastboot-meowarch -s l -d 'Long listing for devices'\n";
    o << "complete -c fastboot-meowarch -s v -d 'Verbose'\n";
    o << "complete -c fastboot-meowarch -s h -d 'Show help'\n\n";

    // per-command argument vocabularies
    o << "complete -c fastboot-meowarch -f -n '__fish_seen_subcommand_from flash erase format fetch' -a '"
      << kPartitions << "'\n";
    o << "complete -c fastboot-meowarch -f -n '__fish_seen_subcommand_from getvar' -a '" << kGetvars
      << "'\n";
    o << "complete -c fastboot-meowarch -f -n '__fish_seen_subcommand_from reboot' -a '"
      << kRebootTargets << "'\n";
    o << "complete -c fastboot-meowarch -f -n '__fish_seen_subcommand_from flashing' -a '"
      << kFlashingSubs << "'\n";
    o << "complete -c fastboot-meowarch -f -n '__fish_seen_subcommand_from gsi' -a '" << kGsiSubs
      << "'\n";
    o << "complete -c fastboot-meowarch -f -n '__fish_seen_subcommand_from snapshot-update' -a '"
      << kSnapshotSubs << "'\n";
    o << "complete -c fastboot-meowarch -f -n '__fish_seen_subcommand_from set_active' -a '"
      << kSlots << "'\n";
    o << "complete -c fastboot-meowarch -F -n '__fish_seen_subcommand_from boot stage wipe-super'\n";
    o << "complete -c fastboot-meowarch -F -n '__fish_seen_subcommand_from update'\n";
    return o.str();
}

}  // namespace

std::string Generate(const std::string& shell) {
    // i18n registry must be initialized before LangList() is meaningful;
    // EarlyInit always runs Init() first, but be safe standalone.
    fbi18n::Init("");
    if (shell == "bash") return BashScript();
    if (shell == "zsh") return ZshScript();
    if (shell == "fish") return FishScript();
    return "";
}

}  // namespace fbcompletion
