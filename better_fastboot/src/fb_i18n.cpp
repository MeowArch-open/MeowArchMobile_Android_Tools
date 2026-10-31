/*
 * fb_i18n.cpp — message catalog runtime. See fb_i18n.h.
 * Language tables live in fb_i18n_<lang>.cpp; each defines an array of
 * {Msg, text} pairs and registers itself via RegisterLang() (called
 * explicitly from Init(), not via static-init self-registration, because the
 * TUs live in a static library whose unreferenced objects the linker drops).
 */
#include "fb_i18n.h"

#include <stdlib.h>
#include <string.h>

#include <mutex>
#include <string>
#include <vector>

namespace fbi18n {
namespace {

struct Lang {
    LangInfo info;
    // Dense lookup array indexed by Msg. Built from the sparse MsgEntry table
    // at registration; missing IDs stay nullptr so T() can fall back.
    const char* dense[kNumMessages];
};

// Filled by the static registrars in each fb_i18n_<lang>.cpp translation unit.
std::vector<Lang>& Registry() {
    static std::vector<Lang> r;
    return r;
}

// Index of the active language in Registry(); 0 == English is guaranteed to be
// registered first by fb_i18n_en.cpp's registrar ordering (English is the
// fallback and is looked up specially).
size_t g_active = 0;
std::string g_locale = "en";

std::string Normalize(std::string s) {
    // Strip encoding/modifier: "zh_CN.UTF-8" -> "zh_CN"; "C"/"POSIX" -> "en".
    if (s == "C" || s == "POSIX") return "en";
    size_t at = s.find_first_of(".@");
    if (at != std::string::npos) s = s.substr(0, at);
    return s;
}

// Find a language matching |code| exactly, else its base ("zh_CN" -> "zh"),
// else any registered language whose base equals |code| ("zh" -> "zh_CN").
// Returns the registry index, or 0 (English) when nothing matches.
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
        // Bare language code: accept the first registered variant of it.
        for (size_t i = 0; i < reg.size(); i++) {
            std::string entry(reg[i].info.code);
            size_t eus = entry.find('_');
            if (eus != std::string::npos && entry.substr(0, eus) == code) return i;
        }
    }
    return 0;  // English
}

}  // namespace

// Called from static initializers of each language TU.
void RegisterLang(const char* code, const char* native, const MsgEntry* table, size_t count) {
    auto& reg = Registry();
    Lang lang{};
    lang.info = {code, native};
    for (size_t i = 0; i < count; i++) {
        if (table[i].id > 0 && table[i].id < kNumMessages) {
            lang.dense[table[i].id] = table[i].text;
        }
    }
    // Keep English first so Lookup()'s fallback and index 0 agree.
    if (!strcmp(code, "en")) {
        reg.insert(reg.begin(), std::move(lang));
        return;
    }
    reg.push_back(std::move(lang));
}

// Each language TU exposes an explicit registrar function. Static-init-based
// self-registration would also work, but these TUs live in a static library
// and the linker drops unreferenced objects — Init() calls them directly so
// every shipped table is guaranteed present.
void RegisterLangEn();     // fb_i18n_en.cpp
void RegisterLangZhCn();   // fb_i18n_zh_cn.cpp
void RegisterLangZhTw();   // fb_i18n_zh_tw.cpp
void RegisterLangJa();     // fb_i18n_ja.cpp

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
        // LANGUAGE may be a colon-separated list; take the first entry.
        std::string first(val);
        size_t colon = first.find(':');
        if (colon != std::string::npos) first = first.substr(0, colon);
        std::string norm = Normalize(first);
        if (norm == "en") {
            g_locale = "en";
            g_active = Lookup("en");
            return;
        }
        size_t idx = Lookup(norm);
        if (idx != 0 || norm == "en") {
            g_active = idx;
            g_locale = Registry()[idx].info.code;
            return;
        }
        // Not found: keep scanning other variables.
    }
    g_locale = "en";
    g_active = Lookup("en");
}

const char* T(Msg id) {
    auto& reg = Registry();
    if (reg.empty() || id <= 0 || id >= kNumMessages) return "";
    size_t idx = g_active < reg.size() ? g_active : 0;
    const char* s = reg[idx].dense[id];
    if (s && s[0]) return s;
    // Missing entry in a partial table: fall back to English.
    if (idx != 0) {
        s = reg[0].dense[id];
        if (s && s[0]) return s;
    }
    return "";
}

const std::string& Locale() { return g_locale; }

const LangInfo* LangTable(size_t* count) {
    // Materialize a plain LangInfo array on first use (all languages are
    // registered by static-init time, so this is stable afterwards).
    static std::vector<LangInfo> infos;
    static std::once_flag once;
    std::call_once(once, [] {
        for (const auto& l : Registry()) infos.push_back(l.info);
    });
    *count = infos.size();
    return infos.empty() ? nullptr : infos.data();
}

}  // namespace fbi18n
