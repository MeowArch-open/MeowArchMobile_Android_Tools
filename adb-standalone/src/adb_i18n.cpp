/*
 * adb_i18n.cpp — message catalog runtime. See adb_i18n.h.
 *
 * Language tables live in adb_i18n_<lang>.cpp; each exposes an explicit
 * registrar called from Init(), because these TUs sit in a static library
 * whose unreferenced objects the linker would otherwise drop.
 */
#include "adb_i18n.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

namespace adbi18n {
namespace {

struct Lang {
    LangInfo info;
    const char* dense[kNumMessages];
};

std::vector<Lang>& Registry() {
    static std::vector<Lang> r;
    return r;
}

size_t g_active = 0;
std::string g_locale = "en";

std::string Normalize(std::string s) {
    if (s == "C" || s == "POSIX") return "en";
    size_t at = s.find_first_of(".@");
    if (at != std::string::npos) s = s.substr(0, at);
    return s;
}

size_t Lookup(const std::string& code) {
    auto& reg = Registry();
    for (size_t i = 0; i < reg.size(); i++) {
        if (code == reg[i].info.code) return i;
    }
    size_t us = code.find('_');
    if (us != std::string::npos) {
        std::string base = code.substr(0, us);
        for (size_t i = 0; i < reg.size(); i++) {
            if (base == reg[i].info.code) return i;
        }
    } else {
        for (size_t i = 0; i < reg.size(); i++) {
            std::string entry(reg[i].info.code);
            size_t eus = entry.find('_');
            if (eus != std::string::npos && entry.substr(0, eus) == code) return i;
        }
    }
    return 0;
}

}  // namespace

void RegisterLang(const char* code, const char* native, const MsgEntry* table, size_t count) {
    auto& reg = Registry();
    Lang lang{};
    lang.info = {code, native};
    for (size_t i = 0; i < count; i++) {
        if (table[i].id > 0 && table[i].id < kNumMessages) {
            lang.dense[table[i].id] = table[i].text;
        }
    }
    if (!strcmp(code, "en")) {
        reg.insert(reg.begin(), std::move(lang));
        return;
    }
    reg.push_back(std::move(lang));
}

void RegisterLangEn();
void RegisterLangZhCn();
void RegisterLangZhTw();
void RegisterLangJa();

static void RegisterAllOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    RegisterLangEn();
    RegisterLangZhCn();
    RegisterLangZhTw();
    RegisterLangJa();
}

