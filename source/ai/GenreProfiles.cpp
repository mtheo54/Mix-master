// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/GenreProfiles.cpp
// =============================================================================
#include "GenreProfiles.h"

#include "EmbeddedData.h"
#include "TextMatch.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <sstream>

namespace omnis::ai {

namespace {

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
    return s;
}

/// Ce qui décrit un son, pour comparer deux titres (non réduit).
std::vector<double> features(const ReferenceEntry& e) {
    const auto& s = e.signature;
    std::vector<double> f;
    for (int k = 1; k < 19; ++k) f.push_back(s.relativeMidDb(k)); // 28 Hz – 10 kHz
    for (int k = 6; k < 19; ++k) f.push_back(s.sideDb[static_cast<std::size_t>(k)] - s.midDb[static_cast<std::size_t>(k)]);
    for (int k = 2; k < 19; ++k) f.push_back(s.midSpreadDb[static_cast<std::size_t>(k)]);
    f.push_back(s.integratedLufs);
    f.push_back(s.plrDb);
    f.push_back(s.loudnessRangeLu);
    for (double v : s.music.microDb) f.push_back(v);
    f.push_back(s.music.centroidHz / 1000.0);
    f.push_back(s.music.onsetsPerSecond);
    for (double& v : f)
        if (!std::isfinite(v)) v = 0.0;
    return f;
}

ReferenceEntry makeProfile(const std::string& title, const std::vector<std::string>& genres, const std::vector<const ReferenceEntry*>& titles) {
    ReferenceEntry p;
    p.artist = kProfileArtist;
    p.title = title;
    p.genres = genres;
    p.signature = aggregateSignature(titles);
    p.signature.music = averageMusic(titles);
    // Une moyenne de tempos, de tonalités ou de notes ne veut rien dire, et
    // garderait la trace d'un titre : effacées.
    p.signature.music.bpm = 0.0;
    p.signature.music.subHz = 0.0;
    p.signature.music.keyRoot = -1;
    p.signature.music.keyConfidence = 0.0;
    p.signature.music.samplePeakDb = -200.0;
    p.measured = true;
    p.profileTitles = static_cast<int>(titles.size());
    return p;
}

} // namespace

const std::vector<std::pair<std::string, std::vector<std::string>>>& genreFamilies() {
    static const std::vector<std::pair<std::string, std::vector<std::string>>> families = [] {
        std::vector<std::pair<std::string, std::vector<std::string>>> out;
        std::istringstream in{std::string(data::classement())};
        for (std::string line; std::getline(in, line);) {
            if (line.empty() || line[0] == '#') continue;
            const auto arrow = line.find("=>");
            if (arrow == std::string::npos) continue;
            std::vector<std::string> words;
            std::istringstream w(line.substr(arrow + 2));
            for (std::string word; std::getline(w, word, ',');)
                if (auto t = trim(word); !t.empty()) words.push_back(t);
            if (!words.empty()) out.emplace_back(trim(line.substr(0, arrow)), words);
        }
        // Un mot de plusieurs familles (« rap » : rap fr ET rap us) ne les distingue pas.
        std::map<std::string, int> uses;
        for (const auto& [name, words] : out)
            for (const auto& w : words) ++uses[normalizeText(w)];
        for (auto& [name, words] : out)
            if (std::any_of(words.begin(), words.end(), [&](const std::string& w) { return uses[normalizeText(w)] == 1; }))
                std::erase_if(words, [&](const std::string& w) { return uses[normalizeText(w)] > 1; });
        return out;
    }();
    return families;
}

bool inGenreFamily(const ReferenceEntry& e, const std::string& family, const std::vector<std::string>& words) {
    if (e.isProfile()) return std::find(e.genres.begin(), e.genres.end(), family) != e.genres.end();
    for (const auto& g : e.genres)
        for (const auto& w : words)
            if (namesMatch(g, w)) return true;
    return false;
}

std::string artistKey(const std::string& name) {
    std::string out;
    for (char ch : normalizeText(name))
        if (ch != ' ') out += ch;
    return out;
}

const std::map<std::string, std::vector<std::string>>& artistStyles() {
    static const std::map<std::string, std::vector<std::string>> styles = [] {
        std::map<std::string, std::vector<std::string>> out;
        std::istringstream in{std::string(data::styles())};
        for (std::string line; std::getline(in, line);) {
            if (line.empty() || line[0] == '#') continue;
            const auto semi = line.find(';');
            if (semi == std::string::npos) continue;
            std::vector<std::string> words;
            std::istringstream w(line.substr(semi + 1));
            for (std::string word; std::getline(w, word, ',');)
                if (auto t = trim(word); !t.empty()) words.push_back(t);
            if (!words.empty()) out[artistKey(line.substr(0, semi))] = words;
        }
        return out;
    }();
    return styles;
}

