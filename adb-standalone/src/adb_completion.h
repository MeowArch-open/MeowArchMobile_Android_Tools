/*
 * adb_completion.h — generates shell completion scripts for bash, zsh and fish.
 *
 * Not an AOSP file: MeowArchMobile glue. `adb --shell-completion=bash|zsh|fish
 * [COMMAND]` prints a ready-to-source script; the package builds install one per
 * command name (adb-m and adb-meowarch) into each shell's vendor directory.
 *
 * Generating from the binary is what keeps the vocabulary honest: commands come
 * from the same help table `adb help` renders (adb_help::Commands), so a command
 * that stops being documented stops completing, and one that is added gets
 * completion without touching a shell script.
 *
 * Beyond the host side, this completes the DEVICE side too, which upstream's
 * adb.bash does only partially:
 *   * remote paths for push/pull (and inside `shell`) — via `adb shell ls -dp`
 *   * the device's own executables after `adb shell <TAB>`
 *   * package names for uninstall (via `adb shell pm list packages`)
 *   * dumpsys/am/pm subcommands
 * Device queries are read-only and time-limited; when no device is attached
 * they come back empty and the shell falls back to host-side files.
 */
#pragma once

#include <string>

namespace adbcompletion {

// Returns "" for an unknown shell (the caller turns that into an error).
// |command| is argv[0]-style: the script completes that name, so the package can
// generate one script per installed alias.
std::string Generate(const std::string& shell, const std::string& command);

// The shells Generate() understands, space separated, for error messages.
const char* SupportedShells();

}  // namespace adbcompletion
