/*
 * fb_i18n_zh_tw.cpp — Traditional Chinese (zh_TW) message table.
 *
 * Derived from the Simplified Chinese table with character-level conversion
 * (opencc s2t) plus terminology normalization: 啟動 (not 啓動), 裝置, 映像,
 * 槽位. Commands themselves stay English tokens (they are what the tool
 * parses); format specifiers match the English originals exactly
 * (StringPrintf validates them).
 */
#include <iterator>

#include "fb_i18n.h"

namespace fbi18n {

static const MsgEntry kZhTwTable[] = {
    // -- help: structure --------------------------------------------------------
    {kHelpUsage, "用法: fastboot [選項...] 命令..."},
    {kSecFlashing, "刷機:"},
    {kSecBasics, "基礎:"},
    {kSecLocking, "鎖定/解鎖:"},
    {kSecAdvanced, "進階:"},
    {kSecBootImage, "啟動映像:"},
    {kSecAndroidThings, "Android Things:"},
    {kSecOptions, "選項:"},
    // -- help: commands ---------------------------------------------------------
    {kHUpdate, "update ZIP\t從 update.zip 封包刷寫所有分割區。"},
    {kHFlashall,
        "flashall\t從 $ANDROID_PRODUCT_OUT 刷寫所有分割區。\n"
        "\t在 A/B 裝置上，被刷寫的槽位將設為使用中槽位。\n"
        "\t次要映像可刷寫到非使用中槽位。"},
    {kHFlash,
        "flash PARTITION [FILENAME]\t刷寫指定分割區；未給定檔案名稱時使用\n"
        "\t$ANDROID_PRODUCT_OUT 中的映像。"},
    {kHFlashVb,
        "flash vendor_boot:RAMDISK [FILENAME]\t刷寫 vendor_boot ramdisk：取得現有的\n"
        "\tvendor_boot 映像並以新的 ramdisk 重新封裝。"},
    {kHDtbIf,
        "--dtb DTB\t與 flash vendor_boot:RAMDISK 同時使用時，\n"
        "\t以提供的 DTB 更新 vendor_boot 映像。"},
    {kHDevices, "devices [-l]\t列出處於 bootloader 的裝置 (-l: 含裝置路徑)。"},
    {kHGetvar, "getvar NAME\t顯示指定的 bootloader 變數。"},
    {kHReboot, "reboot [bootloader]\t重新啟動裝置。"},
    {kHFlashingLock, "flashing lock|unlock\t鎖定/解鎖分割區刷寫權限"},
    {kHFlashingCrit,
        "flashing lock_critical|unlock_critical\t鎖定/解鎖 'critical' bootloader 分割區。"},
    {kHFlashingAbility,
        "flashing get_unlock_ability\t檢查是否允許解鎖 (1) 或不允許 (0)。"},
    {kHErase, "erase PARTITION\t清除一個快閃分割區。"},
    {kHFormat, "format[:FS_TYPE[:SIZE]] PARTITION\t格式化一個快閃分割區。"},
    {kHSetActive, "set_active SLOT\t設定使用中槽位。"},
    {kHOem, "oem [COMMAND...]\t執行 OEM 專有命令。"},
    {kHGsi,
        "gsi wipe|disable|status\t清除、停用 GSI 安裝或顯示其狀態\n"
        "\t(僅限 fastbootd)。"},
    {kHWipeSuper,
        "wipe-super [SUPER_EMPTY]\t清除 super 分割區，將其重設為\n"
        "\t僅包含一組空的預設動態分割區。"},
    {kHCreateLp,
        "create-logical-partition NAME SIZE\t在 super 分割區中建立具有指定名稱\n"
        "\t和大小的邏輯分割區。"},
    {kHDeleteLp, "delete-logical-partition NAME\t刪除具有指定名稱的邏輯分割區。"},
    {kHResizeLp,
        "resize-logical-partition NAME SIZE\t變更指定邏輯分割區的大小。"},
    {kHSnapCancel,
        "snapshot-update cancel\t在支援快照更新的裝置上，取消\n"
        "\t正在進行的更新。這可能導致裝置\n"
        "\t在重新刷寫之前無法啟動。"},
    {kHSnapMerge,
        "snapshot-update merge\t在支援快照更新的裝置上，若更新處於\n"
        "\t\"merging\"（合併）階段，則完成\n"
        "\t正在進行的更新。"},
    {kHFetch, "fetch PARTITION OUT_FILE\t從裝置取得分割區映像。"},
    {kHBoot, "boot KERNEL [RAMDISK [SECOND]]\t從記憶體下載並啟動核心。"},
    {kHFlashRaw,
        "flash:raw PARTITION KERNEL [RAMDISK [SECOND]]\t建立啟動映像並刷寫。"},
    {kHStage, "stage IN_FILE\t傳送指定檔案到暫存區，供下一條命令使用。"},
    {kHGetStaged, "get_staged OUT_FILE\t將上一條命令暫存的資料寫入檔案。"},
    // -- help: boot-image options ----------------------------------------------
    {kHDtb, "--dtb DTB\t指定 boot 映像標頭版本 2 的 DTB 路徑。"},
    {kHCmdline, "--cmdline CMDLINE\t覆寫核心命令列。"},
    {kHBase, "--base ADDRESS\t設定核心基底位址 (預設: 0x10000000)。"},
    {kHKernelOffset, "--kernel-offset\t設定核心位移 (預設: 0x00008000)。"},
    {kHRamdiskOffset, "--ramdisk-offset\t設定 ramdisk 位移 (預設: 0x01000000)。"},
    {kHTagsOffset, "--tags-offset\t設定 tags 位移 (預設: 0x00000100)。"},
    {kHDtbOffset, "--dtb-offset\t設定 dtb 位移 (預設: 0x01100000)。"},
    {kHPageSize, "--page-size BYTES\t設定快閃頁大小 (預設: 2048)。"},
    {kHHeaderVersion, "--header-version VERSION\t設定 boot 映像標頭版本。"},
    {kHOsVersion,
        "--os-version MAJOR[.MINOR[.PATCH]]\t設定 boot 映像的 OS 版本 (預設: 0.0.0)。"},
    {kHOsPatch, "--os-patch-level YYYY-MM-DD\t設定 boot 映像的 OS 安全性修補程式等級。"},
    // -- help: options ----------------------------------------------------------
    {kOWipe, "-w\t清除 userdata。"},
    {kOSerial, "-s SERIAL\t指定一個 USB 裝置。"},
    {kOSerialNet, "-s tcp|udp:HOST[:PORT]\t指定一個網路裝置。"},
    {kOSparse, "-S SIZE[K|M|G]\t拆分為不超過 SIZE 的稀疏檔案。"},
    {kOForce, "--force\t強制執行可能不安全的刷寫操作。"},
    {kOSlot,
        "--slot SLOT\t使用 SLOT；'all' 表示兩個槽位，'other' 表示\n"
        "\t非目前槽位 (預設: 目前使用中槽位)。"},
    {kOSetActive, "--set-active[=SLOT]\t在重新啟動前設定使用中槽位。"},
    {kOSkipSecondary, "--skip-secondary\t在 flashall/update 中不刷寫次要槽位。"},
    {kOSkipReboot, "--skip-reboot\t刷寫後不重新啟動裝置。"},
    {kODisableVerity, "--disable-verity\t刷寫 vbmeta 時設定 disable-verity。"},
    {kODisableVerification, "--disable-verification\t刷寫 vbmeta 時設定 disable-verification。"},
    {kODisableSuperOpt,
        "--disable-super-optimization\t停用刷寫 super 分割區時的最佳化。"},
    {kOExcludeDynamic,
        "--exclude-dynamic-partitions\t排除動態分割區的刷寫。"},
    {kODisableFbInfo,
        "--disable-fastboot-info\t從映像清單收集任務，而不是\n"
        "\t$OUT/fastboot-info.txt。"},
    {kOFsOptions,
        "--fs-options=OPTION[,OPTION]\t啟用檔案系統特性。OPTION 支援 casefold、"
        "projid、compress"},
    {kOUnbuffered, "--unbuffered\t不緩衝輸入或輸出。"},
    {kOVerbose, "--verbose, -v\t詳細輸出。"},
    {kOVersion, "--version\t顯示版本。"},
    {kOHelp, "--help, -h\t顯示此說明訊息。"},
    {kOLang, "--lang=CODE\t設定介面語言 (參見 --list-langs)。"},
    {kOListLangs, "--list-langs\t列出可用的介面語言並結束。"},
    {kONoColor, "--no-color\t停用彩色輸出 (亦可使用 NO_COLOR=1)。"},
    {kOColor, "--color\t即使不是 tty 也強制彩色輸出。"},
    {kONoProgress, "--no-progress\t停用傳輸進度列 (亦可使用 NO_PROGRESS=1)。"},
    // -- runtime: status / epilog ------------------------------------------------
    {kOkay, "OKAY"},
    {kFailed, "FAILED"},
    {kFinished, "完成。總耗時: %.3fs\n"},
    {kWaitingDevice, "< 正在等待 %s >"},
    {kWaitingFor, "< 正在等待 %s >"},
    {kWaitingAny, "< 正在等待任意裝置 >"},
    // -- runtime: errors ----------------------------------------------------------
    {kErrPrefix, "fastboot: 錯誤: "},
    {kUsagePrefix, "fastboot: 用法: "},
    {kVerbosePrefix, "fastboot: 詳細: "},
    {kUnknownCommand, "未知命令 %s"},
    {kUnknownOption, "未知選項 %s"},
    {kUnknownPartition, "未知分割區 '%s'"},
    {kDidYouMean, "您是否想輸入 '%s'？"},
    {kDidYouMeanList, "您是否想輸入其中之一: %s？"},
    {kRunHelp, "執行 'fastboot --help' 查看用法。"},
    {kNoCommand, "沒有命令"},
    {kExpectedArgument, "缺少引數"},
    {kEmptyOemCommand, "oem 命令為空"},
    {kJunkAfterReboot, "reboot 命令後有多餘內容"},
    {kMissingFlashingCommand, "缺少 'flashing' 命令"},
    {kUnknownFlashingCommand, "未知的 'flashing' 命令 %s"},
    {kInvalidGsiCommand, "無效的 gsi 命令"},
    {kExpectedSnapshotUpdate, "應為: snapshot-update [cancel|merge]"},
    {kUnsupportedFsOption, "不支援的選項: %s"},
    // -- driver prologs ------------------------------------------------------------
    {kSending, "正在傳送 '%s' (%zu KB)"},
    {kSendingSparse, "正在傳送稀疏映像 '%s' %zu/%zu (%u KB)"},
    {kWriting, "正在寫入 '%s'"},
    {kErasing, "正在清除 '%s'"},
    {kUploading, "正在上傳 '%s'"},
    {kFetching, "正在取得 %s (offset=%" PRIx64 ", size=%" PRIx64 ")"},
    {kBooting, "正在啟動"},
    {kResumingBoot, "正在恢復啟動"},
    {kRebooting, "正在重新啟動"},
    {kRebootingInto, "正在重新啟動進入 %s"},
    {kCreating, "正在建立 '%s'"},
    {kDeleting, "正在刪除 '%s'"},
    {kResizing, "正在調整 '%s' 大小"},
    {kSettingSlot, "正在將目前槽位設為 '%s'"},
    {kSnapshot, "快照 %s"},
    // -- progress bar ----------------------------------------------------------------
    {kEta, "剩餘"},
    {kSpeedUnit, "/s"},
    // -- languages ----------------------------------------------------------------------
    {kLangsHeader, "可用語言 (--lang=CODE):"},
    {kLangCurrent, " (目前)"},
};

void RegisterLangZhTw() { RegisterLang("zh_TW", "繁體中文", kZhTwTable, std::size(kZhTwTable)); }

}  // namespace fbi18n
