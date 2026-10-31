/*
 * fb_progress.h — live transfer progress bar for the standalone fastboot build.
 *
 * Not an AOSP file: MeowArchMobile glue. Renders a single-line progress bar
 * with instantaneous speed and ETA while a download/upload/fetch is in flight.
 *
 * Contract with the caller (fastboot.cpp's Status/Epilog + the driver's
 * progress callback):
 *   Arm(message)            called by Status() when a command starts; records
 *                           the message so the bar can redraw the whole line.
 *   Update(current, total)  called by the driver as bytes move; the first call
 *                           arms the actual bar.
 *   Finish()                called by Epilog(); erases the bar and leaves the
 *                           cursor where upstream would have left it.
 *
 * When stderr is not a tty (pipe, CI log, file) every function is a no-op, so
 * output stays byte-identical to upstream fastboot.
 */
#pragma once

#include <cstdint>
#include <string>

namespace fbprogress {

// Call once from main(). Detects tty/NO_PROGRESS.
void Init();

// Begin a command. |message| is the prolog text already printed (without its
// trailing newline); the bar redraws it together with the bar.
void Arm(const std::string& message);

// Report transfer progress. First non-zero-total call arms the bar.
void Update(uint64_t current, uint64_t total);

// Erase the bar (if any) and finish the line's dynamic region. Safe to call
// when nothing was armed.
void Finish();

// True while a bar is on screen — used by other output paths (INFO messages,
// warnings) so they can clear the line before printing and restore after.
bool Active();

// Called before writing an unrelated line while a bar is active: hides the
// bar. Call Restore() afterwards to redraw it.
void Hide();
void Restore();

// --no-color / --no-progress style kill switch (also honors NO_PROGRESS).
void SetEnabled(bool enabled);

}  // namespace fbprogress
