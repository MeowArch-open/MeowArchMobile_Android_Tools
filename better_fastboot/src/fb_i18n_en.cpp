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
    {kRunHelp, "Run 'fastboot-m --help' for usage."},
    {kNoCommand, "no command"},
    {kExpectedArgument, "expected argument"},
    {kEmptyOemCommand, "empty oem command"},
    {kJunkAfterReboot, "junk after reboot command"},
    {kMissingFlashingCommand, "missing 'flashing' command"},
    {kUnknownFlashingCommand, "unknown 'flashing' command %s"},
    {kInvalidGsiCommand, "invalid gsi command"},
    {kExpectedSnapshotUpdate, "expected: snapshot-update [cancel|merge]"},
    {kUnsupportedFsOption, "unsupported options: %s"},
    // -- flash-x (payload.bin extraction)
    {kHFlashX, "flash-x\tExtract PARTITION from an OTA payload.bin and flash it.\n\tNAME names the entry inside the payload; it defaults\n\tto PARTITION with any slot suffix removed."},
    {kExtracting, "Extracting '%s' from payload.bin"},
    {kPayloadOpenFailed, "cannot open payload '%s': %s"},
    {kPayloadInvalid, "'%s' is not a usable payload.bin: %s"},
    {kPayloadDelta, "'%s': payload is a delta OTA (incremental); flash-x needs a full payload"},
    {kPayloadNoPartition, "payload has no partition '%s' (available: %s)"},
    {kPayloadExtractFailed, "extracting from payload failed: %s"},
    {kJunkAfterFlashX, "flash-x takes at most three arguments: PARTITION PAYLOAD [NAME]"},
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
