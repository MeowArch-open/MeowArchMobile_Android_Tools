/*
 * fb_progress.cpp — see fb_progress.h. MeowArchMobile glue, not an AOSP file.
 *
 * Renders a single-line bar on stderr while a transfer runs. All entry points
 * early-return when disabled (non-tty stderr, NO_PROGRESS, --no-progress) so
 * piped/CI output is byte-identical to upstream.
 *
 * Single-line discipline: the whole frame is assembled in memory, truncated to
 * the terminal width, and written with one "\r...\033[K" — never a partial
 * frame, never more than one screen row. Drawing past the width is what made
 * the bar wrap onto a new line, after which "\r" returned to the wrong row and
 * every frame stayed on screen.
 */
#include "fb_progress.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#endif

#include <algorithm>
#include <string>

#include "fb_color.h"
#include "fb_i18n.h"

namespace fbprogress {
namespace {

bool g_enabled = false;   // bar rendering on (tty && !NO_PROGRESS)
bool g_armed = false;     // a command is in progress (Arm() called)
bool g_bar_active = false; // the bar line is currently drawn on screen
bool g_hidden = false;    // temporarily hidden for an interleaved message

std::string g_msg;        // exact prolog text upstream printed (padded)
std::string g_shown;      // prolog as last drawn (trimmed to fit the terminal)
bool g_shown_valid = false;  // g_shown was actually rendered (it may be empty)

// Terminal width used for this draw, sampled per frame so a resize mid
// transfer is picked up. 0 = unknown -> conservative default.
int g_cols = 80;

// Bracket colour of the last frame: when it changes, the bar is repainted
// without relying on "clear to end of line", which keeps the final state
// correct even on a terminal that ignores \033[K.
bool g_last_green = false;

// Rate sampling: instantaneous speed over a short ring of samples.
struct Sample {
    double t;
    uint64_t bytes;
};
Sample g_samples[8];
int g_sample_head = 0;
int g_sample_count = 0;

double g_last_draw_t = 0.0;
uint64_t g_last_draw_bytes = 0;
uint64_t g_granularity = 65536;  // min byte delta to consider a redraw

double Now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

// stderr must be a real console for the bar to make sense (it repaints the
// line with \r). On Windows that means GetConsoleMode, not _isatty: a NUL
// redirect is still a character device and would be mistaken for a terminal.
bool StderrIsTerminal() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    if (h == NULL || h == INVALID_HANDLE_VALUE) return false;
    return GetConsoleMode(h, &mode) != 0;
#else
    return isatty(STDERR_FILENO) == 1;
#endif
}

bool Detect() {
    const char* np = getenv("NO_PROGRESS");
    if (np && np[0] != '\0') return false;
    if (!StderrIsTerminal()) return false;
    const char* term = getenv("TERM");
    // Windows terminals do not export TERM; only an explicit "dumb" opts out
    // (same rule as fb_color).
    if (term && !strcmp(term, "dumb")) return false;
    return true;
}

// Width of the terminal behind stderr, in columns.
int TermCols() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h != NULL && h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &csbi)) {
        int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (w > 0) return w;
    }
#else
    struct winsize ws;
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return (int)ws.ws_col;
#endif
    return 0;
}

// Format a byte count as a human string with a binary unit.
void FmtBytes(double bytes, char* out, size_t n) {
    static const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    int u = 0;
    while (bytes >= 1024.0 && u < 4) {
        bytes /= 1024.0;
        u++;
    }
    if (u == 0) snprintf(out, n, "%.0f %s", bytes, units[u]);
    else snprintf(out, n, "%.1f %s", bytes, units[u]);
}

// Speed in bytes/sec over the sample window.
double Speed() {
    if (g_sample_count < 2) return 0.0;
    int oldest = (g_sample_head - g_sample_count + 8) % 8;
    int newest = (g_sample_head - 1 + 8) % 8;
    double dt = g_samples[newest].t - g_samples[oldest].t;
    if (dt <= 0.0001) return 0.0;
    double db = (double)g_samples[newest].bytes - (double)g_samples[oldest].bytes;
    return db / dt;
}