/// La famille où data/styles_artistes.txt range un artiste (sa section « # --- … »,
/// même nom que la famille, casse et accents mis à part).
const std::map<std::string, std::string>& artistSection() {
    static const std::map<std::string, std::string> sections = [] {
        std::map<std::string, std::string> out;
        std::istringstream in{std::string(data::styles())};
        std::string family;
        for (std::string line; std::getline(in, line);) {
            if (line.rfind("# ---", 0) == 0) {
                std::string name = trim(line.substr(5));
                while (!name.empty() && (name.back() == '-' || name.back() == ' ')) name.pop_back();
                family.clear();
                for (const auto& [f, words] : genreFamilies())
                    if (normalizeText(f) == normalizeText(name)) family = f;
                continue;
            }
            if (line.empty() || line[0] == '#' || family.empty()) continue;
            if (const auto semi = line.find(';'); semi != std::string::npos) out[artistKey(line.substr(0, semi))] = family;
        }
        return out;
    }();
    return sections;
}

std::vector<std::string> stylesOfArtist(const std::string& artist) {
    const auto& styles = artistStyles();
    if (const auto it = styles.find(artistKey(artist)); it != styles.end()) return it->second;
    std::string rest = artist;
    for (const char* sep : {" feat. ", " feat ", " ft. ", " x ", " X ", " & ", ", "})
        for (std::size_t at; (at = rest.find(sep)) != std::string::npos;) rest.replace(at, std::strlen(sep), "\n");
    std::vector<std::string> out;
    std::istringstream parts(rest);
    for (std::string part; std::getline(parts, part);)
        if (const auto it = styles.find(artistKey(part)); it != styles.end())
            for (const auto& g : it->second)
                if (std::find(out.begin(), out.end(), g) == out.end()) out.push_back(g);
    return out;
}

