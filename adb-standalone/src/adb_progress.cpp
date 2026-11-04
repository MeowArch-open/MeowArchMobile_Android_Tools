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
#include <vector>

#include "fb_color.h"

namespace adbprogress {
namespace {

bool g_enabled = false;
bool g_armed = false;
bool g_bar_active = false;
bool g_throttled = true;
bool g_finishing = false;  // set by Finish() so the no-ANSI path prints its one line
bool g_percent_mode = false;  // ArmPercent(): total is 100 and bytes mean nothing

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

const char* const kUnits[] = {"B", "KiB", "MiB", "GiB", "TiB"};

// Chooses the unit for a size the usual way (largest that keeps it under 1024).
int UnitFor(double bytes) {
    int u = 0;
    while (bytes >= 1024.0 && u < 4) {
        bytes /= 1024.0;
        u++;
    }
    return u;
}

void FmtBytes(double bytes, char* out, size_t n) {
    int u = UnitFor(bytes);
    for (int i = 0; i < u; i++) bytes /= 1024.0;
    if (u == 0) snprintf(out, n, "%.0f %s", bytes, kUnits[u]);
    else snprintf(out, n, "%.1f %s", bytes, kUnits[u]);
}

// Formats |bytes| in a FIXED unit, so a counter's width cannot change as the
// number grows ("999 KiB" -> "1.0 MiB" is one of the things that made the old
// bar shift). The number is left-padded to |num_w| columns, so the whole field
// is always exactly num_w + 1 + strlen(unit) columns wide.
void FmtBytesInUnit(double bytes, int unit, int num_w, char* out, size_t n) {
    for (int i = 0; i < unit; i++) bytes /= 1024.0;
    char num[32];
    if (unit == 0) snprintf(num, sizeof(num), "%.0f", bytes);
    else snprintf(num, sizeof(num), "%.1f", bytes);
    snprintf(out, n, "%*s %s", num_w, num, kUnits[unit]);
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

// Shortens |s| to exactly |cols| display columns by dropping the middle and
// inserting an ellipsis, keeping both ends: for a path that preserves the
// directory context AND the file name, which is what the reader needs. Used for
// the label column, whose width must not depend on the label's own length —
// otherwise the bar slides sideways every time the file changes.
std::string ElideMiddleToCols(const std::string& s, size_t cols) {
    if (DisplayWidth(s) <= cols) return s;
    if (cols <= 1) return std::string(cols, ' ');
    // Decode into code points so a multi-byte character is never cut in half.
    std::vector<std::string> cps;
    std::vector<size_t> widths;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (i + len > s.size()) len = 1;
        std::string cp = s.substr(i, len);
        cps.push_back(cp);
        widths.push_back(DisplayWidth(cp));
        i += len;
    }
    const size_t ell = DisplayWidth("\xE2\x80\xA6");  // one column
    size_t budget = cols > ell ? cols - ell : 0;
    size_t head_budget = budget / 2;
    size_t tail_budget = budget - head_budget;
    size_t head = 0, head_w = 0;
    while (head < cps.size() && head_w + widths[head] <= head_budget) {
        head_w += widths[head];
        head++;
    }
    size_t tail = 0, tail_w = 0;
    while (tail < cps.size() - head && tail_w + widths[cps.size() - 1 - tail] <= tail_budget) {
        tail_w += widths[cps.size() - 1 - tail];
        tail++;
    }
    std::string out;
    for (size_t i = 0; i < head; i++) out += cps[i];
    out += "\xE2\x80\xA6";
    for (size_t i = cps.size() - tail; i < cps.size(); i++) out += cps[i];
    // Wide characters can leave the result one column short; pad it out so the
    // column width is exact.
    size_t w = DisplayWidth(out);
    if (w < cols) out += std::string(cols - w, ' ');
    return out;
}

// Pads |s| on the right to exactly |cols| display columns (truncating if longer).
std::string PadToCols(const std::string& s, size_t cols) {
    size_t w = DisplayWidth(s);
    if (w == cols) return s;
    if (w > cols) return ElideMiddleToCols(s, cols);
    return s + std::string(cols - w, ' ');
}

double Speed() {
    if (g_count < 2) return 0.0;
    int oldest = (g_head - g_count + 8) % 8;
    int newest = (g_head - 1 + 8) % 8;
    double dt = g_samples[newest].t - g_samples[oldest].t;
    if (dt <= 0.0001) return 0.0;
    return ((double)g_samples[newest].bytes - (double)g_samples[oldest].bytes) / dt;
}

// --------------------------------------------------------------------------
// Layout.
//
// The bar's column position and length are FIXED for the whole transfer: they
// are derived from the terminal width and from byte-count widths (which depend
// only on |g_total|, a constant), never from the label's length. The label gets
// a column of its own and is middle-elided ("…/system/framework/services.jar")
// to fit it exactly. That is what keeps the bar from sliding sideways as the
// file changes or as numbers grow — the previous version measured the label per
// frame, so a long file name pushed the bar right and a short one pulled it
// back.
//
//   <label, exactly L cols>  <bar, exactly W cols>  NN%  <cur>/<tot>  <speed>
//
// Fields:
//   kBarW     bar width, shrunk only when the terminal cannot fit everything
//   kPctW     5  (" 100%")
//   counter   width from the TOTAL's digits, both sides in that same unit, so
//             "999.5 KiB" never becomes "1.0 MiB" mid-transfer
//   kSpeedW   12 (right-aligned; the unit can change without moving anything)

constexpr size_t kBarW = 22;          // plus the two brackets = 24 columns
constexpr size_t kBarFloor = 12;      // the bar shrinks to here before fields go
constexpr size_t kPctW = 5;
constexpr size_t kSpeedW = 12;
constexpr size_t kMinBar = 8;         // absolute floor, only for tiny terminals
constexpr size_t kMinLabel = 6;
// Aim to leave the label at least this much room: at 80 columns a full-width
// bar leaves ~10 columns for the path, which is too little to tell two files
// apart, while a 12-column bar still reads clearly. The bar is only shrunk
// (never moved), so its length stays fixed for the whole transfer either way.
constexpr size_t kPreferredLabel = 18;

struct Layout {
    size_t label_w;     // exactly this many display columns
    size_t bar_w;       // characters between the brackets
    size_t num_w;       // digits+dot for a byte count in |unit|
    size_t counter_w;   // "cur/tot", both in |unit|
    bool show_bar;      // false when there is no total to fill against
    bool show_pct;      // percentage field
    bool show_counter;  // "cur/tot" field
    bool show_speed;    // instantaneous-rate field
    int unit;           // the unit every byte count in this transfer uses
};

// Recomputed per frame (cheap, and picks up a resize). The label is the only
// field whose width is derived rather than fixed: everything else has a width
// that depends solely on |total| and the terminal, so the bar's column cannot
// move when the file name changes.
//
// Degradation ladder for narrow terminals — drop the speed field, then shrink
// the bar to its floor, then drop the counter, then the percentage. The
// invariant FrameWidth(L) <= cols holds at every rung, because a frame wider
// than the terminal wraps, and a wrapped line cannot be erased by "\r" (every
// previous frame then stays on screen, which is the noise this exists to stop).
Layout ComputeLayout(int cols, uint64_t total) {
    Layout L{};
    L.unit = UnitFor((double)total);
    // Widest number we will ever print in that unit: the total itself.
    double t = (double)total;
    for (int i = 0; i < L.unit; i++) t /= 1024.0;
    char num[32];
    if (L.unit == 0) snprintf(num, sizeof(num), "%.0f", t);
    else snprintf(num, sizeof(num), "%.1f", t);
    L.num_w = strlen(num);

    const size_t unit_w = strlen(kUnits[L.unit]);
    // One side of the counter is "<num> <unit>", e.g. "293.0 KiB".
    const size_t side_w = L.num_w + 1 + unit_w;
    L.counter_w = 2 * side_w + 1;  // plus the "/"

    size_t avail = cols > 0 ? (size_t)cols : 80;
    L.bar_w = kBarW;
    L.show_speed = true;
    L.show_counter = true;
    L.show_bar = total != 0;
    L.show_pct = total != 0;  // a percentage of an unknown total is meaningless
    if (g_percent_mode) {
        // No byte counter and no speed: those are meaningless over a fraction
        // reported by the device. The bar and the percentage are the whole
        // story, which also makes this the narrowest of the three modes.
        L.show_counter = false;
        L.show_speed = false;
    }

    if (total == 0) {
        // Streaming with no total (backup, restore, exec-out, and anything that
        // pipes an unbounded amount). The value is printed in whatever unit
        // suits its CURRENT magnitude, so the reservation has to cover the
        // widest rendering any unit can produce — not just the biggest unit.
        // "1005.0 KiB" (10 columns, just after the value crosses 1 MiB) is
        // wider than "999.9 TiB" (9), and reserving only the TiB form let the
        // counter overflow its field by a column mid-transfer, pushing the
        // frame one past the terminal width.
        size_t widest = 0;
        for (int u = 0; u <= 4; u++) {
            // Largest value that still prints in unit u, in that unit.
            const double biggest = (u == 0) ? 1023.0 : 1023.9;
            char num[32];
            if (u == 0) snprintf(num, sizeof(num), "%.0f", biggest);
            else snprintf(num, sizeof(num), "%.1f", biggest);
            size_t w = strlen(num) + 1 + strlen(kUnits[u]);
            if (w > widest) widest = w;
        }
        L.unit = 4;  // TiB, the ceiling the reservation is sized against
        L.num_w = strlen("999.9");
        L.counter_w = widest;
    }

    // Width of every field except the label and its separator.
    auto rest = [&]() {
        size_t w = L.show_bar ? 2 + (L.bar_w + 2) : 0;  // "  [bar]"
        if (L.show_pct) w += 2 + kPctW;
        if (L.show_counter) w += 2 + L.counter_w;
        if (L.show_speed) w += 2 + kSpeedW;
        return w;
    };
    // Widest bar that still leaves the label its preferred room; never below
    // kBarFloor, and never above kBarW. Skipped when there is no bar.
    if (L.show_bar) {
        for (size_t w = kBarW; w >= kBarFloor; w--) {
            L.bar_w = w;
            if (avail >= rest() + kPreferredLabel) break;
            if (w == kBarFloor) break;
        }
    }
    // Still too tight: give up fields before shrinking the bar past the floor.
    while (avail < rest() + kMinLabel) {
        if (L.show_speed) { L.show_speed = false; continue; }
        if (L.show_bar && L.bar_w > kMinBar) { L.bar_w = kMinBar; continue; }
        if (L.show_counter) { L.show_counter = false; continue; }
        if (L.show_pct) { L.show_pct = false; continue; }
        break;
    }
    size_t r = rest();
    L.label_w = avail > r ? avail - r : 0;
    return L;
}

// Total display width of a frame. Equals the terminal width whenever the label
// has room, never more — see the invariant in ComputeLayout.
size_t FrameWidth(const Layout& L) {
    size_t w = L.label_w;
    if (L.show_bar) w += 2 + (L.bar_w + 2);
    if (L.show_pct) w += 2 + kPctW;
    if (L.show_counter) w += 2 + L.counter_w;
    if (L.show_speed) w += 2 + kSpeedW;
    return w;
}

void Draw(uint64_t done) {
    const int cols = TermCols() > 0 ? TermCols() : 80;
    const Layout L = ComputeLayout(cols, g_total);

    // Byte counts, always padded to the width the layout reserved, so the
    // counter column never moves. With a known total both sides use that
    // total's unit (so "999.5 KiB" cannot become "1.0 MiB" mid-transfer and
    // shift everything after it). With no total, the unit follows the current
    // value but the reserved width keeps the field fixed.
    char cur[48], tot[48];
    tot[0] = '\0';
    if (g_total) {
        FmtBytesInUnit((double)done, L.unit, (int)L.num_w, cur, sizeof(cur));
        FmtBytesInUnit((double)g_total, L.unit, (int)L.num_w, tot, sizeof(tot));
    } else {
        double b = (double)done;
        int u = UnitFor(b);
        for (int i = 0; i < u; i++) b /= 1024.0;
        char num[32];
        snprintf(num, sizeof(num), "%.1f", b);
        // The unit's own length varies with magnitude ("B" vs "KiB"), so pad the
        // WHOLE field to the reserved width rather than just the number: the
        // counter column must not move when the value crosses a unit.
        char field[48];
        snprintf(field, sizeof(field), "%s %s", num, kUnits[u]);
        snprintf(cur, sizeof(cur), "%*s", (int)L.counter_w, field);
        // Belt and braces: never emit a field wider than the layout reserved,
        // even if a rendering slips past the width table above. Drop from the
        // left, since the significant digits are on the right.
        if (strlen(cur) > L.counter_w) {
            const char* tail = cur + (strlen(cur) - L.counter_w);
            snprintf(cur, sizeof(cur), "%s", tail);
        }
    }

    int pct = g_total ? (int)((double)done * 100.0 / (double)g_total + 0.5) : -1;
    if (pct > 100) pct = 100;
    char pctbuf[8];
    // kPctW reserves 5 columns and documents them as " 100%", so the number is
    // printed in a 4-wide field: "  42%" and " 100%" are then both exactly 5.
    // "%3d%%" gave 4 for anything under 100, which made the frame one column
    // narrower than the layout reserved and left the label one short.
    if (pct >= 0) snprintf(pctbuf, sizeof(pctbuf), "%4d%%", pct);
    else snprintf(pctbuf, sizeof(pctbuf), "%3s", "?");

    // Speed: right-aligned in a fixed field, so its own unit choice cannot
    // shift anything.
    double sp = Speed();
    char spbuf[32] = "";
    if (sp > 1.0) {
        char spu[24];
        FmtBytes(sp, spu, sizeof(spu));
        snprintf(spbuf, sizeof(spbuf), "%s/s", spu);
    }
    char spfield[48];
    snprintf(spfield, sizeof(spfield), "%*s", (int)kSpeedW, spbuf);

    // Guard the invariant rather than trusting the arithmetic: if a frame would
    // still exceed the terminal, take the excess out of the label. A wrapped
    // frame is the one failure mode that turns a bar into noise, so it is worth
    // a check that should never fire.
    size_t label_w = L.label_w;
    size_t fw = FrameWidth(L);
    if ((size_t)cols < fw) {
        label_w = fw > (size_t)cols ? 0 : label_w - (fw - (size_t)cols);
    }
    const std::string label = PadToCols(g_label, label_w);

    std::string bar;
    if (L.show_bar) {
        int filled = (int)((double)done / (double)g_total * (double)L.bar_w);
        if (filled > (int)L.bar_w) filled = (int)L.bar_w;
        bar = "[";
        for (size_t i = 0; i < L.bar_w; i++) {
            if ((int)i < filled) bar += '#';
            else if ((int)i == filled && pct >= 0 && pct < 100) bar += '>';
            else bar += '-';
        }
        bar += ']';
    }

    const bool show_speed = L.show_speed;

    std::string frame;
    if (fbcolor::EscapesOk()) {
        frame += '\r';
        frame += fbcolor::Paint(fbcolor::C::Cyan, label);
        if (L.show_bar) {
            frame += "  ";
            frame += fbcolor::Code(pct >= 100 ? fbcolor::C::Green : fbcolor::C::Cyan);
            frame += bar;
            frame += fbcolor::Code(fbcolor::C::Reset);
        }
        if (L.show_pct) {
            frame += "  ";
            frame += fbcolor::Code(fbcolor::C::Bold);
            frame += pctbuf;
            frame += fbcolor::Code(fbcolor::C::Reset);
        }
        if (L.show_counter) {
            frame += "  ";
            frame += fbcolor::Code(fbcolor::C::Dim);
            frame += cur;
            if (g_total) {
                frame += "/";
                frame += tot;
            }
            frame += fbcolor::Code(fbcolor::C::Reset);
        }
        if (show_speed) {
            frame += "  ";
            frame += fbcolor::Code(fbcolor::C::Yellow);
            frame += spfield;
            frame += fbcolor::Code(fbcolor::C::Reset);
        }
        frame += fbcolor::ClearToEol();
        fwrite(frame.data(), 1, frame.size(), stderr);
        fflush(stderr);
        g_bar_active = true;
        return;
    }

    // No ANSI on this terminal: one plain line at the end of the transfer
    // only, instead of smearing frames over each other. Without a total there
    // is no "end" to detect from the numbers, so Finish() sets the flag.
    if (!g_finishing) {
        if (g_total && done < g_total) return;
        if (!g_total) return;
    }
    frame = label;
    if (L.show_bar) frame += "  " + bar;
    if (L.show_pct) frame += "  " + std::string(pctbuf);
    if (L.show_counter) frame += "  " + std::string(cur) + (g_total ? "/" + std::string(tot) : "");
    if (show_speed) frame += "  " + std::string(spfield);
    frame += "\n";
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
void SetThrottled(bool throttled) { g_throttled = throttled; }
bool Enabled() { return g_enabled; }

void ArmCommon(const std::string& label, uint64_t total, bool percent) {
    g_armed = true;
    g_bar_active = false;
    g_finishing = false;
    // Percent mode must be set BEFORE the first Draw(): ComputeLayout reads it
    // to decide which fields exist, so arming first and flipping the flag after
    // paints one frame with a byte counter and speed, then drops them — a width
    // jump on the very first frame, which is the thing this whole module exists
    // to prevent.
    g_percent_mode = percent;
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

void Arm(const std::string& label, uint64_t total) { ArmCommon(label, total, false); }

void ArmPercent(const std::string& label) {
    // The caller re-arms on every progress report (bugreportz emits one line per
    // percent), so re-arming with the same label must NOT reset the transfer:
    // Arm() clears the byte count and the rate samples, which would flash the
    // bar back to 0% between updates. Only the first arm — or a label change,
    // which means a new phase, e.g. "generating" turning into "pulling" —
    // resets.
    if (g_armed && g_percent_mode) {
        SetLabel(label);
        return;
    }
    ArmCommon(label, 100, true);
}

void UpdatePercent(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    Update(static_cast<uint64_t>(pct));
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
    // In percent mode `done` is 0..100, so the byte-based granularity below
    // (never under 64 KiB) would swallow every update. Throttle on time only.
    uint64_t granularity = g_percent_mode ? 0 : std::max<uint64_t>(g_total / 100, 65536);
    if (g_throttled) {
        if (g_bar_active && (t - g_last_draw_t) < 0.08 && done < g_total) return;
        if (g_bar_active && granularity && done - g_last_draw_bytes < granularity &&
            done < g_total) {
            return;
        }
    }

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
    // No-ANSI terminals get their single summary line here (a transfer with no
    // known total has nothing to key the "last frame" off, so it has to be
    // signalled).
    g_finishing = true;
    if (g_enabled && !fbcolor::EscapesOk()) Draw(g_done);
    g_finishing = false;
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