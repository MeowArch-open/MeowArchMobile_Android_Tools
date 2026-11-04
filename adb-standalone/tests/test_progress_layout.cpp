/*
 * test_progress_layout.cpp — deterministic check of the transfer bar's layout.
 *
 * Not shipped: a test binary. The bar's contract is that its column position
 * and length do not depend on the file name, and that a frame never exceeds
 * the terminal width (a wrapped frame cannot be erased with "\r", which is what
 * turns a progress bar into a wall of noise). Both properties are impossible to
 * check reliably through a real transfer — loopback pushes finish before enough
 * frames are emitted — so this drives adb_progress directly with a set of
 * labels, totals and byte counts, and the Python harness around it
 * (scripts/test-progress-layout.py) measures the resulting frames.
 *
 * Usage: test_progress_layout "<label>" <total> <done>...
 * Each <done> emits one frame for the same armed transfer, so the harness sees
 * several labels of wildly different lengths in sequence and can compare the
 * column each one's bar starts at.
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <string>

#include "adb_progress.h"
#include "fb_color.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s LABEL TOTAL [DONE...]\n", argv[0]);
        return 2;
    }
    // Init() decides whether stderr interprets ANSI, which is what selects the
    // repainting (\r) path over the single-line-at-the-end fallback. Without it
    // g_escapes_ok stays false and the bar never animates — the test would then
    // measure the wrong renderer.
    fbcolor::Init();
    adbprogress::SetEnabled(true);
    // One frame per Update(), so the harness sees a frame for every byte count
    // it asks about instead of whatever the redraw throttle happened to keep.
    adbprogress::SetThrottled(false);
    const std::string label = argv[1];
    const uint64_t total = strtoull(argv[2], nullptr, 10);
    // TEST_PROGRESS_MODE=percent exercises the percentage-driven mode
    // (adb bugreport): every remaining argument is then a 0..100 percentage
    // instead of a byte count, and the frame carries no counter or speed.
    const char* mode = getenv("TEST_PROGRESS_MODE");
    const bool percent = mode && !strcmp(mode, "percent");
    if (percent) adbprogress::ArmPercent(label);
    else adbprogress::Arm(label, total);
    // The speed field is computed from the elapsed time between samples, so a
    // harness that calls Update() back to back renders it blank and its layout
    // assertions prove nothing. TEST_PROGRESS_FRAME_DELAY makes the harness wait
    // between frames so the speed field really is there.
    const char* delay_env = getenv("TEST_PROGRESS_FRAME_DELAY_MS");
    const unsigned delay_ms = delay_env ? (unsigned)strtoul(delay_env, nullptr, 10) : 0;
    for (int i = 3; i < argc; i++) {
        if (delay_ms && i > 3) usleep(delay_ms * 1000);
        if (percent) adbprogress::UpdatePercent((int)strtoul(argv[i], nullptr, 10));
        else adbprogress::Update(strtoull(argv[i], nullptr, 10));
    }
    adbprogress::Finish();
    return 0;
}