void Init(const std::string& lang_override) {
    RegisterAllOnce();
    if (!lang_override.empty()) {
        g_locale = Normalize(lang_override);
        g_active = Lookup(g_locale);
        g_locale = Registry()[g_active].info.code;
        return;
    }
    static const char* vars[] = {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"};
    for (const char* v : vars) {
        const char* val = getenv(v);
        if (!val || !val[0]) continue;
        std::string first(val);
        size_t colon = first.find(':');
        if (colon != std::string::npos) first = first.substr(0, colon);
        g_locale = Normalize(first);
        g_active = Lookup(g_locale);
        g_locale = Registry()[g_active].info.code;
        return;
    }
    g_active = 0;
    g_locale = "en";
}

const char* T(Msg id) {
    if (id <= 0 || id >= kNumMessages) return "";
    auto& reg = Registry();
    if (reg.empty()) return "";
    if (g_active < reg.size()) {
        const char* t = reg[g_active].dense[id];
        if (t) return t;
    }
    // English fallback, so a partial table degrades instead of blanking.
    const char* en = reg[0].dense[id];
    return en ? en : "";
}

const std::string& Locale() { return g_locale; }

namespace {

// The error strings worth translating, in order of how often a user actually
// hits them. Specifiers must match the English string exactly and in the same
// order — vfprintf feeds the same arguments to whichever text we print.
struct ErrRow {
    const char* en;
    const char* zh_cn;
    const char* zh_tw;
    const char* ja;
};

const ErrRow kErrors[] = {
    {"no devices/emulators found", "未找到设备/模拟器", "找不到裝置/模擬器",
     "デバイス/エミュレータが見つかりません"},
    {"more than one device/emulator", "存在多个设备/模拟器", "存在多個裝置/模擬器",
     "複数のデバイス/エミュレータがあります"},
    {"device '%s' not found", "未找到设备 '%s'", "找不到裝置 '%s'", "デバイス '%s' が見つかりません"},
    {"device offline", "设备已离线", "裝置已離線", "デバイスがオフラインです"},
    {"closed", "连接已关闭", "連線已關閉", "接続が閉じられました"},
    {"unknown command %s", "未知命令 %s", "未知命令 %s", "不明なコマンド %s"},
    {"usage: adb %s", "用法: adb %s", "用法: adb %s", "使い方: adb %s"},
    {"failed to start daemon", "无法启动守护进程", "無法啟動守護程序", "デーモンの起動に失敗しました"},
    {"cannot connect to daemon", "无法连接到守护进程", "無法連線到守護程序", "デーモンに接続できません"},
    {"ADB server version (%d) doesn't match this client (%d); killing...",
     "ADB 服务器版本 (%d) 与此客户端 (%d) 不匹配；正在结束该服务器...",
     "ADB 伺服器版本 (%d) 與此用戶端 (%d) 不符；正在結束該伺服器...",
     "ADB サーバーのバージョン (%d) がこのクライアント (%d) と一致しません。終了します..."},
    {"device unauthorized.\nThis adb server's $ADB_VENDOR_KEYS is not set\n"
     "Try 'adb kill-server' if that seems wrong.\n"
     "Otherwise check for a confirmation dialog on your device.",
     "设备未授权。\n此 adb 服务器的 $ADB_VENDOR_KEYS 未设置\n"
     "如果看起来不对，试试 'adb kill-server'。\n"
     "否则请查看设备上是否有授权确认对话框。",
     "裝置未授權。\n此 adb 伺服器的 $ADB_VENDOR_KEYS 未設定\n"
     "如果看起來不對，試試 'adb kill-server'。\n"
     "否則請檢查裝置上是否有授權確認對話框。",
     "デバイスが未認証です。\nこの adb サーバーの $ADB_VENDOR_KEYS が設定されていません\n"
     "心当たりがなければ 'adb kill-server' を試してください。\n"
     "それ以外はデバイス上の認証ダイアログを確認してください。"},
    {"install requires an apk argument", "install 需要一个 apk 参数", "install 需要一個 apk 參數",
     "install には apk 引数が必要です"},
    {"need APK file on command line", "命令行上需要 APK 文件", "命令列上需要 APK 檔案",
     "コマンドラインに APK ファイルが必要です"},
    {"Attempting to use streaming install on unsupported device",
     "尝试在不支持的设备上使用流式安装", "嘗試在不支援的裝置上使用串流安裝",
     "非対応デバイスでストリーミングインストールを使おうとしています"},
    {"Device doesn't support incremental installations", "设备不支持增量安装",
     "裝置不支援增量安裝", "デバイスが増分インストールに対応していません"},
    {"error: no devices/emulators found", "错误: 未找到设备/模拟器", "錯誤: 找不到裝置/模擬器",
     "エラー: デバイス/エミュレータが見つかりません"},
    {"error: more than one device/emulator", "错误: 存在多个设备/模拟器", "錯誤: 存在多個裝置/模擬器",
     "エラー: 複数のデバイス/エミュレータがあります"},
    {"error: device '%s' not found", "错误: 未找到设备 '%s'", "錯誤: 找不到裝置 '%s'",
     "エラー: デバイス '%s' が見つかりません"},
    {"error: device offline", "错误: 设备已离线", "錯誤: 裝置已離線", "エラー: デバイスがオフラインです"},
    {"error: closed", "错误: 连接已关闭", "錯誤: 連線已關閉", "エラー: 接続が閉じられました"},
    {"Performing Streamed Install", "正在执行流式安装", "正在執行串流安裝", "ストリーミングインストールを実行中"},
    {"Performing Push Install", "正在执行推送安装", "正在執行推送安裝", "プッシュインストールを実行中"},
    {"Performing Incremental Install", "正在执行增量安装", "正在執行增量安裝", "増分インストールを実行中"},
};

}  // namespace

namespace {

// One printf conversion specifier at |pos| in |pat| ("%s", "%d", "%u"...).
// Returns false when pat[pos] is not the start of a complete specifier.
bool ReadSpec(const char* pat, size_t pos, size_t* width) {
    if (pat[pos] != '%' || pat[pos + 1] == '\0') return false;
    size_t i = pos + 1;
    while (pat[i] && strchr("-+ #0", pat[i])) i++;
    while (pat[i] && isdigit((unsigned char)pat[i])) i++;
    if (pat[i] == '.') {
        i++;
        while (pat[i] && isdigit((unsigned char)pat[i])) i++;
    }
    while (pat[i] && strchr("hlLzjt", pat[i])) i++;
    if (!pat[i]) return false;
    *width = i + 1 - pos;
    return true;
}

// Matches |text| against the English |pat|, treating each specifier as a
// wildcard, and appends what each one matched to |out|.
//
// This is deliberately simple rather than a regex engine: adb's error strings
// have one or two specifiers surrounded by fixed prose, so scanning literals in
// order is both enough and easier to audit. Returns false when pat has no
// specifier or does not fit text.
bool MatchSpecs(const char* pat, const std::string& text, std::vector<std::string>* out) {
    std::vector<std::pair<size_t, size_t>> specs;  // {pat offset, specifier width}
    for (size_t i = 0; pat[i];) {
        size_t w = 0;
        if (ReadSpec(pat, i, &w)) {
            specs.push_back({i, w});
            i += w;
        } else {
            i++;
        }
    }
    if (specs.empty()) return false;

    // Literal text before the first specifier must match at the start.
    size_t prefix_len = specs[0].first;
    if (text.size() < prefix_len || text.compare(0, prefix_len, pat, prefix_len) != 0) {
        return false;
    }

    size_t ti = prefix_len;
    for (size_t i = 0; i < specs.size(); i++) {
        size_t lit_off = specs[i].first + specs[i].second;
        size_t lit_end = (i + 1 < specs.size()) ? specs[i + 1].first : strlen(pat);
        size_t lit_len = lit_end - lit_off;
        if (lit_len == 0) {
            // Trailing specifier: it swallows the rest of the string.
            out->push_back(text.substr(ti));
            ti = text.size();
            continue;
        }
        std::string lit(pat + lit_off, lit_len);
        size_t pos = text.find(lit, ti);
        if (pos == std::string::npos) return false;
        out->push_back(text.substr(ti, pos - ti));
        ti = pos + lit_len;
    }
    // Everything after the last literal must have been consumed.
    return ti == text.size();
}

// Rebuilds the translated pattern with the captured values substituted back in.
std::string Substitute(const char* pat, const std::vector<std::string>& vals) {
    std::string out;
    size_t vi = 0;
    for (size_t i = 0; pat[i];) {
        size_t w = 0;
        if (ReadSpec(pat, i, &w)) {
            if (vi < vals.size()) out += vals[vi++];
            i += w;
            continue;
        }
        out += pat[i++];
    }
    return out;
}

const char* ColumnFor(const ErrRow& r, const std::string& loc) {
    if (loc == "zh_CN") return r.zh_cn;
    if (loc == "zh_TW") return r.zh_tw;
    if (loc == "ja") return r.ja;
    return nullptr;
}

}  // namespace

std::string TranslateError(const std::string& text) {
    if (text.empty()) return "";
    const std::string& loc = Locale();
    if (loc == "en") return "";
    for (const auto& r : kErrors) {
        const char* target = ColumnFor(r, loc);
        if (!target) continue;
        if (strcmp(r.en, text.c_str()) == 0) return target;
        std::vector<std::string> vals;
        if (MatchSpecs(r.en, text, &vals) && !vals.empty()) {
            return Substitute(target, vals);
        }
    }
    return "";
}

const LangInfo* LangTable(size_t* count) {
    RegisterAllOnce();
    auto& reg = Registry();
    *count = reg.size();
    // LangInfo is the first member of Lang, so this cast is well-defined and
    // saves a copy.
    static std::vector<LangInfo> infos;
    infos.clear();
    for (auto& l : reg) infos.push_back(l.info);
    return infos.data();
}

}  // namespace adbi18n
