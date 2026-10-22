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
#include <filesystem>
#include <fstream>
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
std::string compactName(std::string_view name) {
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

ReferenceLibrary::NameKey ReferenceLibrary::NameKey::of(std::string_view name) {
    NameKey k;
    k.compact = compactName(name);
    std::string letters;
    for (char c : k.compact) (c >= '0' && c <= '9' ? k.digits : letters) += c;
    k.phonetic = letters.empty() ? std::string() : phoneticKey(letters);
    return k;
}

bool ReferenceLibrary::NameKey::matches(const NameKey& o) const noexcept {
    if (compact.empty() || o.compact.empty()) return false;
    if (compact == o.compact) return true;
    // Les chiffres d'un nom (« 2hollis », « 21 savage ») sont exacts ; les
    // lettres tolèrent les fautes (« 2holis »), sauf sous 5 caractères
    // (« trop » n'est pas le genre « trap »).
    if (digits != o.digits || phonetic.empty() || o.phonetic.empty()) return false;
    if (std::min(compact.size(), o.compact.size()) < 5) return false;
    if (phonetic == o.phonetic) return true;
    const std::size_t len = std::min(phonetic.size(), o.phonetic.size());
    if (len < 4 || phonetic[0] != o.phonetic[0]) return false;
    const int allowed = len >= 8 ? 2 : 1;
    const auto diff = phonetic.size() > o.phonetic.size() ? phonetic.size() - o.phonetic.size() : o.phonetic.size() - phonetic.size();
    if (diff > static_cast<std::size_t>(allowed)) return false;
    return damerauLevenshtein(phonetic, o.phonetic) <= allowed;
}

bool namesMatch(std::string_view a, std::string_view b) {
    return ReferenceLibrary::NameKey::of(a).matches(ReferenceLibrary::NameKey::of(b));
}

bool ReferenceLibrary::parse(std::string_view text, std::string* error) {
    bool ok = true;
    std::istringstream in{std::string(text)};
    std::string line;
    ReferenceEntry current;
    bool open = false;
    bool hasMeasures = false;
    const auto close = [&] {
        current.measured = hasMeasures;
        hasMeasures = false;
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
        else if (key == "proches") current.similar = splitList(value);
        else if (key == "traits") current.traits = value;
        else if (key == "lufs") number_(s.integratedLufs);
        else if (key == "lra") number_(s.loudnessRangeLu);
        else if (key == "tp") number_(s.truePeakDb);
        else if (key == "plr") number_(s.plrDb);
        else if (key == "sub") number_(s.subEnergyDb);
        else if (key == "mid") {
            ok = parseBands(value, s.midDb) && ok;
            hasMeasures = true;
        }
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
        if (!e.similar.empty()) {
            out += "proches = ";
            for (std::size_t i = 0; i < e.similar.size(); ++i) out += (i ? " | " : "") + e.similar[i];
            out += "\n";
        }
        if (!e.traits.empty()) out += "traits = " + e.traits + "\n";
        if (!e.measured) {
            out += "# connu de nom : en attente d'un son pour le mesurer\n";
            continue;
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

void ReferenceLibrary::index(std::size_t i) {
    if (keys_.size() <= i) keys_.resize(i + 1);
    const auto& e = entries_[i];
    auto& k = keys_[i];
    k.artist = NameKey::of(e.artist);
    k.title = e.title.empty() ? NameKey{} : NameKey::of(e.title);
    k.artistTitle = e.title.empty() ? NameKey{} : NameKey::of(e.artist + " " + e.title);
    k.titleArtist = e.title.empty() ? NameKey{} : NameKey::of(e.title + " " + e.artist);
    k.aliases.clear();
    k.genres.clear();
    for (const auto& a : e.aliases) k.aliases.push_back(NameKey::of(a));
    for (const auto& g : e.genres) k.genres.push_back(NameKey::of(g));
}

void ReferenceLibrary::add(ReferenceEntry entry) {
    // Doublon = même artiste et même titre une fois compactés (« 2 Hollis » =
    // « 2hollis ») : recherche directe, la base pouvant compter des milliers
    // d'entrées.
    const std::string id = compactName(entry.artist) + "|" + compactName(entry.title);
    if (const auto it = exact_.find(id); it != exact_.end()) {
        auto& e = entries_[it->second];
        // Une entrée de nom ne remplace jamais des mesures ; elle peut
        // compléter genres et alias.
        const auto mergeInto = [](ReferenceEntry& to, const ReferenceEntry& from) {
            for (const auto& g : from.genres)
                if (std::none_of(to.genres.begin(), to.genres.end(), [&](const std::string& x) { return namesMatch(x, g); })) to.genres.push_back(g);
            for (const auto& a : from.aliases)
                if (std::find(to.aliases.begin(), to.aliases.end(), a) == to.aliases.end()) to.aliases.push_back(a);
            for (const auto& a : from.similar)
                if (std::none_of(to.similar.begin(), to.similar.end(), [&](const std::string& x) { return namesMatch(x, a); })) to.similar.push_back(a);
            if (to.traits.empty()) to.traits = from.traits;
        };
        if (!entry.measured && e.measured) {
            mergeInto(e, entry);
        } else {
            // Des mesures remplacent une entrée de nom (ou de plus anciennes
            // mesures) sans perdre ses genres ni ses alias.
            const ReferenceEntry previous = e;
            e = std::move(entry);
            if (!previous.measured || e.genres.empty()) mergeInto(e, previous);
            if (e.similar.empty()) e.similar = previous.similar;
            if (e.traits.empty()) e.traits = previous.traits;
        }
        index(it->second);
        return;
    }
    exact_[id] = entries_.size();
    if (entry.title.empty()) artistLevel_[compactName(entry.artist)] = entries_.size();
    entries_.push_back(std::move(entry));
    index(entries_.size() - 1);
}

ReferenceMatch ReferenceLibrary::find(std::string_view query) const {
    ReferenceMatch m;
    const NameKey q = NameKey::of(query);
    if (q.compact.empty()) return m;
    const auto same = [&](const NameKey& k, bool exact) { return exact ? (!k.compact.empty() && k.compact == q.compact) : k.matches(q); };
    const auto byArtist = [&](bool exact) {
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            bool hit = same(keys_[i].artist, exact);
            for (const auto& al : keys_[i].aliases) hit = hit || same(al, exact);
            if (hit) m.entries.push_back(&entries_[i]);
        }
        if (!m.found()) return false;
        m.kind = ReferenceMatch::Kind::Artist;
        m.how = "artiste " + m.entries.front()->artist + " (" + pluralTitles(m.entries.size()) + ")";
        return true;
    };
    const auto byArtistTitle = [&](bool exact) {
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (same(keys_[i].artistTitle, exact) || same(keys_[i].titleArtist, exact)) {
                m.kind = ReferenceMatch::Kind::Title;
                m.entries.push_back(&entries_[i]);
                m.how = "titre " + entries_[i].displayName();
                return true;
            }
        return false;
    };
    const auto byTitle = [&](bool exact) {
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (!entries_[i].title.empty() && same(keys_[i].title, exact)) {
                m.kind = ReferenceMatch::Kind::Title;
                m.entries.push_back(&entries_[i]);
                m.how = "titre " + entries_[i].displayName();
                return true;
            }
        return false;
    };
    // Prénom seul d'un artiste en plusieurs mots (« kanye », « travis »), s'il
    // ne désigne qu'un artiste : avant toute faute supposée (« kanye » n'est
    // pas « quannnic »).
    const auto byFirstName = [&]() {
        if (q.compact.size() < 4) return false;
        const std::string* artist = nullptr;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const std::string& name = entries_[i].artist;
            const auto space = name.find(' ');
            if (space == std::string::npos || compactName(name.substr(0, space)) != q.compact) continue;
            if (artist && compactName(*artist) != keys_[i].artist.compact) return false; // ambigu (« lil »)
            artist = &entries_[i].artist;
        }
        if (!artist) return false;
        const NameKey full = NameKey::of(*artist);
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (keys_[i].artist.compact == full.compact) m.entries.push_back(&entries_[i]);
        m.kind = ReferenceMatch::Kind::Artist;
        m.how = "artiste " + *artist + " (" + pluralTitles(m.entries.size()) + ")";
        return true;
    };
    // Exact : « artiste titre » > artiste > titre > prénom. Avec fautes :
    // l'artiste d'abord (« ken carsonn » est Ken Carson, pas « Ken Carson - Pots »).
    m.exact = true;
    if (byArtistTitle(true) || byArtist(true) || byTitle(true)) return m;
    if (byFirstName()) return m;
    m.exact = false;
    if (byArtist(false) || byArtistTitle(false) || byTitle(false)) return m;
    // Un genre.
    for (const bool exact : {true, false}) {
        std::string genre;
        for (std::size_t i = 0; i < entries_.size(); ++i)
            for (std::size_t g = 0; g < keys_[i].genres.size(); ++g)
                if (same(keys_[i].genres[g], exact)) {
                    m.entries.push_back(&entries_[i]);
                    genre = entries_[i].genres[g];
                    break;
                }
        if (m.found()) {
            m.kind = ReferenceMatch::Kind::Genre;
            m.exact = exact;
            m.how = "genre " + genre + " (" + pluralTitles(m.entries.size()) + ")";
            return m;
        }
    }
    return m;
}

ReferenceMatch ReferenceLibrary::findTitleOf(const std::vector<const ReferenceEntry*>& artistEntries, std::string_view title) const {
    ReferenceMatch m;
    const NameKey q = NameKey::of(title);
    if (q.phonetic.size() < 3) return m;
    for (const auto* e : artistEntries) {
        if (e < entries_.data() || e >= entries_.data() + entries_.size() || e->title.empty()) continue;
        const NameKey& k = keys_[static_cast<std::size_t>(e - entries_.data())].title;
        // L'artiste est déjà sûr : une faute est tolérée même sur un titre court.
        if (k.compact == q.compact || k.matches(q) ||
            (k.digits == q.digits && !k.phonetic.empty() && k.phonetic[0] == q.phonetic[0] && damerauLevenshtein(k.phonetic, q.phonetic) <= 1)) {
            m.kind = ReferenceMatch::Kind::Title;
            m.entries.push_back(e);
            m.how = "titre " + e->displayName();
            return m;
        }
    }
    return m;
}

std::vector<const ReferenceEntry*> ReferenceLibrary::measuredWithGenre(std::string_view genre) const {
    std::vector<const ReferenceEntry*> out;
    const NameKey q = NameKey::of(genre);
    const auto carries = [&](std::size_t i) {
        for (const auto& g : keys_[i].genres)
            if (g.matches(q)) return true;
        return false;
    };
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (!entries_[i].measured) continue;
        const auto artist = artistLevel_.find(keys_[i].artist.compact);
        if (carries(i) || (artist != artistLevel_.end() && carries(artist->second))) out.push_back(&entries_[i]);
    }
    return out;
}

ReferenceLibrary::Neighbours ReferenceLibrary::measuredSimilarTo(std::string_view artist, std::size_t maxArtists) const {
    Neighbours out;
    const NameKey self = NameKey::of(artist);
    if (self.compact.empty()) return out;
    const auto isArtist = [](const EntryKeys& k, const NameKey& name) {
        if (k.artist.matches(name)) return true;
        return std::any_of(k.aliases.begin(), k.aliases.end(), [&](const NameKey& a) { return a.matches(name); });
    };
    // Proches cités par l'artiste (toutes ses entrées), puis artistes qui le citent.
    std::vector<std::string> candidates;
    const auto push = [&](const std::string& name) {
        if (!namesMatch(name, artist) &&
            std::none_of(candidates.begin(), candidates.end(), [&](const std::string& c) { return namesMatch(c, name); }))
            candidates.push_back(name);
    };
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (isArtist(keys_[i], self)) {
            for (const auto& s : entries_[i].similar) push(s);
            if (out.traits.empty()) out.traits = entries_[i].traits;
        }
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (!entries_[i].similar.empty() && !isArtist(keys_[i], self) &&
            std::any_of(entries_[i].similar.begin(), entries_[i].similar.end(), [&](const std::string& s) { return namesMatch(s, artist); }))
            push(entries_[i].artist);
    for (const auto& name : candidates) {
        if (out.artists.size() >= maxArtists) break;
        const NameKey key = NameKey::of(name);
        std::string heard;
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].measured && isArtist(keys_[i], key)) {
                out.entries.push_back(&entries_[i]);
                if (heard.empty()) heard = entries_[i].artist;
            }
        if (!heard.empty()) out.artists.push_back(heard);
    }
    return out;
}

