// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/ReferenceLibrary.cpp
// =============================================================================
#include "ReferenceLibrary.h"

#include "EmbeddedData.h"
#include "TextMatch.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>

namespace omnis::ai {

namespace {

constexpr int kBands = params::kMatchBands;

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

std::vector<std::string> splitList(std::string_view s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == '|') {
            auto item = trim(s.substr(start, i - start));
            if (!item.empty()) out.push_back(std::move(item));
            start = i + 1;
        }
    return out;
}

/// Nom compacté : mots normalisés collés (« 2 Hollis » -> « 2hollis »).
std::string compact(std::string_view name) {
    std::string out;
    for (const auto& t : tokenize(name))
        if (isWord(t)) out += t;
    return out;
}

bool parseBands(const std::string& value, analysis::BandArray& out) {
    std::istringstream in(value);
    in.imbue(std::locale::classic());
    for (auto& v : out)
        if (!(in >> v)) return false;
    return true;
}

std::string formatBands(const analysis::BandArray& v) {
    std::string s;
    char buf[32];
    for (std::size_t k = 0; k < v.size(); ++k) {
        std::snprintf(buf, sizeof(buf), "%s%.2f", k ? " " : "", v[k]);
        s += buf;
    }
    return s;
}

std::string pluralTitles(std::size_t n) { return std::to_string(n) + (n > 1 ? " titres" : " titre"); }

} // namespace

std::string ReferenceEntry::displayName() const { return title.empty() ? artist : artist + " - " + title; }

bool namesMatch(std::string_view a, std::string_view b) {
    const std::string ca = compact(a), cb = compact(b);
    if (ca.empty() || cb.empty()) return false;
    if (ca == cb) return true;
    // Les chiffres d'un nom (« 2hollis », « 21 savage ») doivent être exacts,
    // les lettres tolèrent les fautes (« 2holis »).
    std::string digitsA, digitsB, lettersA, lettersB;
    for (char c : ca) (c >= '0' && c <= '9' ? digitsA : lettersA) += c;
    for (char c : cb) (c >= '0' && c <= '9' ? digitsB : lettersB) += c;
    if (digitsA != digitsB || lettersA.empty() || lettersB.empty()) return false;
    return fuzzyEquals(lettersA, lettersB);
}

bool ReferenceLibrary::parse(std::string_view text, std::string* error) {
    bool ok = true;
    std::istringstream in{std::string(text)};
    std::string line;
    ReferenceEntry current;
    bool open = false;
    const auto close = [&] {
        if (open && !current.artist.empty()) add(std::move(current));
        current = ReferenceEntry{};
        open = false;
    };
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t.front() == '[') {
            close();
            open = true;
            continue;
        }
        const auto eq = t.find('=');
        if (!open || eq == std::string::npos) {
            ok = false;
            if (error) *error += "ligne " + std::to_string(number) + " : « clé = valeur » attendu dans une entrée [ ]\n";
            continue;
        }
        const std::string key = trim(t.substr(0, eq)), value = trim(t.substr(eq + 1));
        auto& s = current.signature;
        const auto number_ = [&](double& out) {
            std::istringstream v(value);
            v.imbue(std::locale::classic());
            if (!(v >> out)) ok = false;
        };
        if (key == "artiste") current.artist = value;
        else if (key == "titre") current.title = value;
        else if (key == "alias") current.aliases = splitList(value);
        else if (key == "genres") current.genres = splitList(value);
        else if (key == "lufs") number_(s.integratedLufs);
        else if (key == "lra") number_(s.loudnessRangeLu);
        else if (key == "tp") number_(s.truePeakDb);
        else if (key == "plr") number_(s.plrDb);
        else if (key == "sub") number_(s.subEnergyDb);
        else if (key == "mid") ok = parseBands(value, s.midDb) && ok;
        else if (key == "side") ok = parseBands(value, s.sideDb) && ok;
        else if (key == "dyn") ok = parseBands(value, s.midSpreadDb) && ok;
        else if (error) *error += "ligne " + std::to_string(number) + " : clé inconnue « " + key + " » ignorée\n";
    }
    close();
    return ok;
}

