/*
 * fb_i18n_en.cpp — English message table.
 *
 * Help entries (the kH... / kO... message IDs) are deliberately absent from
 * this table: the renderer falls back to the row text generated from upstream
 * fastboot.cpp (scripts/gen-help-entries.py), which guarantees byte-identical
 * English help without duplicating ~90 strings. Only runtime strings live in
 * this table, matching upstream wording exactly (CI diffs against a captured
 * baseline).
 */
#include <iterator>

#include "fb_i18n.h"

namespace fbi18n {

static const MsgEntry kEnTable[] = {
    // -- runtime: status / epilog ---------------------------------------------
    {kOkay, "OKAY"},
    {kFailed, "FAILED"},
    {kFinished, "Finished. Total time: %.3fs\n"},
    {kWaitingDevice, "< waiting for %s >"},
    {kWaitingFor, "< waiting for %s >"},
    {kWaitingAny, "< waiting for any device >"},
    // -- runtime: errors -------------------------------------------------------
    {kErrPrefix, "fastboot: error: "},
    {kUsagePrefix, "fastboot: usage: "},
    {kVerbosePrefix, "fastboot: verbose: "},
    {kUnknownCommand, "unknown command %s"},
    {kUnknownOption, "unknown option %s"},
    {kUnknownPartition, "unknown partition '%s'"},
    {kDidYouMean, "Did you want '%s'?"},
    {kDidYouMeanList, "Did you want one of: %s?"},
    {kRunHelp, "Run 'fastboot --help' for usage."},
    {kNoCommand, "no command"},
    {kExpectedArgument, "expected argument"},
    {kEmptyOemCommand, "empty oem command"},
    {kJunkAfterReboot, "junk after reboot command"},
    {kMissingFlashingCommand, "missing 'flashing' command"},
    {kUnknownFlashingCommand, "unknown 'flashing' command %s"},
    {kInvalidGsiCommand, "invalid gsi command"},
    {kExpectedSnapshotUpdate, "expected: snapshot-update [cancel|merge]"},
    {kUnsupportedFsOption, "unsupported options: %s"},
    // -- driver prologs (printf formats; arguments as upstream) ----------------
    {kSending, "Sending '%s' (%zu KB)"},
    {kSendingSparse, "Sending sparse '%s' %zu/%zu (%u KB)"},
    {kWriting, "Writing '%s'"},
    {kErasing, "Erasing '%s'"},
    {kUploading, "Uploading '%s'"},
    {kFetching, "Fetching %s (offset=%" PRIx64 ", size=%" PRIx64 ")"},
    {kBooting, "Booting"},
    {kResumingBoot, "Resuming boot"},
    {kRebooting, "Rebooting"},
    {kRebootingInto, "Rebooting into %s"},
    {kCreating, "Creating '%s'"},
    {kDeleting, "Deleting '%s'"},
    {kResizing, "Resizing '%s'"},
    {kSettingSlot, "Setting current slot to '%s'"},
    {kSnapshot, "Snapshot %s"},
    // -- progress bar -----------------------------------------------------------
    {kEta, "ETA"},
    {kSpeedUnit, "/s"},
    // -- languages ---------------------------------------------------------------
    {kLangsHeader, "Available languages (--lang=CODE):"},
    {kLangCurrent, " (current)"},
};

void RegisterLangEn() { RegisterLang("en", "English", kEnTable, std::size(kEnTable)); }

}  // namespace fbi18n