std::vector<std::string> ReferenceLibrary::measuredArtists() const {
    std::vector<std::string> out;
    std::unordered_map<std::string, bool> seen;
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].measured && seen.emplace(keys_[i].artist.compact, true).second) out.push_back(entries_[i].artist);
    return out;
}

std::vector<std::string> ReferenceLibrary::artists() const {
    std::vector<std::string> out;
    std::unordered_map<std::string, bool> seen;
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (seen.emplace(keys_[i].artist.compact, true).second) out.push_back(entries_[i].artist);
    return out;
}

std::vector<std::string> ReferenceLibrary::genres() const {
    std::vector<std::string> out;
    std::unordered_map<std::string, bool> seen;
    for (std::size_t i = 0; i < entries_.size(); ++i)
        for (std::size_t g = 0; g < entries_[i].genres.size(); ++g)
            if (seen.emplace(keys_[i].genres[g].compact, true).second) out.push_back(entries_[i].genres[g]);
    return out;
}

bool parseArtistLink(std::string_view line, ReferenceEntry& out) {
    std::string text;
    for (char c : line)
        if (c != '*' && c != '`') text += c; // gras / code markdown
    std::string_view t = text;
    const auto skipSpaces = [&] {
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.remove_prefix(1);
    };
    skipSpaces();
    if (t.empty() || t.front() == '#' || t.find(';') == std::string_view::npos) return false;
    // Puces (« - », « • », « > ») et numérotation (« 12. », « 3) ») ; pas « 21 Savage ».
    for (bool more = true; more;) {
        more = false;
        if (t.front() == '-' || t.front() == '>') t.remove_prefix(1), more = true;
        else if (t.rfind("\xE2\x80\xA2", 0) == 0) t.remove_prefix(3), more = true;
        else if (const auto d = t.find_first_not_of("0123456789"); d > 0 && d < t.size() && (t[d] == '.' || t[d] == ')'))
            t.remove_prefix(d + 1), more = true;
        skipSpaces();
        if (t.empty()) return false;
    }

    std::vector<std::string> fields;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= t.size(); ++i)
        if (i == t.size() || t[i] == ';') {
            fields.push_back(trim(t.substr(start, i - start)));
            start = i + 1;
        }
    // « A | B » ; « A, B » seulement sans « | » (« Tyler, The Creator »).
    const auto commaList = [](std::string s) {
        if (s.find('|') == std::string::npos)
            for (char& c : s)
                if (c == ',') c = '|';
        return splitList(s);
    };
    // « proches : … », « traits sonores : … » : champ nommé, à n'importe quelle place.
    const auto labelled = [](const std::string& f, const char* label, std::string& value) {
        std::string lower;
        for (char c : f.substr(0, f.find(':'))) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (f.find(':') == std::string::npos || trim(lower).rfind(label, 0) != 0) return false;
        value = trim(std::string_view(f).substr(f.find(':') + 1));
        return true;
    };
    ReferenceEntry e;
    e.measured = false;
    if (!labelled(fields[0], "artiste", e.artist)) e.artist = fields[0];
    if (e.artist.empty()) return false;
    const auto addGenres = [&](const std::string& list) {
        for (auto& g : commaList(list))
            if (std::none_of(e.genres.begin(), e.genres.end(), [&](const std::string& x) { return namesMatch(x, g); }))
                e.genres.push_back(std::move(g));
    };
    std::size_t plain = 0;
    for (std::size_t i = 1; i < fields.size(); ++i) {
        std::string value;
        if (labelled(fields[i], "proche", value)) e.similar = commaList(value);
        else if (labelled(fields[i], "trait", value)) e.traits = value;
        else if (labelled(fields[i], "famille", value) || labelled(fields[i], "style", value) || labelled(fields[i], "genre", value))
            addGenres(value);
        else if (plain++ < 2) addGenres(fields[i]); // famille, puis styles
    }
    out = std::move(e);
    return true;
}

bool addToLibraryFile(const std::string& path, const ReferenceEntry& entry) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    ReferenceLibrary library;
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::ostringstream text;
            text << in.rdbuf();
            library.parse(text.str());
        }
    }
    library.add(entry);
    const std::string temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << library.serialize();
        if (!out) return false;
    }
    fs::rename(temp, path, ec);
    return !ec;
}

const ReferenceLibrary& ReferenceLibrary::builtin() {
    static const ReferenceLibrary library = [] {
        ReferenceLibrary l;
        l.parse(data::references()); // titres mesurés
        l.parse(data::catalogue());  // artistes et titres connus de nom (complètent les genres)
        return l;
    }();
    return library;
}

analysis::SpectralSignature aggregateSignature(const std::vector<const ReferenceEntry*>& all) {
    analysis::SpectralSignature out;
    std::vector<const ReferenceEntry*> entries;
    for (const auto* e : all)
        if (e->measured) entries.push_back(e);
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