std::string ReferenceLibrary::serialize() const {
    std::string out = "# OMNIS — base de références (signatures mesurées, aucun audio)\n";
    char buf[64];
    for (const auto& e : entries_) {
        const auto& s = e.signature;
        out += "\n[" + e.displayName() + "]\n";
        out += "artiste = " + e.artist + "\n";
        if (!e.title.empty()) out += "titre = " + e.title + "\n";
        if (!e.aliases.empty()) {
            out += "alias = ";
            for (std::size_t i = 0; i < e.aliases.size(); ++i) out += (i ? " | " : "") + e.aliases[i];
            out += "\n";
        }
        if (!e.genres.empty()) {
            out += "genres = ";
            for (std::size_t i = 0; i < e.genres.size(); ++i) out += (i ? " | " : "") + e.genres[i];
            out += "\n";
        }
        for (const auto& [key, value] : {std::pair{"lufs", s.integratedLufs}, std::pair{"lra", s.loudnessRangeLu},
                                         std::pair{"tp", s.truePeakDb}, std::pair{"plr", s.plrDb}, std::pair{"sub", s.subEnergyDb}}) {
            std::snprintf(buf, sizeof(buf), "%s = %.2f\n", key, value);
            out += buf;
        }
        out += "mid = " + formatBands(s.midDb) + "\n";
        out += "side = " + formatBands(s.sideDb) + "\n";
        out += "dyn = " + formatBands(s.midSpreadDb) + "\n";
    }
    return out;
}

void ReferenceLibrary::add(ReferenceEntry entry) {
    for (auto& e : entries_)
        if (namesMatch(e.artist, entry.artist) && namesMatch(e.title.empty() ? e.artist : e.title, entry.title.empty() ? entry.artist : entry.title)) {
            e = std::move(entry);
            return;
        }
    entries_.push_back(std::move(entry));
}

ReferenceMatch ReferenceLibrary::find(std::string_view query) const {
    ReferenceMatch m;
    if (compact(query).empty()) return m;
    // 1. Un titre (« poster boy », « 2hollis poster boy »).
    for (const auto& e : entries_)
        if (!e.title.empty() && (namesMatch(query, e.title) || namesMatch(query, e.artist + " " + e.title) ||
                                 namesMatch(query, e.title + " " + e.artist))) {
            m.entries.push_back(&e);
            m.how = "titre " + e.displayName();
            return m;
        }
    // 2. Un artiste (ou un alias).
    for (const auto& e : entries_) {
        bool hit = namesMatch(query, e.artist);
        for (const auto& a : e.aliases) hit = hit || namesMatch(query, a);
        if (hit) m.entries.push_back(&e);
    }
    if (m.found()) {
        m.how = "artiste " + m.entries.front()->artist + " (" + pluralTitles(m.entries.size()) + ")";
        return m;
    }
    // 3. Un genre.
    std::string genre;
    for (const auto& e : entries_)
        for (const auto& g : e.genres)
            if (namesMatch(query, g)) {
                m.entries.push_back(&e);
                genre = g;
                break;
            }
    if (m.found()) m.how = "genre " + genre + " (" + pluralTitles(m.entries.size()) + ")";
    return m;
}

std::vector<std::string> ReferenceLibrary::artists() const {
    std::vector<std::string> out;
    for (const auto& e : entries_)
        if (std::none_of(out.begin(), out.end(), [&](const std::string& a) { return namesMatch(a, e.artist); })) out.push_back(e.artist);
    return out;
}

std::vector<std::string> ReferenceLibrary::genres() const {
    std::vector<std::string> out;
    for (const auto& e : entries_)
        for (const auto& g : e.genres)
            if (std::none_of(out.begin(), out.end(), [&](const std::string& x) { return namesMatch(x, g); })) out.push_back(g);
    return out;
}

const ReferenceLibrary& ReferenceLibrary::builtin() {
    static const ReferenceLibrary library = [] {
        ReferenceLibrary l;
        l.parse(data::references());
        return l;
    }();
    return library;
}

analysis::SpectralSignature aggregateSignature(const std::vector<const ReferenceEntry*>& entries) {
    analysis::SpectralSignature out;
    if (entries.empty()) return out;
    out.integratedLufs = out.loudnessRangeLu = out.truePeakDb = out.plrDb = out.subEnergyDb = 0.0;
    const double n = static_cast<double>(entries.size());
    for (const auto* e : entries) {
        const auto& s = e->signature;
        const double body = s.bodyDb();
        for (std::size_t k = 0; k < static_cast<std::size_t>(kBands); ++k) {
            out.midDb[k] += (s.midDb[k] - body) / n;
            out.sideDb[k] += (s.sideDb[k] - body) / n;
            out.midSpreadDb[k] += s.midSpreadDb[k] / n;
        }
        out.subEnergyDb += (s.subEnergyDb - body) / n;
        out.integratedLufs += s.integratedLufs / n;
        out.loudnessRangeLu += s.loudnessRangeLu / n;
        out.truePeakDb += s.truePeakDb / n;
        out.plrDb += s.plrDb / n;
        out.activeFrames += s.activeFrames;
    }
    return out;
}

} // namespace omnis::ai
