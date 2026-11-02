// =============================================================================
//  Abrasion Labs // OMNIS
//  core/I18n.cpp
// =============================================================================
#include "I18n.h"

#include "UserDataDir.h"
#include "ai/EmbeddedData.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace omnis::i18n {

namespace {

std::atomic<int> gLang{-1}; // -1 : pas encore décidée

bool systemIsFrench() {
#if defined(_WIN32)
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;
#elif defined(__APPLE__)
    bool fr = false;
    if (CFArrayRef langs = CFLocaleCopyPreferredLanguages()) {
        if (CFArrayGetCount(langs) > 0) {
            auto first = static_cast<CFStringRef>(CFArrayGetValueAtIndex(langs, 0));
            char buf[16] = {};
            if (CFStringGetCString(first, buf, sizeof buf, kCFStringEncodingUTF8)) fr = buf[0] == 'f' && buf[1] == 'r';
        }
        CFRelease(langs);
    }
    return fr;
#else
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"})
        if (const char* v = std::getenv(var); v && *v) return v[0] == 'f' && v[1] == 'r';
    return true; // sans indication : la langue d'origine
#endif
}

struct Table {
    std::unordered_map<std::string, std::string> exact;
    std::vector<std::pair<std::string, std::string>> fragments; ///< les plus longs d'abord
};

const Table& table() {
    static const Table t = [] {
        Table out;
        const std::string_view text = ai::data::i18n();
        std::size_t pos = 0;
        while (pos < text.size()) {
            std::size_t end = text.find('\n', pos);
            if (end == std::string_view::npos) end = text.size();
            std::string_view line = text.substr(pos, end - pos);
            pos = end + 1;
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (line.empty() || line.front() == '#') continue;
            const std::size_t tab = line.find('\t');
            if (tab == std::string_view::npos || tab == 0) continue;
            std::string fr(line.substr(0, tab)), en(line.substr(tab + 1));
            out.exact.emplace(fr, en);
            // Morceau remplaçable au milieu d'un texte : assez long, ou avec une
            // ponctuation / un espace (pas « OUI » au milieu d'un mot).
            if (fr.size() >= 6 || fr.find_first_of(" :«»()") != std::string::npos) out.fragments.emplace_back(fr, en);
        }
        std::sort(out.fragments.begin(), out.fragments.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
        return out;
    }();
    return t;
}

bool isWordByte(unsigned char c) { return std::isalnum(c) || c >= 0x80; }

std::string translateFragments(std::string s) {
    for (const auto& [fr, en] : table().fragments) {
        for (std::size_t at = s.find(fr); at != std::string::npos; at = s.find(fr, at)) {
            // Un morceau qui commence / finit par une lettre doit tomber sur une limite de mot.
            const bool startOk = !isWordByte(static_cast<unsigned char>(fr.front())) || at == 0 || !isWordByte(static_cast<unsigned char>(s[at - 1]));
            const std::size_t after = at + fr.size();
            const bool endOk = !isWordByte(static_cast<unsigned char>(fr.back())) || after >= s.size() || !isWordByte(static_cast<unsigned char>(s[after]));
            if (startOk && endOk) {
                s.replace(at, fr.size(), en);
                at += en.size();
            } else {
                at += 1;
            }
        }
    }
    return s;
}

} // namespace

Lang language() noexcept {
    int v = gLang.load(std::memory_order_acquire);
    if (v < 0) {
        v = systemIsFrench() ? 0 : 1;
        // OMNIS_LANG=en|fr : imposée (outils, captures d'écran).
        if (const char* env = std::getenv("OMNIS_LANG"); env && (env[0] == 'e' || env[0] == 'f')) {
            gLang.store(env[0] == 'e' ? 1 : 0, std::memory_order_release);
            return env[0] == 'e' ? Lang::En : Lang::Fr;
        }
        try {
            std::ifstream in(userDataDir() / "langue.txt");
            std::string word;
            if (in >> word) v = word.rfind("en", 0) == 0 ? 1 : word.rfind("fr", 0) == 0 ? 0 : v;
        } catch (...) {
        }
        gLang.store(v, std::memory_order_release);
    }
    return v == 1 ? Lang::En : Lang::Fr;
}

void setLanguage(Lang lang) {
    gLang.store(lang == Lang::En ? 1 : 0, std::memory_order_release);
    try {
        std::error_code ec;
        std::filesystem::create_directories(userDataDir(), ec);
        std::ofstream(userDataDir() / "langue.txt") << (lang == Lang::En ? "en\n" : "fr\n");
    } catch (...) {
    }
}

void overrideLanguageForTests(Lang lang) noexcept { gLang.store(lang == Lang::En ? 1 : 0, std::memory_order_release); }

std::string tr(std::string_view french) {
    if (language() == Lang::Fr || french.empty()) return std::string(french);
    static std::mutex mutex;
    static std::unordered_map<std::string, std::string> cache;
    std::lock_guard<std::mutex> lock(mutex);
    std::string key(french);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    const auto& t = table();
    std::string out;
    if (auto it = t.exact.find(key); it != t.exact.end()) out = it->second;
    else out = translateFragments(key);
    if (cache.size() > 4096) cache.clear(); // mesures qui bougent : pas de croissance sans fin
    cache.emplace(std::move(key), out);
    return out;
}

} // namespace omnis::i18n