void FmtEta(double secs, char* out, size_t n) {
    if (secs < 0 || secs > 86400) {
        snprintf(out, n, "--:--");
        return;
    }
    long s = (long)(secs + 0.5);
    if (s >= 3600) snprintf(out, n, "%ld:%02ld:%02ld", s / 3600, (s % 3600) / 60, s % 60);
    else snprintf(out, n, "%ld:%02ld", s / 60, s % 60);
}

// Display columns of a UTF-8 string. Exact for ASCII and Latin-1, two columns
// for the CJK / fullwidth ranges (that second column is what made the bar
// overflow an 80-column terminal with the Chinese catalog loaded: the prolog
// is padded to 50 *characters* but occupies 55 columns).
size_t DisplayWidth(const std::string& s) {
    size_t cols = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        unsigned cp;
        int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { cols++; i++; continue; }  // invalid lead byte: count one column
        if (i + len > s.size()) { cols++; i++; continue; }
        for (int k = 1; k < len; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += len;
        bool wide = (cp >= 0x1100 && cp <= 0x115F) ||          // Hangul Jamo
                    (cp >= 0x2E80 && cp <= 0xA4CF) ||          // CJK radicals..Yi
                    (cp >= 0xAC00 && cp <= 0xD7A3) ||          // Hangul syllables
                    (cp >= 0xF900 && cp <= 0xFAFF) ||          // CJK compat ideographs
                    (cp >= 0xFE30 && cp <= 0xFE6F) ||          // CJK compat forms
                    (cp >= 0xFF00 && cp <= 0xFF60) ||          // fullwidth forms
                    (cp >= 0xFFE0 && cp <= 0xFFE6) ||
                    (cp >= 0x1F300 && cp <= 0x1F64F) ||        // emoji
                    (cp >= 0x1F900 && cp <= 0x1F9FF) ||
                    (cp >= 0x20000 && cp <= 0x3FFFD);
        cols += wide ? 2 : 1;
    }
    return cols;
}

// Truncate |s| to at most |cols| display columns, appending an ellipsis when it
// had to cut. Returns a UTF-8 string; never splits a multi-byte sequence.
std::string CutToCols(const std::string& s, size_t cols) {
    if (DisplayWidth(s) <= cols) return s;
    // Reserve one column for the ellipsis.
    size_t budget = cols > 1 ? cols - 1 : cols;
    size_t used = 0, i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        int len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        std::string ch = s.substr(i, len);
        size_t w = DisplayWidth(ch);
        if (used + w > budget) break;
        used += w;
        i += len;
    }
    std::string out = s.substr(0, i);
    if (cols > 1) out += "\xE2\x80\xA6";  // U+2026 HORIZONTAL ELLIPSIS
    return out;
}

