/*
 * fb_color.h — terminal color support for the standalone fastboot build.
 *
 * Not an AOSP file: this is MeowArchMobile glue compiled alongside the
 * upstream sources. Colors are opt-in by environment detection, never forced
 * on a pipe, so scripted output stays byte-identical to upstream:
 *
 *   enabled  = stderr is a tty
 *           && TERM is set and != "dumb"
 *           && NO_COLOR is unset (https://no-color.org)
 *           && !fb_color_disable() was called (--no-color)
 *   FORCE_COLOR=1 overrides the tty/TERM checks (useful for CI screenshots).
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

// Clears from the cursor to end of line (empty string when colors are off —
// callers that redraw a line must not rely on it).
const char* ClearToEol();

}  // namespace fbcolor
