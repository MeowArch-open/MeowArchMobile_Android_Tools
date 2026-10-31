/*
 * fb_progress.cpp — see fb_progress.h. MeowArchMobile glue, not an AOSP file.
 *
 * Renders a single-line bar on stderr while a transfer runs. All entry points
 * early-return when disabled (non-tty stderr, NO_PROGRESS, --no-progress) so
 * piped/CI output is byte-identical to upstream.
 */
#include "fb_progress.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

bool Detect() {
    const char* np = getenv("NO_PROGRESS");
    if (np && np[0] != '\0') return false;
    if (!isatty(STDERR_FILENO)) return false;
    const char* term = getenv("TERM");
    if (term && term[0] && !strcmp(term, "dumb")) return false;
    return true;
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

void Draw(uint64_t current, uint64_t total) {
    // \r back to col 0, reprint the prolog message, then the bar.
    fputc('\r', stderr);
    fputs(g_msg.c_str(), stderr);

    double frac = total ? (double)current / (double)total : 0.0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;

    const int width = 24;
    int filled = (int)(frac * width + 0.5);
    if (filled > width) filled = width;

    char bar[64];
    int p = 0;
    bar[p++] = '[';
    for (int i = 0; i < width; i++) {
        if (i < filled) bar[p++] = '#';
        else if (i == filled && frac > 0 && frac < 1) bar[p++] = '>';
        else bar[p++] = '-';
    }
    bar[p++] = ']';
    bar[p] = '\0';

    char pct[16];
    snprintf(pct, sizeof(pct), "%3d%%", (int)(frac * 100.0 + 0.5));

    char cur[32], tot[32];
    FmtBytes((double)current, cur, sizeof(cur));
    FmtBytes((double)total, tot, sizeof(tot));

    double sp = Speed();
    char spbuf[32] = "";
    char etabuf[32] = "";
    if (sp > 1.0) {
        char spu[32];
        FmtBytes(sp, spu, sizeof(spu));
        snprintf(spbuf, sizeof(spbuf), " %s/s", spu);
        if (total > current) {
            char eta[32];
            FmtEta(((double)total - (double)current) / sp, eta, sizeof(eta));
            snprintf(etabuf, sizeof(etabuf), " %s %s", _(fbi18n::kEta), eta);
        }
    }

    // Color the bar by completion: dim while moving, green near done.
    // NOTE: keep the %s count exactly in sync with the argument list — an
    // extra %s reads a garbage va_arg pointer and SIGSEGVs on unlucky stack
    // layouts (caught by ASAN; shipped once, see fb_progress regression test).
    fbcolor::C barcol = frac >= 1.0 ? fbcolor::C::Green : fbcolor::C::Cyan;
    fprintf(stderr, " %s%s%s %s%s%s%s  %s/%s%s",
            fbcolor::Code(barcol), bar, fbcolor::Code(fbcolor::C::Reset),
            fbcolor::Code(fbcolor::C::Bold), pct, fbcolor::Code(fbcolor::C::Reset),
            fbcolor::Code(fbcolor::C::Dim),
            cur, tot,
            fbcolor::Code(fbcolor::C::Reset));
    // speed + eta appended (already carry their own leading space)
    if (spbuf[0]) fprintf(stderr, "%s%s", fbcolor::Code(fbcolor::C::Yellow), spbuf);
    if (etabuf[0]) fprintf(stderr, "%s", fbcolor::Paint(fbcolor::C::Dim, etabuf).c_str());
    else if (spbuf[0]) fputs(fbcolor::Code(fbcolor::C::Reset), stderr);

    fputs(fbcolor::ClearToEol(), stderr);
    fflush(stderr);
    g_bar_active = true;
}

}  // namespace

void Arm(const std::string& message) {
    g_armed = true;
    g_bar_active = false;
    g_hidden = false;
    g_msg = message;
    g_sample_head = 0;
    g_sample_count = 0;
    g_last_draw_t = 0.0;
    g_last_draw_bytes = 0;
    if (g_enabled) {
        // A fresh command line: make sure any prior bar is gone.
        // (Epilog already called Finish(), so this is just belt-and-suspenders.)
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
        // Restore the line to exactly what upstream left (padded prolog msg),
        // then clear the bar region. Epilog appends OKAY/FAILED after this.
        fputc('\r', stderr);
        fputs(g_msg.c_str(), stderr);
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
