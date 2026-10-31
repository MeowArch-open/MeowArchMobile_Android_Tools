/*
 * fb_completion.h — shell completion script generator (bash / zsh / fish).
 *
 * MeowArchMobile glue, not an AOSP file. `fastboot --shell-completion=SHELL`
 * prints a ready-to-source completion script for SHELL on stdout. Generating
 * from the binary keeps the vocabulary in sync with what the binary actually
 * accepts: commands and long options come from fbsuggest's tables (the same
 * single source of truth that powers Did-you-want), and the language list
 * comes from the compiled i18n registry.
 *
 * The script is hidden from --help on purpose (byte-parity with upstream),
 * documented in README.md. Package builds call it once per shell and install
 * the output into the standard vendor directories.
 */
#pragma once

#include <string>

namespace fbcompletion {

// Returns the completion script for |shell| ("bash", "zsh", "fish").
// Returns an empty string for an unknown shell (caller reports the error).
std::string Generate(const std::string& shell);

}  // namespace fbcompletion
