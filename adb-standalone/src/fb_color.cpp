/*
 * fb_color.cpp — see fb_color.h. MeowArchMobile glue, not an AOSP file.
 */
#include "fb_color.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

namespace fbcolor {
namespace {

bool g_enabled = false;
bool g_forced = false;  // set by --color; overrides tty detection only, not NO_COLOR
bool g_escapes_ok = false;  // stderr interprets ANSI sequences (see EscapesOk)

// Is stderr a real console?
//
// POSIX isatty() is not enough on Windows: redirecting to the NUL device
// (`2>nul`, or `2>/dev/null` under MSYS bash) hands us a *character device*,
// which _isatty() happily reports as a terminal — and colours then leak into
// piped output (caught by the windows-latest CI parity test). GetConsoleMode
// is the accurate test: it succeeds only for an actual console screen buffer.
// |fd| chooses the stream whose console state matters: STDERR for the progress
// bar and error colouring, STDOUT for `adb help` (its text goes to stdout, so
// `adb help 2>log` must stay coloured).
bool StreamIsTerminal(int fd) {
#ifdef _WIN32
    HANDLE h = GetStdHandle(fd == STDOUT_FILENO ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    DWORD mode = 0;
    if (h == NULL || h == INVALID_HANDLE_VALUE) return false;
    if (!GetConsoleMode(h, &mode)) return false;
    // A console screen buffer is a terminal, but ANSI sequences only do
    // anything once ENABLE_VIRTUAL_TERMINAL_PROCESSING is on — Windows
    // Terminal and conhost both leave it off by default, so a "\r" repaint
    // and "\033[K" were written out literally and every progress frame got
    // appended to the line until it wrapped (the Windows bug). Turn it on.
    if (!(mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        if (!SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            // Pre-VT console (Windows 8 or an old conhost): treat as
            // non-escape-capable so callers fall back to plain output.
            return false;
        }
    }
    return true;
#else
    return isatty(fd) == 1;
#endif
}

bool StderrIsTerminal() { return StreamIsTerminal(STDERR_FILENO); }

bool TermAllowsEscapes() {
    const char* term = getenv("TERM");
#ifdef _WIN32
    // Native Windows terminals do not set TERM; only reject an explicit
    // "dumb". Before this, every Windows terminal failed the "TERM is set"
    // check and silently lost colors and the progress bar.
    if (term && !strcmp(term, "dumb")) return false;
    return true;
#else
    if (!term || !term[0] || !strcmp(term, "dumb")) return false;
    // A bare "TERM=xterm" without any color capability is rare enough not to
    // bother with terminfo lookups; upstream tools (git, ls) make the same
    // trade-off.
    return true;
#endif
}

bool Detect() {
    // NO_COLOR wins over everything, including FORCE_COLOR and --color.
    const char* no_color = getenv("NO_COLOR");
    if (no_color && no_color[0] != '\0') return false;

    const char* force = getenv("FORCE_COLOR");
    if (force && force[0] != '\0' && force[0] != '0') return true;

    return g_escapes_ok && TermAllowsEscapes();
}

}  // namespace

void Init() {
    // Escape capability is decided once, from the device itself: the
    // terminal either interprets ANSI or it does not. NO_COLOR and
    // --no-color must not affect it (they only stop us *emitting* color).
    g_escapes_ok = StderrIsTerminal();

    g_enabled = g_forced ? true : Detect();
    // --color forces on even when stderr is a pipe, but still respects NO_COLOR.
    if (g_forced) {
        const char* no_color = getenv("NO_COLOR");
        g_enabled = !(no_color && no_color[0] != '\0');
    }
}

void InitForStdout() {
    g_escapes_ok = StreamIsTerminal(STDOUT_FILENO);
    if (g_forced) {
        const char* no_color = getenv("NO_COLOR");
        g_enabled = !(no_color && no_color[0] != '\0');
        return;
    }
    g_enabled = Detect();
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

bool EscapesOk() { return g_escapes_ok; }

const char* Code(C c) {
    // g_escapes_ok keeps a non-VT console from getting literal escape text.
    // An explicit --color/FORCE_COLOR bypasses it: forcing colour into a pipe
    // (`... | less -R`) is exactly what the flag is for.
    if (!g_enabled || !(g_escapes_ok || g_forced)) return "";
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
    if (!g_enabled || !(g_escapes_ok || g_forced) || s.empty()) return s;
    return std::string(Code(c)) + s + Code(C::Reset);
}

const char* ClearToEol() { return g_escapes_ok ? "\033[K" : ""; }

}  // namespace fbcolor