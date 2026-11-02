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
// like "3 files"), |total| is the byte count when known (0 = unknown).
void Arm(const std::string& label, uint64_t total);
void Update(uint64_t done);
void Finish();

}  // namespace adbprogress