void Draw(uint64_t current, uint64_t total) {
    double frac = total ? (double)current / (double)total : 0.0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    const bool green = frac >= 1.0;

    char pct[16];
    snprintf(pct, sizeof(pct), "%3d%%", (int)(frac * 100.0 + 0.5));

    char cur[32], tot[32];
    FmtBytes((double)current, cur, sizeof(cur));
    FmtBytes((double)total, tot, sizeof(tot));

    double sp = Speed();
    std::string spd, eta;
    if (sp > 1.0) {
        char spu[32];
        FmtBytes(sp, spu, sizeof(spu));
        spd = std::string(" ") + spu + "/s";
        if (total > current) {
            char e[32];
            FmtEta(((double)total - (double)current) / sp, e, sizeof(e));
            eta = std::string(" ") + _(fbi18n::kEta) + " " + e;
        }
    }
    const std::string cnt = std::string(cur) + "/" + tot;

    // Upstream's Status() pads the prolog to 50 columns so that OKAY lines up
    // on the *finished* line. On a repainted bar line that padding is dead
    // width, so measure (and later reprint) the trimmed text.
    std::string prolog = g_msg;
    while (!prolog.empty() && prolog.back() == ' ') prolog.pop_back();
    size_t prolog_w = DisplayWidth(prolog);

    const int cols = g_cols > 0 ? g_cols : 80;
    const int kMinBar = 12, kMaxBar = 28;

    // Field widths are budgeted, not measured per frame: the speed text
    // changes length every frame, and letting the bar chase it makes the line
    // jitter. Each constant is the worst case for that field, including its
    // leading separator.
    const size_t cnt_budget = 2 + 2 * DisplayWidth(tot) + 1;  // "  99.9 MiB/99.9 MiB"
    const size_t spd_budget = 12;                             // " 999.9 MiB/s"
    const size_t eta_budget = 1 + DisplayWidth(_(fbi18n::kEta)) + 9;  // " ETA 12:34:56"
    const size_t pct_budget = 2 + 4;                          // "  100%"

    struct Tail {
        bool counter, speed, eta;
        size_t budget;
    };
    auto budget_of = [&](bool counter, bool speed, bool eta) {
        return pct_budget + (counter ? cnt_budget : 0) + (speed ? spd_budget : 0) +
               (eta ? eta_budget : 0);
    };
    // Degradation order: drop ETA, then drop the byte counters, then keep the
    // percentage only — a narrow terminal loses detail instead of wrapping
    // onto a second row (a wrapped row makes "\r" repaint the wrong line and
    // leaves every frame on screen).
    const Tail tails[] = {
        {true, true, true, budget_of(true, true, true)},
        {true, true, false, budget_of(true, true, false)},
        {true, false, false, budget_of(true, false, false)},
        {false, false, false, budget_of(false, false, false)},
    };
    Tail tail = tails[3];
    for (const auto& t : tails) {
        if (prolog_w + 3 + kMinBar + t.budget <= (size_t)cols) {
            tail = t;
            break;
        }
    }
    size_t tail_budget = tail.budget;

    // Give the bar everything the line has left, within tasteful bounds.
    int width = cols - (int)prolog_w - 3 - (int)tail_budget;
    if (width > kMaxBar) width = kMaxBar;
    if (width < kMinBar) {
        // No room: shorten the prolog (it is the most redundant part — the
        // command line the user typed is right above) before giving up.
        int room = cols - 3 - kMinBar - (int)tail_budget;
        if (room >= 8) {
            prolog = CutToCols(prolog, (size_t)room);
            prolog_w = DisplayWidth(prolog);
        } else {
            prolog.clear();  // bar + numbers only
            prolog_w = 0;
        }
        width = cols - (int)prolog_w - 3 - (int)tail_budget;
        if (width > kMaxBar) width = kMaxBar;
        if (width < 4) width = 4;
    }

    int filled = (int)(frac * width + 0.5);
    if (filled > width) filled = width;
    std::string barstr("[");
    for (int i = 0; i < width; i++) {
        if (i < filled) barstr += '#';
        else if (i == filled && frac > 0 && frac < 1) barstr += '>';
        else barstr += '-';
    }
    barstr += ']';

    const std::string sep = prolog.empty() ? std::string() : std::string(" ");

    // A terminal that cannot erase the line (no ANSI) would smear every frame
    // together, so there the bar only appears once the line is complete: a
    // single self-contained line, which the epilog then rewrites exactly.
    if (!fbcolor::EscapesOk()) {
        if (current < total) return;
        // "\r" is a carriage return, not an escape: it still works on a
        // console without VT processing, so the padded prolog Status()
        // already printed is overwritten rather than duplicated.
        std::string plain = "\r" + prolog + sep + barstr;
        plain += "  " + std::string(pct);
        if (tail.counter) plain += "  " + cnt;
        if (tail.speed && !spd.empty()) plain += spd;
        if (tail.eta && !eta.empty()) plain += eta;
        plain += "\n";
        fwrite(plain.data(), 1, plain.size(), stderr);
        fflush(stderr);
        g_shown = prolog;
        g_shown_valid = true;
        g_bar_active = false;
        g_last_green = green;
        return;
    }

    std::string frame;
    frame += '\r';
    frame += prolog;
    frame += sep;
    frame += fbcolor::Code(green ? fbcolor::C::Green : fbcolor::C::Cyan);
    frame += barstr;
    frame += fbcolor::Code(fbcolor::C::Reset);
    frame += "  ";
    frame += fbcolor::Code(fbcolor::C::Bold);
    frame += pct;
    frame += fbcolor::Code(fbcolor::C::Reset);
    if (tail.counter) {
        frame += "  ";
        frame += fbcolor::Code(fbcolor::C::Dim);
        frame += cnt;
        frame += fbcolor::Code(fbcolor::C::Reset);
    }
    if (tail.speed && !spd.empty()) {
        frame += fbcolor::Code(fbcolor::C::Yellow);
        frame += spd;
        frame += fbcolor::Code(fbcolor::C::Reset);
    }
    if (tail.eta && !eta.empty()) frame += fbcolor::Paint(fbcolor::C::Dim, eta);

    // Erase whatever this frame did not overwrite, and repaint unconditionally
    // when the bar colour just changed (the final green state must survive
    // even on a terminal that ignores \033[K).
    frame += fbcolor::ClearToEol();

    fwrite(frame.data(), 1, frame.size(), stderr);
    fflush(stderr);
    g_shown = prolog;
    g_shown_valid = true;
    g_bar_active = true;
    g_last_green = green;
}

}  // namespace

