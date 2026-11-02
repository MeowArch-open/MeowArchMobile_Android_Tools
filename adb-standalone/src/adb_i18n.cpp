/*
 * adb_i18n.cpp — message catalog runtime. See adb_i18n.h.
 *
 * Language tables live in adb_i18n_<lang>.cpp; each exposes an explicit
 * registrar called from Init(), because these TUs sit in a static library
 * whose unreferenced objects the linker would otherwise drop.
 */
#include "adb_i18n.h"

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
