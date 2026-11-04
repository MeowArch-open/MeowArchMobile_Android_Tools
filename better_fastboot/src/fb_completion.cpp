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
    o << R"BASHRAW(# bash completion for mfastboot (long alias: fastboot-meowarch).
# GENERATED by `mfastboot --shell-completion=bash` — do not edit by hand;
# regenerate so the vocabulary always matches the binary.
# Install as /usr/share/bash-completion/completions/mfastboot and
# /usr/share/bash-completion/completions/fastboot-meowarch (same content:
# bash-completion only autoloads the file named after the command).

_mfastboot() {
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
        flash | flash:raw)
            # flash PARTITION [FILENAME]: argument 1 is a partition name, the
            # rest are image files (upstream: flash:raw PARTITION KERNEL ...).
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
        flash-x)
            # flash-x PARTITION PAYLOAD [NAME]: argument 1 is the partition to
            # write, argument 2 the payload.bin, argument 3 the entry inside
            # the payload (defaults to the partition, slot suffix stripped).
            if ((nargs == 0 || nargs >= 2)); then
                COMPREPLY=( $(compgen -W "$partitions" -- "$cur") )
            else
                [[ $(type -t compopt) == builtin ]] && compopt -o filenames
                COMPREPLY=( $(compgen -f -- "$cur") $(compgen -d -- "$cur") )
            fi ;;
        boot | stage | wipe-super)
            _fbma_img_files ;;
    esac
    return 0
}

complete -F _mfastboot mfastboot fastboot-meowarch
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
    o << "#compdef mfastboot fastboot-meowarch\n"
         "# zsh completion for mfastboot (long alias: fastboot-meowarch).\n"
         "# GENERATED by `mfastboot --shell-completion=zsh` — do not edit by\n"
         "# hand; regenerate so the vocabulary always matches the binary.\n"
         "# Install as /usr/share/zsh/site-functions/_mfastboot and\n"
         "# /usr/share/zsh/site-functions/_fastboot-meowarch (same content).\n\n"
         "_mfastboot() {\n"
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
      << "    flash|flash:raw)\n"
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
      << "    flash-x)\n"
      << "      if ((nargs == 1)); then _files\n"
      << "      else compadd -a partitions; fi ;;\n"
      << "    update) _files -g '*.zip' ;;\n"
      << "    boot|stage|wipe-super) _files -g '*.img' ;;\n"
      << "  esac\n"
      << "}\n\n"
      << "_mfastboot \"$@\"\n";
    return o.str();
}

// ------------------------------------------------------------------- fish