GenreProfileSet buildGenreProfiles(const std::vector<const ReferenceEntry*>& all, int minTitles, const ReferenceLibrary* names) {
    GenreProfileSet set;
    minTitles = std::max(1, minTitles);
    const auto& families = genreFamilies();
    std::map<std::string, std::vector<std::string>> artistGenres;
    if (names)
        for (const auto& e : names->entries())
            if (e.title.empty())
                for (const auto& g : e.genres) artistGenres[artistKey(e.artist)].push_back(g);

    struct Item {
        const ReferenceEntry* e;
        std::string family;
        std::vector<std::string> candidates;
        std::string style;
        std::vector<double> f;
    };
    std::vector<Item> items;
    for (const auto* e : all) {
        if (!e->measured || e->isProfile()) continue;
        ReferenceEntry withArtist; // genres du titre, puis ceux de son artiste
        withArtist.genres = e->genres;
        if (const auto it = artistGenres.find(artistKey(e->artist)); it != artistGenres.end())
            withArtist.genres.insert(withArtist.genres.end(), it->second.begin(), it->second.end());
        std::string family;
        for (const auto& [name, words] : families)
            if (inGenreFamily(withArtist, name, words)) {
                family = name;
                break;
            }
        if (family.empty()) { // pas de genre : la section de l'artiste dans styles_artistes.txt
            if (const auto it = artistSection().find(artistKey(e->artist)); it != artistSection().end()) family = it->second;
        }
        if (family.empty()) {
            set.unclassified.push_back(e->displayName());
            continue;
        }
        items.push_back({e, family, stylesOfArtist(e->artist), {}, features(*e)});
    }
    if (items.empty()) return set;

    // Centrer-réduire chaque grandeur sur toute la base.
    const std::size_t dims = items.front().f.size();
    std::vector<double> mean(dims, 0.0), sd(dims, 0.0);
    for (const auto& it : items)
        for (std::size_t d = 0; d < dims; ++d) mean[d] += it.f[d] / static_cast<double>(items.size());
    for (const auto& it : items)
        for (std::size_t d = 0; d < dims; ++d) sd[d] += (it.f[d] - mean[d]) * (it.f[d] - mean[d]) / static_cast<double>(items.size());
    for (auto& v : sd) v = std::sqrt(v) > 1e-9 ? std::sqrt(v) : 1.0;
    for (auto& it : items)
        for (std::size_t d = 0; d < dims; ++d) it.f[d] = (it.f[d] - mean[d]) / sd[d];

    // k-moyennes restreintes : un centre par (famille, sous-genre).
    using Key = std::pair<std::string, std::string>;
    std::map<Key, std::vector<double>> centre;
    const auto recompute = [&](bool initial) {
        std::map<Key, std::vector<double>> sum;
        std::map<Key, double> weight;
        for (const auto& it : items) {
            if (it.candidates.empty()) continue;
            const auto add = [&](const std::string& style, double w) {
                auto& s = sum[{it.family, style}];
                s.resize(dims, 0.0);
                for (std::size_t d = 0; d < dims; ++d) s[d] += w * it.f[d];
                weight[{it.family, style}] += w;
            };
            if (initial) {
                for (const auto& c : it.candidates) add(c, 1.0 / static_cast<double>(it.candidates.size()));
            } else {
                add(it.style, 1.0);
            }
        }
        // Un sous-genre qui a perdu tous ses titres garde son centre : il peut en
        // reprendre au tour suivant.
        if (initial) centre.clear();
        for (auto& [k, s] : sum) {
            for (auto& v : s) v /= weight[k];
            centre[k] = s;
        }
    };
    const auto distance = [&](const std::vector<double>& a, const std::vector<double>& b) {
        double d = 0.0;
        for (std::size_t i = 0; i < dims; ++i) d += (a[i] - b[i]) * (a[i] - b[i]);
        return d;
    };
    recompute(true);
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (auto& it : items) {
            if (it.candidates.empty()) continue;
            std::string best = it.candidates.front();
            double bestD = std::numeric_limits<double>::infinity();
            for (const auto& c : it.candidates)
                if (const auto found = centre.find({it.family, c}); found != centre.end()) {
                    const double d = distance(it.f, found->second);
                    if (d < bestD) bestD = d, best = c;
                }
            if (best != it.style) it.style = best, changed = true;
        }
        recompute(false);
        if (!changed) break;
    }

    // Profils : chaque famille, puis chaque sous-genre d'au moins minTitles titres.
    std::map<std::string, std::vector<const ReferenceEntry*>> byFamily;
    std::map<Key, std::vector<const ReferenceEntry*>> byStyle;
    for (const auto& it : items) {
        byFamily[it.family].push_back(it.e);
        if (!it.style.empty()) byStyle[{it.family, it.style}].push_back(it.e);
    }
    for (const auto& [name, words] : families) {
        const auto f = byFamily.find(name);
        if (f == byFamily.end() || static_cast<int>(f->second.size()) < minTitles) continue;
        std::vector<std::string> genres = words;
        genres.push_back(name);
        set.profiles.push_back(makeProfile(name, genres, f->second));
        for (const auto& [key, titles] : byStyle)
            if (key.first == name && static_cast<int>(titles.size()) >= minTitles)
                set.profiles.push_back(makeProfile(name + " › " + key.second, {key.second, name}, titles));
    }
    // Où chaque titre est allé, et les artistes de nom (genres de leurs titres).
    std::map<std::string, ReferenceEntry> artists;
    for (const auto& it : items) {
        const bool styled = !it.style.empty() && static_cast<int>(byStyle[{it.family, it.style}].size()) >= minTitles;
        set.assignments.push_back({it.e->artist, it.e->title, it.family, styled ? it.style : std::string()});
        auto& a = artists[artistKey(it.e->artist)];
        if (a.artist.empty()) {
            a.artist = it.e->artist;
            a.measured = false;
            a.aliases = it.e->aliases;
        }
        std::vector<std::string> genres;
        for (const auto& [name, words] : families)
            if (name == it.family) genres = words;
        if (styled) genres.push_back(it.style);
        for (const auto& g : genres)
            if (std::find(a.genres.begin(), a.genres.end(), g) == a.genres.end()) a.genres.push_back(g);
    }
    for (auto& [k, a] : artists) set.artists.push_back(std::move(a));
    return set;
}

std::string serializeGenreProfiles(const GenreProfileSet& set) {
    ReferenceLibrary lib;
    for (const auto& p : set.profiles) lib.add(p);
    for (const auto& a : set.artists) lib.add(a);
    std::string text = lib.serialize();
    const std::string head = "# OMNIS — base de références (signatures mesurées, aucun audio)\n";
    if (text.rfind(head, 0) == 0) text.erase(0, head.size());
    return "# =============================================================================\n"
           "#  Abrasion Labs // OMNIS — Profils de genre livrés (généré : omnis_refdb profils)\n"
           "#\n"
           "#  Seulement des MOYENNES par famille et par sous-genre (« profil = N » titres,\n"
           "#  au moins 3), sans aucune donnée propre à un titre ni aucun nom de titre ;\n"
           "#  puis les artistes connus de nom avec leurs genres (aucun chiffre).\n"
           "#  Ne pas modifier à la main : régénérer depuis data/references.txt.\n"
           "# =============================================================================\n" +
           text;
}

} // namespace omnis::ai
