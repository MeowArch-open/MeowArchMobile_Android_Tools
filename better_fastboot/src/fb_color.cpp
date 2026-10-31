/*
 * fb_color.cpp — see fb_color.h. MeowArchMobile glue, not an AOSP file.
 */
#include "fb_color.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace fbcolor {
namespace {

bool g_enabled = false;
bool g_forced = false;  // set by --color; overrides tty detection only, not NO_COLOR

bool Detect() {
    // NO_COLOR wins over everything, including FORCE_COLOR and --color.
    const char* no_color = getenv("NO_COLOR");
    if (no_color && no_color[0] != '\0') return false;

    const char* force = getenv("FORCE_COLOR");
    if (force && force[0] != '\0' && force[0] != '0') return true;

    if (!isatty(STDERR_FILENO)) return false;

    const char* term = getenv("TERM");
    if (!term || !term[0] || !strcmp(term, "dumb")) return false;

    // A bare "TERM=xterm" without any color capability is rare enough not to
    // bother with terminfo lookups; upstream tools (git, ls) make the same
    // trade-off.
    return true;
}

}  // namespace

void Init() {
    g_enabled = g_forced ? true : Detect();
    // --color forces on even when stderr is a pipe, but still respects NO_COLOR.
    if (g_forced) {
        const char* no_color = getenv("NO_COLOR");
        g_enabled = !(no_color && no_color[0] != '\0');
    }
}

void SetEnabled(bool enabled) {
    g_forced = true;
    // NO_COLOR (https://no-color.org) wins over an explicit --color: an
    // explicit environment opt-out is the user's stronger signal.
    if (enabled) {
        const char* no_color = getenv("NO_COLOR");
        enabled = !(no_color && no_color[0] != '\0');
    }
    g_enabled = enabled;
}

bool Enabled() { return g_enabled; }

const char* Code(C c) {
    if (!g_enabled) return "";
    switch (c) {
        case C::Reset:   return "\033[0m";
        case C::Bold:    return "\033[1m";
        case C::Dim:     return "\033[2m";
        case C::Red:     return "\033[31m";
        case C::Green:   return "\033[32m";
        case C::Yellow:  return "\033[33m";
        case C::Blue:    return "\033[34m";
        case C::Magenta: return "\033[35m";
        case C::Cyan:    return "\033[36m";
    }
    return "";
}

std::string Paint(C c, const std::string& s) {
    if (!g_enabled || s.empty()) return s;
    return std::string(Code(c)) + s + Code(C::Reset);
}

const char* ClearToEol() { return g_enabled ? "\033[K" : ""; }

}  // namespace fbcolor
