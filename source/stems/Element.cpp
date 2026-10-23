// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/Element.cpp
// =============================================================================
#include "Element.h"

#include "ai/EmbeddedData.h"
#include "ai/TextMatch.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>
#include <vector>

namespace omnis::stems {

namespace {

struct Info {
    const char* key;
    const char* label;
    Family family;
};

constexpr Info kInfo[kElementCount] = {
    {"auto", "Auto", Family::Unknown},
    {"kick", "Kick", Family::Drums},
    {"snare", "Snare", Family::Drums},
    {"clap", "Clap", Family::Drums},
    {"hihat", "Hi-hat", Family::Drums},
    {"openhat", "Open hat", Family::Drums},
    {"cymbal", "Cymbale", Family::Drums},
    {"perc", "Percu", Family::Drums},
    {"drumloop", "Drums (boucle)", Family::Drums},
    {"808", "808", Family::Bass},
    {"bass", "Basse", Family::Bass},
    {"keys", "Piano / clavier", Family::Instrument},
    {"guitar", "Guitare", Family::Instrument},
    {"brass", "Cuivres / vents", Family::Instrument},
    {"strings", "Cordes", Family::Instrument},
    {"pad", "Nappe", Family::Instrument},
    {"lead", "Lead / synthé", Family::Instrument},
    {"pluck", "Pluck / cloche", Family::Instrument},
    {"vocal", "Voix lead", Family::Vocal},
    {"backs", "Backs / adlibs", Family::Vocal},
    {"fx", "FX", Family::Fx},
    {"other", "Autre", Family::Unknown},
};

std::array<std::string_view, kElementCount> labels(const char* first) {
    std::array<std::string_view, kElementCount> out{};
    for (int i = 0; i < kElementCount; ++i) out[static_cast<std::size_t>(i)] = i == 0 ? first : kInfo[i].label;
    return out;
}

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

struct Form {
    Element element;
    std::vector<std::string> words;
    std::string joined; ///< mots collés (« closedhat »)
};

const std::vector<Form>& forms() {
    static const std::vector<Form> table = [] {
        std::vector<Form> t;
        std::istringstream in{std::string(ai::data::elements())};
        std::string line;
        while (std::getline(in, line)) {
            const std::string s = trim(line);
            const auto colon = s.find(':');
            if (s.empty() || s[0] == '#' || colon == std::string::npos) continue;
            const auto element = elementFromKey(trim(std::string_view(s).substr(0, colon)));
            if (!element) continue;
            std::string_view rest = std::string_view(s).substr(colon + 1);
            while (!rest.empty()) {
                const auto bar = rest.find('|');
                const std::string form = trim(rest.substr(0, bar));
                rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);
                Form f{*element, {}, {}};
                for (auto& w : ai::tokenize(form))
                    if (ai::isWord(w)) {
                        f.joined += w;
                        f.words.push_back(std::move(w));
                    }
                if (!f.words.empty()) t.push_back(std::move(f));
            }
        }
        return t;
    }();
    return table;
}

bool isAsciiAlnum(unsigned char c) { return std::isalnum(c) != 0; }

} // namespace

Family familyOf(Element e) noexcept {
    const int i = static_cast<int>(e);
    return i >= 0 && i < kElementCount ? kInfo[i].family : Family::Unknown;
}

const char* familyLabel(Family f) noexcept {
    switch (f) {
        case Family::Drums: return "drums";
        case Family::Bass: return "basse";
        case Family::Instrument: return "instrument";
        case Family::Vocal: return "voix";
        case Family::Fx: return "fx";
        case Family::Unknown:
        default: return "inconnue";
    }
}

const char* elementLabel(Element e) noexcept {
    const int i = static_cast<int>(e);
    return i >= 0 && i < kElementCount ? kInfo[i].label : "Autre";
}

const char* elementKey(Element e) noexcept {
    const int i = static_cast<int>(e);
    return i >= 0 && i < kElementCount ? kInfo[i].key : "other";
}

std::optional<Element> elementFromKey(std::string_view key) noexcept {
    for (int i = 0; i < kElementCount; ++i)
        if (key == kInfo[i].key) return static_cast<Element>(i);
    return std::nullopt;
}

const std::array<std::string_view, kElementCount>& elementChoiceLabels() noexcept {
    static const auto l = labels("Auto");
    return l;
}

const std::array<std::string_view, kElementCount>& elementMeterLabels() noexcept {
    static const auto l = labels("—");
    return l;
}

std::vector<std::string> nameWords(std::string_view name) {
    // Découpage sur le texte d'origine (la casse compte), puis chaque mot est
    // normalisé (minuscules, accents retirés). Les octets non ASCII (lettres
    // accentuées en UTF-8) restent dans le mot.
    std::vector<std::string> raw;
    std::string current;
    const auto flush = [&] {
        if (!current.empty()) raw.push_back(current);
        current.clear();
    };
    for (std::size_t i = 0; i < name.size(); ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        if (c >= 0x80) {
            current += static_cast<char>(c);
            continue;
        }
        if (!isAsciiAlnum(c)) {
            flush();
            continue;
        }
        if (!current.empty()) {
            const auto prev = static_cast<unsigned char>(current.back());
            const bool caseUp = prev < 0x80 && std::islower(prev) && std::isupper(c);
            const bool digitEdge = prev < 0x80 && (std::isdigit(prev) != 0) != (std::isdigit(c) != 0);
            if (caseUp || digitEdge) flush();
        }
        current += static_cast<char>(c);
    }
    flush();
    std::vector<std::string> words;
    for (const auto& w : raw)
        for (auto& t : ai::tokenize(w))
            if (ai::isWord(t)) words.push_back(std::move(t));
    return words;
}

NameHint elementFromName(std::string_view trackName) {
    const auto words = nameWords(trackName);
    NameHint best;
    for (const auto& f : forms()) {
        int score = 0;
        if (f.words.size() > 1) {
            // Mots consécutifs, ou collés dans un seul mot (« closedhat »).
            for (std::size_t i = 0; i + f.words.size() <= words.size() && !score; ++i)
                if (std::equal(f.words.begin(), f.words.end(), words.begin() + static_cast<std::ptrdiff_t>(i)))
                    score = static_cast<int>(f.joined.size()) + 1;
            for (const auto& w : words)
                if (!score && w.find(f.joined) != std::string::npos) score = static_cast<int>(f.joined.size());
        } else {
            const std::string& form = f.words[0];
            for (const auto& w : words) {
                if (w == form) score = std::max(score, static_cast<int>(form.size()) + 1);
                // Collée à d'autres lettres (« jayykick ») : seulement une forme longue.
                else if (form.size() >= 4 && w.find(form) != std::string::npos) score = std::max(score, static_cast<int>(form.size()));
            }
        }
        if (score > 0) {
            // Un nombre (« 808 ») est un nom voulu ; une voix (« vox lead ») passe
            // avant le mot d'instrument qui l'accompagne.
            const bool digits = std::any_of(f.joined.begin(), f.joined.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
            if (digits) score += 3;
            if (f.element == Element::Vocal || f.element == Element::Backs) score += 2;
        }
        if (score > best.strength) best = {f.element, f.joined, score};
    }
    return best;
}

} // namespace omnis::stems
