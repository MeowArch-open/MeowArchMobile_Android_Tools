/*
 * fb_i18n_ja.cpp — Japanese (ja) message table.
 *
 * Same layout as fb_i18n_zh_cn.cpp. Commands stay English tokens (they are
 * what the tool parses); format specifiers match the English originals
 * exactly (StringPrintf validates them). Terminology follows common Japanese
 * Android-tooling usage: 書き込み (flash/write), パーティション, スロット,
 * イメージ, スパース.
 */
#include <iterator>

#include "fb_i18n.h"

namespace fbi18n {

static const MsgEntry kJaTable[] = {
    // -- help: structure --------------------------------------------------------
    {kHelpUsage, "使用法: fastboot [オプション...] コマンド..."},
    {kSecFlashing, "書き込み:"},
    {kSecBasics, "基本:"},
    {kSecLocking, "ロック/ロック解除:"},
    {kSecAdvanced, "高度:"},
    {kSecBootImage, "ブートイメージ:"},
    {kSecAndroidThings, "Android Things:"},
    {kSecOptions, "オプション:"},
    // -- help: commands ---------------------------------------------------------
    {kHUpdate, "update ZIP\tupdate.zip パッケージからすべてのパーティションを書き込む。"},
    {kHFlashall,
        "flashall\t$ANDROID_PRODUCT_OUT からすべてのパーティションを書き込む。\n"
        "\tA/B デバイスでは、書き込まれたスロットがアクティブになる。\n"
        "\tセカンダリイメージは非アクティブスロットに書き込める。"},
    {kHFlash,
        "flash PARTITION [FILENAME]\t指定パーティションを書き込む。ファイル名省略時は\n"
        "\t$ANDROID_PRODUCT_OUT のイメージを使用。"},
    {kHFlashVb,
        "flash vendor_boot:RAMDISK [FILENAME]\tvendor_boot ramdisk を書き込む。既存の\n"
        "\tvendor_boot イメージを取得し、新しい ramdisk で\n\t再パッケージする。"},
    {kHDtbIf,
        "--dtb DTB\tflash vendor_boot:RAMDISK と併用すると、提供された DTB で\n"
        "\tvendor_boot イメージを更新する。"},
    {kHDevices, "devices [-l]\tbootloader 状態のデバイスを列挙 (-l: パス付き)。"},
    {kHGetvar, "getvar NAME\t指定の bootloader 変数を表示。"},
    {kHReboot, "reboot [bootloader]\tデバイスを再起動。"},
    {kHFlashingLock, "flashing lock|unlock\tパーティション書き込みをロック/ロック解除"},
    {kHFlashingCrit,
        "flashing lock_critical|unlock_critical\t'critical' bootloader パーティションを\n\tロック/ロック解除。"},
    {kHFlashingAbility,
        "flashing get_unlock_ability\tロック解除が許可 (1) / 不許可 (0) かを確認。"},
    {kHErase, "erase PARTITION\tフラッシュパーティションを消去。"},
    {kHFormat, "format[:FS_TYPE[:SIZE]] PARTITION\tフラッシュパーティションをフォーマット。"},
    {kHSetActive, "set_active SLOT\tアクティブスロットを設定。"},
    {kHOem, "oem [COMMAND...]\tOEM 固有コマンドを実行。"},
    {kHGsi,
        "gsi wipe|disable|status\tGSI インストールを消去/無効化、状態を表示\n"
        "\t(fastbootd のみ)。"},
    {kHWipeSuper,
        "wipe-super [SUPER_EMPTY]\tsuper パーティションを消去。空のデフォルト\n"
        "\t動的パーティション集合にリセットする。"},
    {kHCreateLp,
        "create-logical-partition NAME SIZE\tsuper パーティション内に指定の名前と\n"
        "\tサイズの論理パーティションを作成。"},
    {kHDeleteLp, "delete-logical-partition NAME\t指定の名前の論理パーティションを削除。"},
    {kHResizeLp,
        "resize-logical-partition NAME SIZE\t指定の論理パーティションのサイズを変更。"},
    {kHSnapCancel,
        "snapshot-update cancel\tスナップショット更新対応デバイスで、進行中の\n"
        "\t更新をキャンセル。再書き込みまでデバイスが\n"
        "\t起動不能になる可能性がある。"},
    {kHSnapMerge,
        "snapshot-update merge\tスナップショット更新対応デバイスで、更新が\n"
        "\t\"merging\"（マージ）段階なら進行中の更新を\n\t完了させる。"},
    {kHFetch, "fetch PARTITION OUT_FILE\tデバイスからパーティションイメージを取得。"},
    {kHBoot, "boot KERNEL [RAMDISK [SECOND]]\tRAM からカーネルをダウンロードして起動。"},
    {kHFlashRaw,
        "flash:raw PARTITION KERNEL [RAMDISK [SECOND]]\tブートイメージを作成して書き込む。"},
    {kHStage, "stage IN_FILE\t次のコマンド用にファイルをステージへ送信。"},
    {kHGetStaged, "get_staged OUT_FILE\t直前コマンドがステージしたデータをファイルへ書く。"},
    // -- help: boot-image options ----------------------------------------------
    {kHDtb, "--dtb DTB\tブートイメージヘッダ v2 の DTB パスを指定。"},
    {kHCmdline, "--cmdline CMDLINE\tカーネルコマンドラインを上書き。"},
    {kHBase, "--base ADDRESS\tカーネルベースアドレスを設定 (既定: 0x10000000)。"},
    {kHKernelOffset, "--kernel-offset\tカーネルオフセットを設定 (既定: 0x00008000)。"},
    {kHRamdiskOffset, "--ramdisk-offset\tramdisk オフセットを設定 (既定: 0x01000000)。"},
    {kHTagsOffset, "--tags-offset\ttags オフセットを設定 (既定: 0x00000100)。"},
    {kHDtbOffset, "--dtb-offset\tdtb オフセットを設定 (既定: 0x01100000)。"},
    {kHPageSize, "--page-size BYTES\tフラッシュページサイズを設定 (既定: 2048)。"},
    {kHHeaderVersion, "--header-version VERSION\tブートイメージヘッダバージョンを設定。"},
    {kHOsVersion,
        "--os-version MAJOR[.MINOR[.PATCH]]\tブートイメージの OS バージョンを設定 (既定: 0.0.0)。"},
    {kHOsPatch, "--os-patch-level YYYY-MM-DD\tブートイメージの OS セキュリティパッチレベルを設定。"},
    // -- help: options ----------------------------------------------------------
    {kOWipe, "-w\tuserdata を消去。"},
    {kOSerial, "-s SERIAL\tUSB デバイスを指定。"},
    {kOSerialNet, "-s tcp|udp:HOST[:PORT]\tネットワークデバイスを指定。"},
    {kOSparse, "-S SIZE[K|M|G]\tSIZE 以下のスパースファイルに分割。"},
    {kOForce, "--force\t安全でない可能性のある書き込みを強制実行。"},
    {kOSlot,
        "--slot SLOT\tSLOT を使用。'all' は両スロット、'other' は\n"
        "\t非現在のスロット (既定: 現在のアクティブスロット)。"},
    {kOSetActive, "--set-active[=SLOT]\t再起動前にアクティブスロットを設定。"},
    {kOSkipSecondary, "--skip-secondary\tflashall/update でセカンダリスロットを書き込まない。"},
    {kOSkipReboot, "--skip-reboot\t書き込み後にデバイスを再起動しない。"},
    {kODisableVerity, "--disable-verity\tvbmeta 書き込み時に disable-verity を設定。"},
    {kODisableVerification, "--disable-verification\tvbmeta 書き込み時に disable-verification を設定。"},
    {kODisableSuperOpt,
        "--disable-super-optimization\tsuper パーティション書き込み時の最適化を無効化。"},
    {kOExcludeDynamic,
        "--exclude-dynamic-partitions\t動的パーティションの書き込みを除外。"},
    {kODisableFbInfo,
        "--disable-fastboot-info\t$OUT/fastboot-info.txt ではなくイメージ一覧から\n"
        "\tタスクを収集する。"},
    {kOFsOptions,
        "--fs-options=OPTION[,OPTION]\tファイルシステム機能を有効化。OPTION は casefold、"
        "projid、compress に対応"},
    {kOUnbuffered, "--unbuffered\t入出力をバッファリングしない。"},
    {kOVerbose, "--verbose, -v\t詳細出力。"},
    {kOVersion, "--version\tバージョンを表示。"},
    {kOHelp, "--help, -h\tこのメッセージを表示。"},
    {kOLang, "--lang=CODE\t表示言語を設定 (--list-langs 参照)。"},
    {kOListLangs, "--list-langs\t利用可能な表示言語を列挙して終了。"},
    {kONoColor, "--no-color\tカラー出力を無効化 (NO_COLOR=1 でも可)。"},
    {kOColor, "--color\ttty でなくてもカラー出力を強制。"},
    {kONoProgress, "--no-progress\t転送プログレスバーを無効化 (NO_PROGRESS=1 でも可)。"},
    // -- runtime: status / epilog ------------------------------------------------
    {kOkay, "OKAY"},
    {kFailed, "FAILED"},
    {kFinished, "完了。総時間: %.3fs\n"},
    {kWaitingDevice, "< %s を待機中 >"},
    {kWaitingFor, "< %s を待機中 >"},
    {kWaitingAny, "< 任意のデバイスを待機中 >"},
    // -- runtime: errors ----------------------------------------------------------
    {kErrPrefix, "fastboot: エラー: "},
    {kUsagePrefix, "fastboot: 使用法: "},
    {kVerbosePrefix, "fastboot: 詳細: "},
    {kUnknownCommand, "不明なコマンド %s"},
    {kUnknownOption, "不明なオプション %s"},
    {kUnknownPartition, "不明なパーティション '%s'"},
    {kDidYouMean, "'%s' のことですか？"},
    {kDidYouMeanList, "これらのいずれかのことですか: %s？"},
    {kRunHelp, "使用法は 'fastboot-m --help' を実行してください。"},
    {kNoCommand, "コマンドがありません"},
    {kExpectedArgument, "引数が必要です"},
    {kEmptyOemCommand, "oem コマンドが空です"},
    {kJunkAfterReboot, "reboot コマンドの後に余分な指定があります"},
    {kMissingFlashingCommand, "'flashing' コマンドがありません"},
    {kUnknownFlashingCommand, "不明な 'flashing' コマンド %s"},
    {kInvalidGsiCommand, "無効な gsi コマンド"},
    {kExpectedSnapshotUpdate, "次の形式が必要です: snapshot-update [cancel|merge]"},
    {kUnsupportedFsOption, "未対応のオプション: %s"},
    // -- flash-x (payload.bin extraction)
    {kHFlashX, "flash-x\tOTA の payload.bin からパーティションを抽出して書き込みます。\n\tイメージ名は省略時、パーティション名（スロット接尾辞を\n\t除く）を使います。"},
    {kExtracting, "payload.bin から '%s' を展開中"},
    {kPayloadOpenFailed, "payload '%s' を開けません: %s"},
    {kPayloadInvalid, "'%s' は使用可能な payload.bin ではありません: %s"},
    {kPayloadDelta, "'%s': 差分 OTA の payload です。flash-x には完全な payload が必要です"},
    {kPayloadNoPartition, "payload にパーティション '%s' がありません（利用可能: %s）"},
    {kPayloadExtractFailed, "payload からの抽出に失敗しました: %s"},
    {kJunkAfterFlashX, "flash-x の引数は最大3つです: パーティション PAYLOAD [イメージ名]"},
    // -- driver prologs ------------------------------------------------------------
    {kSending, "'%s' を送信中 (%zu KB)"},
    {kSendingSparse, "スパース '%s' を送信中 %zu/%zu (%u KB)"},
    {kWriting, "'%s' を書き込み中"},
    {kErasing, "'%s' を消去中"},
    {kUploading, "'%s' をアップロード中"},
    {kFetching, "%s を取得中 (offset=%" PRIx64 ", size=%" PRIx64 ")"},
    {kBooting, "起動中"},
    {kResumingBoot, "起動を再開中"},
    {kRebooting, "再起動中"},
    {kRebootingInto, "%s へ再起動中"},
    {kCreating, "'%s' を作成中"},
    {kDeleting, "'%s' を削除中"},
    {kResizing, "'%s' のサイズを変更中"},
    {kSettingSlot, "現在のスロットを '%s' に設定中"},
    {kSnapshot, "スナップショット %s"},
    // -- progress bar ----------------------------------------------------------------
    {kEta, "残り"},
    {kSpeedUnit, "/s"},
    // -- languages ----------------------------------------------------------------------
    {kLangsHeader, "利用可能な言語 (--lang=CODE):"},
    {kLangCurrent, " (現在の言語)"},
};

void RegisterLangJa() { RegisterLang("ja", "日本語", kJaTable, std::size(kJaTable)); }

}  // namespace fbi18n
