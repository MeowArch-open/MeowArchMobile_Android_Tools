/*
 * adb_progress.cpp — see adb_progress.h. MeowArchMobile glue, not an AOSP file.
 *
 * Rendering rules are the same ones better_fastboot's bar learned the hard way:
 *   * the frame is assembled in memory and truncated to the terminal width
 *     measured in *display columns* (CJK counts two), so it never wraps onto a
 *     second row — a wrapped row makes "\r" repaint the wrong line and every
 *     frame stays on screen;
 *   * the terminal width is re-queried per frame, so a resize is picked up;
 *   * the escape/erase sequences only go out when stderr really is a terminal
 *     that interprets them (fb_color::EscapesOk turns on Windows VT processing
 *     and returns false for a pipe).
 */
#include "adb_progress.h"

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

namespace adbprogress {
namespace {

bool g_enabled = false;
bool g_armed = false;
bool g_bar_active = false;

std::string g_label;
uint64_t g_total = 0;

// Rate sampling over a short ring, same idea as the fastboot bar.
struct Sample {
    double t;
    uint64_t bytes;
};
Sample g_samples[8];
int g_head = 0;
int g_count = 0;

double g_last_draw_t = 0.0;
uint64_t g_last_draw_bytes = 0;
uint64_t g_done = 0;

uint64_t last_done() { return g_done; }

double Now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

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

// Display columns: exact for ASCII/Latin-1, two for the CJK/fullwidth ranges.
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
        else { cols++; i++; continue; }
        if (i + len > s.size()) { cols++; i++; continue; }
        for (int k = 1; k < len; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += len;
        bool wide = (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) ||
                    (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
                    (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
                    (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
                    (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x20000 && cp <= 0x3FFFD);
        cols += wide ? 2 : 1;
    }
    return cols;
}

// Truncate to at most |cols| display columns, ending in an ellipsis.
std::string CutToCols(const std::string& s, size_t cols) {
    if (DisplayWidth(s) <= cols) return s;
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
    if (cols > 1) out += "\xE2\x80\xA6";
    return out;
}

double Speed() {
    if (g_count < 2) return 0.0;
    int oldest = (g_head - g_count + 8) % 8;
    int newest = (g_head - 1 + 8) % 8;
    double dt = g_samples[newest].t - g_samples[oldest].t;
    if (dt <= 0.0001) return 0.0;
    return ((double)g_samples[newest].bytes - (double)g_samples[oldest].bytes) / dt;
}

void Draw(uint64_t done) {
    const int cols = TermCols() > 0 ? TermCols() : 80;

    char cur[32], tot[32];
    FmtBytes((double)done, cur, sizeof(cur));
    FmtBytes((double)g_total, tot, sizeof(tot));

    int pct = g_total ? (int)((double)done * 100.0 / (double)g_total + 0.5) : -1;
    if (pct > 100) pct = 100;
    char pctbuf[8] = "  ?";
    if (pct >= 0) snprintf(pctbuf, sizeof(pctbuf), "%3d%%", pct);

    double sp = Speed();
    char spbuf[32] = "";
    if (sp > 1.0) {
        char spu[24];
        FmtBytes(sp, spu, sizeof(spu));
        snprintf(spbuf, sizeof(spbuf), " %s/s", spu);
    }

    // "<label>: 42%  1.2 MiB/3.4 MiB 12.3 MiB/s [####>-----]"
    const int kMinBar = 10, kMaxBar = 24;
    // Budgets, not per-frame measurements: the speed text changes length and
    // letting the bar chase it makes the line jitter.
    size_t counter_budget = 2 + 2 * DisplayWidth(tot) + 1;
    size_t tail_budget = 4 /*"  NN%"*/ + counter_budget + 12 /*speed*/ + 2 + kMinBar;
    std::string label = g_label;
    size_t room = (size_t)cols > tail_budget + 8 ? (size_t)cols - tail_budget : 8;
    label = CutToCols(label, room);

    int width = cols - (int)DisplayWidth(label) - (int)tail_budget;
    if (width > kMaxBar) width = kMaxBar;
    if (width < 4) width = 4;

    int filled = g_total ? (int)((double)done / (double)g_total * width) : 0;
    if (filled > width) filled = width;
    std::string bar("[");
    for (int i = 0; i < width; i++) {
        if (i < filled) bar += '#';
        else if (i == filled && pct >= 0 && pct < 100) bar += '>';
        else bar += '-';
    }
    bar += ']';

    std::string frame;
    if (fbcolor::EscapesOk()) {
        frame += '\r';
        frame += fbcolor::Paint(fbcolor::C::Cyan, label);
        frame += ": ";
        frame += fbcolor::Code(pct >= 100 ? fbcolor::C::Green : fbcolor::C::Cyan);
        frame += bar;
        frame += fbcolor::Code(fbcolor::C::Reset);
        frame += " ";
        frame += fbcolor::Code(fbcolor::C::Bold);
        frame += pctbuf;
        frame += fbcolor::Code(fbcolor::C::Reset);
        frame += "  ";
        frame += fbcolor::Code(fbcolor::C::Dim);
        frame += cur;
        frame += "/";
        frame += tot;
        frame += fbcolor::Code(fbcolor::C::Reset);
        if (spbuf[0]) {
            frame += fbcolor::Code(fbcolor::C::Yellow);
            frame += spbuf;
            frame += fbcolor::Code(fbcolor::C::Reset);
        }
        frame += fbcolor::ClearToEol();
        fwrite(frame.data(), 1, frame.size(), stderr);
        fflush(stderr);
        g_bar_active = true;
        return;
    }

    // No ANSI on this terminal: one plain line at the end of the transfer
    // only, instead of smearing frames over each other.
    if (g_total && done < g_total) return;
    frame = label + ": " + pctbuf + "  " + cur + "/" + tot + spbuf + "\n";
    fwrite(frame.data(), 1, frame.size(), stderr);
    fflush(stderr);
    g_bar_active = false;
}

}  // namespace

void Init() {
    const char* np = getenv("NO_PROGRESS");
    if (np && np[0] != '\0') {
        g_enabled = false;
        return;
    }
    if (!StderrIsTerminal()) {
        g_enabled = false;
        return;
    }
    const char* term = getenv("TERM");
    if (term && !strcmp(term, "dumb")) {
        g_enabled = false;
        return;
    }
    g_enabled = true;
}

void SetEnabled(bool enabled) { g_enabled = enabled; }
bool Enabled() { return g_enabled; }

void Arm(const std::string& label, uint64_t total) {
    g_armed = true;
    g_bar_active = false;
    g_label = label;
    g_total = total;
    g_head = 0;
    g_count = 0;
    g_done = 0;
    g_last_draw_t = 0.0;
    g_last_draw_bytes = 0;
    if (g_enabled) {
        Draw(0);
        g_last_draw_t = Now();
    }
}

void SetLabel(const std::string& label) {
    if (!g_armed) return;
    if (g_label != label) {
        g_label = label;
        Draw(last_done());
    }
}

void Update(uint64_t done) {
    if (!g_enabled || !g_armed) return;

    g_done = done;
    double t = Now();
    // 80ms / ~1% of the transfer, whichever comes later — same cadence the
    // fastboot bar uses, so a big push repaints smoothly instead of spinning.
    uint64_t granularity = std::max<uint64_t>(g_total / 100, 65536);
    if (g_bar_active && (t - g_last_draw_t) < 0.08 && done < g_total) return;
    if (g_bar_active && done - g_last_draw_bytes < granularity && done < g_total) return;

    g_samples[g_head].t = t;
    g_samples[g_head].bytes = done;
    g_head = (g_head + 1) % 8;
    if (g_count < 8) g_count++;

    Draw(done);
    g_last_draw_t = t;
    g_last_draw_bytes = done;
}

void Finish() {
    g_armed = false;
    if (!g_enabled || !g_bar_active) {
        g_bar_active = false;
        return;
    }
    // Leave the cursor at column 0 of a cleared line; the caller prints the
    // result line next (the "[42%] foo" summary upstream already emits).
    fputc('\r', stderr);
    fputs(fbcolor::ClearToEol(), stderr);
    fflush(stderr);
    g_bar_active = false;
}

}  // namespace adbprogress