std::string FishScript() {
    std::ostringstream o;

    // Fish evaluates every applicable `complete` rule and concatenates the
    // results, so a rule has to know *where* the cursor is, not just which
    // subcommand was typed. __fbma_arg_is N <cmds...> is true only when the
    // completion point is exactly argument N of one of <cmds> (0-based, after
    // the command word). Before this, a flat
    // `-n '__fish_seen_subcommand_from flash' -a '<partitions>'` rule offered
    // partition names after `flash boot_a`, where a file name belongs.
    o << "# fish completion for mfastboot (long alias: fastboot-meowarch).\n"
         "# GENERATED by `mfastboot --shell-completion=fish` — do not edit by\n"
         "# hand; regenerate so the vocabulary always matches the binary.\n"
         "# Install as /usr/share/fish/vendor_completions.d/mfastboot.fish and\n"
         "# .../fastboot-meowarch.fish (same content).\n\n"
         "function __fbma_devices -d 'Serials of connected fastboot devices'\n"
         "    set -l fb (commandline -opc)[1]\n"
         "    command $fb devices 2>/dev/null | awk '{print $1}'\n"
         "end\n\n"
         "function __fbma_pos -d 'Print the command word and how many arguments follow it'\n"
         "    set -l toks (commandline -opc)\n"
         "    set -l cmd ''\n"
         "    set -l nargs 0\n"
         "    set -l skip 0\n"
         "    if test (count $toks) -gt 1\n"
         "        for t in $toks[2..-1]\n"
         "            if test $skip -eq 1\n"
         "                set skip 0\n"
         "                continue\n"
         "            end\n"
         "            if contains -- $t $__fbma_value_opts\n"
         "                set skip 1\n"
         "                continue\n"
         "            end\n"
         "            string match -q -- '-*' $t; and continue\n"
         "            if test -z \"$cmd\"\n"
         "                set cmd $t\n"
         "                continue\n"
         "            end\n"
         "            set nargs (math $nargs + 1)\n"
         "        end\n"
         "    end\n"
         "    printf '%s\\n%d\\n' \"$cmd\" $nargs\n"
         "end\n\n"
         "function __fbma_arg_is -d 'True when completing exactly argument N of one of these commands'\n"
         "    set -l nth $argv[1]\n"
         "    set -e argv[1]\n"
         "    set -l pos (__fbma_pos)\n"
         "    contains -- $pos[1] $argv\n"
         "    and test \"$pos[2]\" -eq $nth\n"
         "end\n\n"
         "function __fbma_arg_from -d 'True from argument N on of one of these commands'\n"
         "    set -l nth $argv[1]\n"
         "    set -e argv[1]\n"
         "    set -l pos (__fbma_pos)\n"
         "    contains -- $pos[1] $argv\n"
         "    and test \"$pos[2]\" -ge $nth\n"
         "end\n\n"
         "function __fbma_files -d 'Local files (optionally one extension) and directories'\n"
         "    set -l ext $argv[1]\n"
         "    set -l cur (commandline -ct)\n"
         "    set -l dir .\n"
         "    set -l showpath 0\n"
         "    if string match -q -- '*/' $cur\n"
         "        set dir $cur\n"
         "        set showpath 1\n"
         "    else if string match -q -- '*/*' $cur\n"
         "        set dir (path dirname -- $cur)\n"
         "        set showpath 1\n"
         "    end\n"
         "    for f in $dir/*\n"
         "        test -e $f; or continue\n"
         "        set -l out $f\n"
         "        if test $showpath -eq 0\n"
         "            set out (path basename -- $f)\n"
         "        end\n"
         "        if test -d $f\n"
         "            printf '%s/\\n' $out\n"
         "        else if test -z \"$ext\"\n"
         "            printf '%s\\n' $out\n"
         "        else if string match -q -r \"\\.$ext\\$\" -- $f\n"
         "            printf '%s\\n' $out\n"
         "        end\n"
         "    end\n"
         "end\n\n"
         "function __fbma_img_files -d 'Local *.img files and directories'\n"
         "    __fbma_files img\n"
         "end\n\n"
         "set -g __fbma_value_opts ";

    // options that consume the next token (same list as bash/zsh)
    o << kValueOpts << "\n\n";

    // Two command names, one script: `mfastboot` is the short primary name,
    // `fastboot-meowarch` is kept as an alias in /usr/bin (both are symlinks to
    // the same binary). A single `complete -c A -c B` call registers for both.
    // commands (subcommand position) with upstream help descriptions
    for (const auto& c : CommandList()) {
        o << "complete -c mfastboot -c fastboot-meowarch -f -n '__fish_use_subcommand' -a '" << c
          << "'";
        std::string d = DescFor(c);
        if (!d.empty()) o << " -d '" << FishQuote(d) << "'";
        o << "\n";
    }
    o << "\n";

    // options with descriptions + typed arguments
    for (const auto& op : fbsuggest::Options()) {
        o << "complete -c mfastboot -c fastboot-meowarch -l '" << op.substr(2) << "'";
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
    o << "complete -c mfastboot -c fastboot-meowarch -s w -d '" << FishQuote(DescFor("-w"))
      << "'\n";
    o << "complete -c mfastboot -c fastboot-meowarch -s s -r -f -xa '(__fbma_devices)'"
         " -d 'Serial (USB or tcp/udp host[:port])'\n";
    o << "complete -c mfastboot -c fastboot-meowarch -s S -r -d 'Sparse-limit SIZE[K|M|G]'\n";
    o << "complete -c mfastboot -c fastboot-meowarch -s a -d 'Set active slot before rebooting'\n";
    o << "complete -c mfastboot -c fastboot-meowarch -s l -d 'Long listing for devices'\n";
    o << "complete -c mfastboot -c fastboot-meowarch -s v -d 'Verbose'\n";
    o << "complete -c mfastboot -c fastboot-meowarch -s h -d 'Show help'\n\n";

    // NB: no blanket `-f` rule here. fish rejects --no-files together with
    // --force-files on the same command, and file completion is already
    // suppressed position by position (-f on each rule below) — which is also
    // what keeps `flash <TAB>` from listing every file in the directory.

    // Per-command argument vocabularies, position aware.
    //   flash / flash:raw  PARTITION [FILENAME ...]   partition, then images
    //   flash-x            PARTITION PAYLOAD [NAME]   partition, payload, entry
    //   erase / format     PARTITION
    //   fetch              PARTITION OUT_FILE
    //   getvar             VARIABLE
    //   reboot             [TARGET]
    //   set_active         SLOT
    //   boot/stage/wipe-super  IMAGE
    //   update             ZIP
    const std::string base = "complete -c mfastboot -c fastboot-meowarch -f -n ";
    o << base << "'__fbma_arg_is 0 flash flash:raw erase format fetch' -a '" << kPartitions
      << "'\n";
    o << base << "'__fbma_arg_from 1 flash flash:raw' -a '(__fbma_img_files)'\n";
    o << base << "'__fbma_arg_is 1 fetch' -a '(__fbma_files)'\n";
    //   flash-x  PARTITION PAYLOAD [NAME]   partition, then the payload file,
    //            then the entry inside the payload
    o << base << "'__fbma_arg_is 0 flash-x' -a '" << kPartitions << "'\n";
    o << base << "'__fbma_arg_is 1 flash-x' -a '(__fbma_files)'\n";
    o << base << "'__fbma_arg_from 2 flash-x' -a '" << kPartitions << "'\n";
    o << base << "'__fbma_arg_is 0 getvar' -a '" << kGetvars << "'\n";
    o << base << "'__fbma_arg_is 0 reboot' -a '" << kRebootTargets << "'\n";
    o << base << "'__fbma_arg_is 0 flashing' -a '" << kFlashingSubs << "'\n";
    o << base << "'__fbma_arg_is 0 gsi' -a '" << kGsiSubs << "'\n";
    o << base << "'__fbma_arg_is 0 snapshot-update' -a '" << kSnapshotSubs << "'\n";
    o << base << "'__fbma_arg_is 0 set_active' -a '" << kSlots << "'\n";
    o << "complete -c mfastboot -c fastboot-meowarch -f -n "
         "'__fbma_arg_is 0 boot stage wipe-super' -a '(__fbma_img_files)'\n";
    o << "complete -c mfastboot -c fastboot-meowarch -f -n "
         "'__fbma_arg_is 0 update' -a '(__fbma_files zip)'\n";
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
