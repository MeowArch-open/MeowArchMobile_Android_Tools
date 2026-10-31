/*
 * fb_i18n_zh_cn.cpp — Simplified Chinese (zh_CN) message table.
 *
 * Help entries: fully translated (see scripts/gen-help-entries.py's ID map for
 * which row renders which string). Runtime strings keep upstream's English
 * keywords where they are protocol-ish ("OKAY"/"FAILED" come from the device
 * protocol and stay as-is, with a translated gloss where useful).
 *
 * Translations aim for the terminology used by Chinese Android tooling
 * communities: 分区 (partition), 刷机 (flash/flashing), 槽位 (slot), 镜像
 * (image), 稀疏 (sparse).
 */
#include <iterator>

#include "fb_i18n.h"

namespace fbi18n {

static const MsgEntry kZhCnTable[] = {
    // -- help: structure --------------------------------------------------------
    {kHelpUsage, "用法: fastboot [选项...] 命令..."},
    {kSecFlashing, "刷机:"},
    {kSecBasics, "基础:"},
    {kSecLocking, "锁定/解锁:"},
    {kSecAdvanced, "高级:"},
    {kSecBootImage, "启动镜像:"},
    {kSecAndroidThings, "Android Things:"},
    {kSecOptions, "选项:"},
    // -- help: commands ---------------------------------------------------------
    {kHUpdate, "update ZIP\t从 update.zip 包刷写所有分区。"},
    {kHFlashall,
        "flashall\t从 $ANDROID_PRODUCT_OUT 刷写所有分区。\n"
        "\t在 A/B 设备上，被刷写的槽位将设为活动槽位。\n"
        "\t次要镜像可刷写到非活动槽位。"},
    {kHFlash,
        "flash PARTITION [FILENAME]\t刷写指定分区；未给出文件名时使用\n"
        "\t$ANDROID_PRODUCT_OUT 中的镜像。"},
    {kHFlashVb,
        "flash vendor_boot:RAMDISK [FILENAME]\t刷写 vendor_boot ramdisk：拉取现有的\n"
        "\tvendor_boot 镜像并用新的 ramdisk 重新打包。"},
    {kHDtbIf,
        "--dtb DTB\t与 flash vendor_boot:RAMDISK 同时使用时，\n"
        "\t用提供的 DTB 更新 vendor_boot 镜像。"},
    {kHDevices, "devices [-l]\t列出处于 bootloader 的设备 (-l: 含设备路径)。"},
    {kHGetvar, "getvar NAME\t显示指定的 bootloader 变量。"},
    {kHReboot, "reboot [bootloader]\t重启设备。"},
    {kHFlashingLock, "flashing lock|unlock\t锁定/解锁分区刷写权限"},
    {kHFlashingCrit,
        "flashing lock_critical|unlock_critical\t锁定/解锁 'critical' bootloader 分区。"},
    {kHFlashingAbility,
        "flashing get_unlock_ability\t检查是否允许解锁 (1) 或不允许 (0)。"},
    {kHErase, "erase PARTITION\t擦除一个闪存分区。"},
    {kHFormat, "format[:FS_TYPE[:SIZE]] PARTITION\t格式化一个闪存分区。"},
    {kHSetActive, "set_active SLOT\t设置活动槽位。"},
    {kHOem, "oem [COMMAND...]\t执行 OEM 专有命令。"},
    {kHGsi,
        "gsi wipe|disable|status\t擦除、禁用 GSI 安装或显示其状态\n"
        "\t(仅限 fastbootd)。"},
    {kHWipeSuper,
        "wipe-super [SUPER_EMPTY]\t擦除 super 分区，将其重置为\n"
        "\t仅包含一组空的默认动态分区。"},
    {kHCreateLp,
        "create-logical-partition NAME SIZE\t在 super 分区中创建具有给定名称\n"
        "\t和大小的逻辑分区。"},
    {kHDeleteLp, "delete-logical-partition NAME\t删除具有给定名称的逻辑分区。"},
    {kHResizeLp,
        "resize-logical-partition NAME SIZE\t更改指定逻辑分区的大小。"},
    {kHSnapCancel,
        "snapshot-update cancel\t在支持快照更新的设备上，取消\n"
        "\t正在进行的更新。这可能导致设备\n"
        "\t在重新刷写之前无法启动。"},
    {kHSnapMerge,
        "snapshot-update merge\t在支持快照更新的设备上，若更新处于\n"
        "\t\"merging\"（合并）阶段，则完成\n"
        "\t正在进行的更新。"},
    {kHFetch, "fetch PARTITION OUT_FILE\t从设备拉取分区镜像。"},
    {kHBoot, "boot KERNEL [RAMDISK [SECOND]]\t从内存下载并启动内核。"},
    {kHFlashRaw,
        "flash:raw PARTITION KERNEL [RAMDISK [SECOND]]\t创建启动镜像并刷写。"},
    {kHStage, "stage IN_FILE\t发送指定文件到暂存区，供下一条命令使用。"},
    {kHGetStaged, "get_staged OUT_FILE\t将上一条命令暂存的数据写入文件。"},
    // -- help: boot-image options ----------------------------------------------
    {kHDtb, "--dtb DTB\t指定 boot 镜像头版本 2 的 DTB 路径。"},
    {kHCmdline, "--cmdline CMDLINE\t覆盖内核命令行。"},
    {kHBase, "--base ADDRESS\t设置内核基地址 (默认: 0x10000000)。"},
    {kHKernelOffset, "--kernel-offset\t设置内核偏移 (默认: 0x00008000)。"},
    {kHRamdiskOffset, "--ramdisk-offset\t设置 ramdisk 偏移 (默认: 0x01000000)。"},
    {kHTagsOffset, "--tags-offset\t设置 tags 偏移 (默认: 0x00000100)。"},
    {kHDtbOffset, "--dtb-offset\t设置 dtb 偏移 (默认: 0x01100000)。"},
    {kHPageSize, "--page-size BYTES\t设置闪存页大小 (默认: 2048)。"},
    {kHHeaderVersion, "--header-version VERSION\t设置 boot 镜像头版本。"},
    {kHOsVersion,
        "--os-version MAJOR[.MINOR[.PATCH]]\t设置 boot 镜像的 OS 版本 (默认: 0.0.0)。"},
    {kHOsPatch, "--os-patch-level YYYY-MM-DD\t设置 boot 镜像的 OS 安全补丁级别。"},
    // -- help: options ----------------------------------------------------------
    {kOWipe, "-w\t擦除 userdata。"},
    {kOSerial, "-s SERIAL\t指定一个 USB 设备。"},
    {kOSerialNet, "-s tcp|udp:HOST[:PORT]\t指定一个网络设备。"},
    {kOSparse, "-S SIZE[K|M|G]\t拆分为不超过 SIZE 的稀疏文件。"},
    {kOForce, "--force\t强制执行可能不安全的刷写操作。"},
    {kOSlot,
        "--slot SLOT\t使用 SLOT；'all' 表示两个槽位，'other' 表示\n"
        "\t非当前槽位 (默认: 当前活动槽位)。"},
    {kOSetActive, "--set-active[=SLOT]\t在重启前设置活动槽位。"},
    {kOSkipSecondary, "--skip-secondary\t在 flashall/update 中不刷写次要槽位。"},
    {kOSkipReboot, "--skip-reboot\t刷写后不重启设备。"},
    {kODisableVerity, "--disable-verity\t刷写 vbmeta 时设置 disable-verity。"},
    {kODisableVerification, "--disable-verification\t刷写 vbmeta 时设置 disable-verification。"},
    {kODisableSuperOpt,
        "--disable-super-optimization\t禁用刷写 super 分区时的优化。"},
    {kOExcludeDynamic,
        "--exclude-dynamic-partitions\t排除动态分区的刷写。"},
    {kODisableFbInfo,
        "--disable-fastboot-info\t从镜像列表收集任务，而不是\n"
        "\t$OUT/fastboot-info.txt。"},
    {kOFsOptions,
        "--fs-options=OPTION[,OPTION]\t启用文件系统特性。OPTION 支持 casefold、"
        "projid、compress"},
    {kOUnbuffered, "--unbuffered\t不缓冲输入或输出。"},
    {kOVerbose, "--verbose, -v\t详细输出。"},
    {kOVersion, "--version\t显示版本。"},
    {kOHelp, "--help, -h\t显示此帮助信息。"},
    {kOLang, "--lang=CODE\t设置界面语言 (参见 --list-langs)。"},
    {kOListLangs, "--list-langs\t列出可用的界面语言并退出。"},
    {kONoColor, "--no-color\t禁用彩色输出 (也可用 NO_COLOR=1)。"},
    {kOColor, "--color\t即使不是 tty 也强制彩色输出。"},
    {kONoProgress, "--no-progress\t禁用传输进度条 (也可用 NO_PROGRESS=1)。"},
    // -- runtime: status / epilog ------------------------------------------------
    {kOkay, "OKAY"},
    {kFailed, "FAILED"},
    {kFinished, "完成。总耗时: %.3fs\n"},
    {kWaitingDevice, "< 正在等待 %s >"},
    {kWaitingFor, "< 正在等待 %s >"},
    {kWaitingAny, "< 正在等待任意设备 >"},
    // -- runtime: errors ----------------------------------------------------------
    {kErrPrefix, "fastboot: 错误: "},
    {kUsagePrefix, "fastboot: 用法: "},
    {kVerbosePrefix, "fastboot: 详细: "},
    {kUnknownCommand, "未知命令 %s"},
    {kUnknownOption, "未知选项 %s"},
    {kUnknownPartition, "未知分区 '%s'"},
    {kDidYouMean, "您是否想输入 '%s'？"},
    {kDidYouMeanList, "您是否想输入其中之一: %s？"},
    {kRunHelp, "运行 'fastboot --help' 查看用法。"},
    {kNoCommand, "没有命令"},
    {kExpectedArgument, "缺少参数"},
    {kEmptyOemCommand, "oem 命令为空"},
    {kJunkAfterReboot, "reboot 命令后有多余内容"},
    {kMissingFlashingCommand, "缺少 'flashing' 命令"},
    {kUnknownFlashingCommand, "未知的 'flashing' 命令 %s"},
    {kInvalidGsiCommand, "无效的 gsi 命令"},
    {kExpectedSnapshotUpdate, "应为: snapshot-update [cancel|merge]"},
    {kUnsupportedFsOption, "不支持的选项: %s"},
    // -- driver prologs ------------------------------------------------------------
    {kSending, "正在发送 '%s' (%zu KB)"},
    {kSendingSparse, "正在发送稀疏镜像 '%s' %zu/%zu (%u KB)"},
    {kWriting, "正在写入 '%s'"},
    {kErasing, "正在擦除 '%s'"},
    {kUploading, "正在上传 '%s'"},
    {kFetching, "正在拉取 %s (offset=%" PRIx64 ", size=%" PRIx64 ")"},
    {kBooting, "正在启动"},
    {kResumingBoot, "正在恢复启动"},
    {kRebooting, "正在重启"},
    {kRebootingInto, "正在重启进入 %s"},
    {kCreating, "正在创建 '%s'"},
    {kDeleting, "正在删除 '%s'"},
    {kResizing, "正在调整 '%s' 大小"},
    {kSettingSlot, "正在将当前槽位设为 '%s'"},
    {kSnapshot, "快照 %s"},
    // -- progress bar ----------------------------------------------------------------
    {kEta, "剩余"},
    {kSpeedUnit, "/s"},
    // -- languages ----------------------------------------------------------------------
    {kLangsHeader, "可用语言 (--lang=CODE):"},
    {kLangCurrent, " (当前)"},
};

void RegisterLangZhCn() { RegisterLang("zh_CN", "简体中文", kZhCnTable, std::size(kZhCnTable)); }

}  // namespace fbi18n
