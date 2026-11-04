/*
 * adb_progress.h — single-line transfer bar for the standalone adb client.
 *
 * Not an AOSP file: MeowArchMobile glue. Upstream's LinePrinter already
 * overprints one line per file ("[ 42%] name"), but it reports only whole
 * percentages of the *current* file and nothing about speed, which for a large
 * push reads as a stutter. This renders the same overprinted line with a bar,
 * bytes, speed and ETA, sized to the terminal.
 *
 * Contract with the caller (client/file_sync_client.cpp's TransferLedger):
 *   Arm(label, total)     new transfer starts; label is what to show first
 *   Update(done)          bytes moved so far
 *   Finish()              erase the bar, leaving the line to the caller
 *
 * Everything is a no-op when stderr is not a tty (pipe/CI log) or when
 * NO_PROGRESS is set / --no-progress was given, so scripted output stays
 * byte-identical to upstream.
 */
#pragma once

#include <cstdint>
#include <string>

namespace adbprogress {

void Init();
void SetEnabled(bool enabled);
bool Enabled();

// Start a transfer: |label| names what is moving (a file name, or a summary
// like "3 files"), |total| is the byte count when known. 0 means unknown — the
// bar and percentage are then omitted (`adb backup`, `restore`, `exec-out`
// stream an amount nobody knows up front) but the byte counter and speed still
// render, at a fixed width, so those long-running commands are not silent.
// Re-arming also resets the speed samples, so call it once per transfer.
void Arm(const std::string& label, uint64_t total);
// Change the label of the currently armed transfer without resetting it: a
// directory push runs one bar over the whole batch, but the name shown should
// follow the file being copied. No-op when nothing is armed.
void SetLabel(const std::string& label);
// Redraw throttling: frames are dropped unless ~80ms and ~1% of the transfer
// have passed, so a fast device does not spend its time repainting. Turn it off
// to make every Update() emit a frame, which is what the layout test needs
// (a deterministic frame per byte count). On by default.
void SetThrottled(bool throttled);
void Update(uint64_t done);

// Percentage-driven mode, for progress that is reported as a fraction rather
// than as bytes (`adb bugreport`, where the device's bugreportz emits
// "PROGRESS:<done>:<total>"). Arms with a total of 100 and renders bar +
// percentage only: a byte counter and a speed over a percentage would be
// nonsense, so both are omitted and the frame is
//   <label>  [####------]  42%
// at a fixed width.
void ArmPercent(const std::string& label);
void UpdatePercent(int pct);

void Finish();

}  // namespace adbprogress