void Arm(const std::string& message) {
    g_armed = true;
    g_bar_active = false;
    g_hidden = false;
    g_msg = message;
    g_shown.clear();
    g_shown_valid = false;
    g_sample_head = 0;
    g_sample_count = 0;
    g_last_draw_t = 0.0;
    g_last_draw_bytes = 0;
    if (g_enabled) {
        // Sample the terminal up front: the prolog is padded to 50 characters
        // by upstream's Status(), and CJK translations occupy more columns
        // than that, so the usable bar width depends on it.
        g_cols = TermCols();
    }
}

void Update(uint64_t current, uint64_t total) {
    if (!g_enabled || !g_armed || g_hidden) return;
    if (total == 0) return;

    // Throttle: skip cheaply unless we advanced enough to matter.
    if (g_bar_active && current - g_last_draw_bytes < g_granularity) return;

    double t = Now();
    // Throttle by time as well (the sparse path calls per 1 KiB chunk).
    if (g_bar_active && (t - g_last_draw_t) < 0.08 && current < total) return;

    g_samples[g_sample_head].t = t;
    g_samples[g_sample_head].bytes = current;
    g_sample_head = (g_sample_head + 1) % 8;
    if (g_sample_count < 8) g_sample_count++;

    g_granularity = std::max<uint64_t>(total / 400, 65536);
    g_cols = TermCols();
    Draw(current, total);
    g_last_draw_t = t;
    g_last_draw_bytes = current;
}

void Finish() {
    if (!g_enabled) {
        g_armed = false;
        g_bar_active = false;
        return;
    }
    if (g_bar_active && !g_hidden) {
        // Leave the line exactly as upstream would have: \r, the padded prolog,
        // then erase the bar region. The erase deliberately does not depend on
        // colours being enabled (NO_COLOR + tty used to leave the bar's tail
        // stuck to the OKAY line).
        fputc('\r', stderr);
        const int cols = TermCols();
        const bool padded_fits = cols <= 0 || DisplayWidth(g_msg) <= (size_t)cols;
        if (!g_shown_valid || padded_fits) {
            // Reproduce upstream's line exactly: the padded prolog, so that
            // "OKAY [...]" starts in the same column it always has.
            fputs(g_msg.c_str(), stderr);
        } else {
            // The padded prolog is wider than the terminal (it is only 50
            // *characters*, but CJK counts two columns each). Reuse the short
            // form the bar drew, so the result fits and stays on one line.
            fputs(g_shown.c_str(), stderr);
            if (!g_shown.empty()) fputc(' ', stderr);
        }
        fputs(fbcolor::ClearToEol(), stderr);
        fflush(stderr);
    }
    g_armed = false;
    g_bar_active = false;
    g_hidden = false;
}

bool Active() { return g_enabled && g_armed && g_bar_active && !g_hidden; }

void Hide() {
    if (!g_enabled || !g_bar_active || g_hidden) return;
    fputc('\r', stderr);
    fputs(fbcolor::ClearToEol(), stderr);
    fflush(stderr);
    g_hidden = true;
}

void Restore() {
    if (!g_enabled || !g_hidden) return;
    g_hidden = false;
    // Force a redraw on the next Update by rewinding the throttle.
    g_last_draw_bytes = 0;
    g_last_draw_t = 0.0;
}

void SetEnabled(bool enabled) { g_enabled = enabled; }

void Init() { g_enabled = Detect(); }

}  // namespace fbprogress