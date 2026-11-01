/*
 * fb_color.h — terminal color support for the standalone fastboot build.
 *
 * Not an AOSP file: this is MeowArchMobile glue compiled alongside the
 * upstream sources. Colors are opt-in by environment detection, never forced
 * on a pipe, so scripted output stays byte-identical to upstream:
 *
 *   enabled  = stderr is a tty
 *           && TERM is set and != "dumb"        (POSIX only — see below)
 *           && NO_COLOR is unset (https://no-color.org)
 *           && !fb_color_disable() was called (--no-color)
 *   FORCE_COLOR=1 overrides the tty/TERM checks (useful for CI screenshots).
 *
 * Windows: "tty" means GetConsoleMode succeeds, and ANSI escapes only work
 * once ENABLE_VIRTUAL_TERMINAL_PROCESSING is set on the handle — Windows
 * Terminal leaves it off by default. Init() turns it on. The TERM check is
 * skipped there because cmd.exe / Windows Terminal do not set TERM at all
 * (that check wrongly disabled colors on every native Windows terminal).
 */
#pragma once

#include <string>

namespace fbcolor {

enum class C {
    Reset,
    Bold,
    Dim,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
};

// Call once from main() before any output. Detects the environment.
void Init();

// Explicit overrides (--no-color / --color).
void SetEnabled(bool enabled);
bool Enabled();

// Returns the SGR escape for |c|, or "" when colors are off — so callers can
// unconditionally write Color(C::Red) << "text" << Color(C::Reset).
const char* Code(C c);

// Convenience: wraps |s| in |c| (no-op string when disabled).
std::string Paint(C c, const std::string& s);

// True when ANSI control sequences (SGR, "\r", \033[K) are actually
// interpreted by the terminal on fd 2. Independent of whether colors are
// *enabled*: NO_COLOR turns colors off but must not break the progress bar's
// ability to erase its own line — that coupling used to leave the bar's tail
// on screen after "OKAY [...]" (visible on Windows, where it also lacked VT
// processing entirely).
bool EscapesOk();

// Clears from the cursor to end of line. Empty string when EscapesOk() is
// false, i.e. when writing an escape would land in a pipe as literal text.
const char* ClearToEol();

}  // namespace fbcolor