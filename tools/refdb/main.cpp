// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/refdb — Base de références (signatures, jamais d'audio).
//
//    omnis_refdb add titre.wav --artiste "2hollis" [--titre "poster boy"]
//                [--genres "rage|hyperpop"] [--alias "2 hollis"] --db base.txt
//    omnis_refdb scan dossier/ [--genres "rage"] --db base.txt
//                (WAV / MP3 / FLAC nommés « Artiste - Titre.ext »)
//    omnis_refdb nom --artiste "Yeat" --genres "rage" [--alias "…"] --db base.txt
//    omnis_refdb noms liste.txt --db base.txt
//                (une ligne par artiste : « Artiste ; genre, genre ; alias, alias »)
//    omnis_refdb csv export.csv --classes familles.txt --db base.txt
//                (export de playlists : colonnes « Artist Name(s) », « Track Name »,
//                « Genre_Classement » ; seuls les noms, titres et le classement de la
//                personne sont gardés — jamais les colonnes calculées par Spotify)
//    omnis_refdb titres liste.txt --db base.txt
//                (« Artiste ; Titre ; genre, genre » : importe et signale les
//                désaccords avec le classement des playlists de la personne)
//    omnis_refdb artistes liens.txt --db base.txt
//                (« Artiste ; famille ; styles ; proches : A, B ; traits : … » :
//                les artistes proches servent de référence tant que l'artiste
//                lui-même n'a pas été écouté)
//    omnis_refdb scan dossier/ --titres liste.txt --db base.txt
//                (chaque son est reconnu dans la liste et prend ses genres)
//    omnis_refdb list [--db base.txt]
//    omnis_refdb find "2 holis" [--db base.txt]
// =============================================================================
#include "AudioRead.h"
#include "AiFiles.h"
#include "AudioRead.h"
#include "WavFile.h"

#include "analysis/SpectralSignature.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <thread>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

using namespace omnis;
using namespace omnis::tools;

namespace {

std::vector<std::string> list(const std::string& s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == '|') {
            std::string item = s.substr(start, i - start);
            while (!item.empty() && item.front() == ' ') item.erase(item.begin());
            while (!item.empty() && item.back() == ' ') item.pop_back();
            if (!item.empty()) out.push_back(item);
            start = i + 1;
        }
    return out;
}

/// Liste « a, b » ou « a | b ».
std::vector<std::string> loose(std::string s) {
    for (char& c : s)
        if (c == ',') c = '|';
    return list(s);
}

ai::ReferenceLibrary openDb(const std::string& db) {
    ai::ReferenceLibrary user;
    std::string text;
    if (readTextFile(db, text)) user.parse(text);
    return user;
}

/// `threads` : threads pour CE fichier (1 quand plusieurs fichiers sont mesurés en parallèle).
bool measure(const std::filesystem::path& path, ai::ReferenceEntry& e, std::string& error, unsigned threads = 0) {
    AudioFile f;
    if (!readAudioFile(path, f, error)) return false;
    const auto& r = f.channels[std::min<std::size_t>(1, f.channels.size() - 1)];
    e.signature = analysis::SignatureAccumulator::analyzeParallel(f.channels[0].data(), r.data(), f.numFrames(), f.sampleRate, threads);
    e.signature.music = analysis::analyzeMusic(f.channels[0].data(), r.data(), f.numFrames(), f.sampleRate);
    e.measured = true;
    return true;
}

/// Une ligne CSV (RFC 4180 : guillemets, guillemets doublés, virgules dans un champ).
std::vector<std::string> csvFields(const std::string& line) {
    std::vector<std::string> out(1);
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                out.back() += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                out.back() += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            out.emplace_back();
        } else if (c != '\r') {
            out.back() += c;
        }
    }
    return out;
}

struct TitleLine {
    std::string artist, title;
    std::vector<std::string> genres;
};

/// Écritures d'un même genre ramenées à une seule (« r&b » -> « rnb ») et
/// micro-genres rattachés à leur famille (« digicore » est de l'hyperpop).
std::vector<std::string> canonicalGenres(std::vector<std::string> genres) {
    static const std::pair<const char*, const char*> kAlias[] = {{"r&b", "rnb"}, {"r & b", "rnb"}, {"électronique", "electronique"},
                                                                 {"électro", "electro"}};
    static const std::pair<const char*, const char*> kFamily[] = {{"digicore", "hyperpop"}, {"dariacore", "hyperpop"}};
    for (auto& g : genres) {
        std::string lower;
        for (char c : g) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const auto& [from, to] : kAlias)
            if (lower == from) g = to;
    }
    const std::vector<std::string> base = genres;
    for (const auto& g : base)
        for (const auto& [micro, family] : kFamily)
            if (g == micro && std::find(genres.begin(), genres.end(), family) == genres.end()) genres.emplace_back(family);
    return genres;
}

std::vector<TitleLine> readTitleList(const std::string& path) {
    std::vector<TitleLine> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= line.size(); ++i)
            if (i == line.size() || line[i] == ';') {
                f.push_back(line.substr(start, i - start));
                start = i + 1;
            }
        if (f.size() < 3) continue;
        TitleLine t;
        const auto a = list(f[0]), ti = list(f[1]);
        if (a.empty() || ti.empty()) continue;
        t.artist = a.front();
        t.title = ti.front();
        t.genres = canonicalGenres(loose(f[2]));
        out.push_back(std::move(t));
    }
    return out;
}

/// Ligne de la liste correspondant à un fichier « Artiste - Titre » (fautes
/// tolérées) ; à défaut, un titre exact et unique dans la liste.
const TitleLine* lineForFile(const std::vector<TitleLine>& lines, const std::string& artist, const std::string& title) {
    for (const auto& l : lines)
        if (!artist.empty() && ai::namesMatch(l.artist, artist) && ai::namesMatch(l.title, title)) return &l;
    const TitleLine* only = nullptr;
    for (const auto& l : lines)
        if (ai::namesMatch(l.title, title.empty() ? artist : title)) {
            if (only) return nullptr; // ambigu
            only = &l;
        }
    return only;
}

/// Titre sans « (feat. …) », « (with …) », « [prod. …] » : on cherche le morceau, pas l'invité.
std::string cleanTitle(std::string t) {
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] != '(' && t[i] != '[') continue;
        std::string inside = t.substr(i + 1, 6);
        for (char& c : inside) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (inside.rfind("feat", 0) == 0 || inside.rfind("ft.", 0) == 0 || inside.rfind("with ", 0) == 0 || inside.rfind("prod", 0) == 0) {
            t.erase(i);
            break;
        }
    }
    while (!t.empty() && (t.back() == ' ' || t.back() == '-')) t.pop_back();
    return t;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::puts("Abrasion Labs // OMNIS — base de références\n"
                  "  omnis_refdb add titre.wav --artiste NOM [--titre T] [--genres \"a|b\"] [--alias \"x|y\"] --db base.txt\n"
                  "  omnis_refdb scan dossier/ [--genres \"a|b\"] --db base.txt   (« Artiste - Titre.mp3|flac|wav »)\n"
                  "  omnis_refdb nom --artiste NOM --genres \"a|b\" [--alias \"x|y\"] --db base.txt\n"
                  "  omnis_refdb noms liste.txt --db base.txt   (« Artiste ; genre, genre ; alias »)\n"
                  "  omnis_refdb artistes liens.txt --db base.txt   (« Artiste ; famille ; styles ; proches : A, B ; traits : … »)\n"
                  "  omnis_refdb fiche titre.wav   (la fiche complète d'un son, sans rien écrire)\n"
                  "  omnis_refdb list [--db base.txt]\n"
                  "  omnis_refdb find \"requête\" [--db base.txt]");
        return 1;
    }
    const std::string command = argv[1];
    std::string db, artist, title, genres, aliases, input;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--db" && i + 1 < argc) db = argv[++i];
        else if (a == "--artiste" && i + 1 < argc) artist = argv[++i];
        else if (a == "--titre" && i + 1 < argc) title = argv[++i];
        else if (a == "--genres" && i + 1 < argc) genres = argv[++i];
        else if (a == "--alias" && i + 1 < argc) aliases = argv[++i];
        else if ((a == "--classes" || a == "--titres") && i + 1 < argc) ++i;
        else if (input.empty()) input = a;
    }

    if (command == "fiche") {
        std::string error;
        ai::ReferenceEntry e;
        e.artist = artist.empty() ? std::filesystem::path(input).stem().string() : artist;
        if (input.empty() || !measure(input, e, error)) {
            std::fprintf(stderr, "%s\n", input.empty() ? "fiche : un fichier audio" : error.c_str());
            return 1;
        }
        const auto& m = e.signature.music;
        std::printf("%s (%.0f s)\n", e.artist.c_str(), m.seconds);
        std::printf("  sonie %.1f LUFS, LRA %.1f LU, true peak %.1f dBTP, PLR %.1f dB\n", e.signature.integratedLufs, e.signature.loudnessRangeLu,
                    e.signature.truePeakDb, e.signature.plrDb);
        std::printf("  tempo %.1f BPM · tonalité %s (confiance %.2f) · 808 / grave tenu %s\n", m.bpm, m.keyName().c_str(), m.keyConfidence,
                    analysis::MusicDescriptors::noteName(m.subHz).c_str());
        std::printf("  sections (sonie 3 s) : calme %.1f · médiane %.1f · fort %.1f LUFS\n", m.shortTermLufs[0], m.shortTermLufs[1], m.shortTermLufs[2]);
        std::printf("  punch (crête 10 ms − niveau 400 ms) : grave %.1f · bas-médium %.1f · médium %.1f · aigu %.1f dB\n", m.microDb[0], m.microDb[1],
                    m.microDb[2], m.microDb[3]);
        std::printf("  stéréo : corrélation %.2f, grave %.2f, perte en mono %.1f dB\n", m.correlation, m.lowCorrelation, m.monoLossDb);
        std::printf("  %.1f coups/s · brillance %.0f Hz · crête %.2f dBFS · écrêtage %.2f %%\n", m.onsetsPerSecond, m.centroidHz, m.samplePeakDb, m.clipPercent);
        return 0;
    }
    if (command == "add") {
        if (input.empty() || artist.empty() || db.empty()) {
            std::fputs("add : fichier, --artiste et --db requis\n", stderr);
            return 1;
        }
        std::string error;
        ai::ReferenceEntry e;
        e.artist = artist;
        e.title = title;
        e.genres = list(genres);
        e.aliases = list(aliases);
        if (!measure(input, e, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        auto user = openDb(db);
        user.add(e);
        if (!writeTextFile(db, user.serialize())) {
            std::fprintf(stderr, "écriture impossible : %s\n", db.c_str());
            return 1;
        }
        std::printf("Ajouté : %s  (%.1f LUFS, LRA %.1f LU, sub %+.1f dB / corps) -> %s (%zu entrées)\n", e.displayName().c_str(),
                    e.signature.integratedLufs, e.signature.loudnessRangeLu, e.signature.subEnergyDb - e.signature.bodyDb(), db.c_str(),
                    user.entries().size());
        return 0;
    }
    if (command == "scan") {
        if (input.empty() || db.empty()) return 1;
        std::vector<TitleLine> titleList;
        for (int i = 2; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--titres") titleList = readTitleList(argv[i + 1]);
        auto user = openDb(db);
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(input, ec))
            if (entry.is_regular_file() && isReadableAudio(entry.path())) files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        // Un fichier par cœur : décodage et analyse en parallèle, résultats
        // rangés dans l'ordre des fichiers.
        struct Job {
            ai::ReferenceEntry entry;
            std::string error;
            bool ok = false;
        };
        std::vector<Job> jobs(files.size());
        std::atomic<std::size_t> next{0};
        const unsigned workers = std::max(1u, std::min<unsigned>(std::thread::hardware_concurrency(), static_cast<unsigned>(files.size())));
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<std::thread> pool;
        for (unsigned w = 0; w < workers; ++w)
            pool.emplace_back([&] {
                for (std::size_t i; (i = next.fetch_add(1)) < files.size();) {
                    // « Artiste - Titre » ; sans tiret : le nom de fichier est l'artiste.
                    // En UTF-8, comme le reste de la base (« La Fève », pas la page de code).
                    const std::string stem = utf8Path(files[i].stem());
                    auto& job = jobs[i];
                    const auto dash = stem.find(" - ");
                    job.entry.artist = dash == std::string::npos ? stem : stem.substr(0, dash);
                    job.entry.title = dash == std::string::npos ? std::string() : stem.substr(dash + 3);
                    job.entry.genres = list(genres);
                    // Reconnu dans la liste : noms exacts de la liste et ses genres.
                    if (const TitleLine* l = lineForFile(titleList, job.entry.artist, job.entry.title)) {
                        job.entry.artist = l->artist;
                        job.entry.title = l->title;
                        for (const auto& g : l->genres)
                            if (std::find(job.entry.genres.begin(), job.entry.genres.end(), g) == job.entry.genres.end()) job.entry.genres.push_back(g);
                    }
                    job.ok = measure(files[i], job.entry, job.error, 1);
                }
            });
        for (auto& t : pool) t.join();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        int added = 0;
        for (std::size_t i = 0; i < files.size(); ++i) {
            const auto& job = jobs[i];
            if (!job.ok) {
                std::printf("  ignoré : %s (%s)\n", utf8Path(files[i].filename()).c_str(), job.error.c_str());
                continue;
            }
            std::printf("  %-44s %6.1f LUFS  LRA %4.1f  %5.1f BPM  %s\n", job.entry.displayName().c_str(), job.entry.signature.integratedLufs,
                        job.entry.signature.loudnessRangeLu, job.entry.signature.music.bpm, job.entry.signature.music.keyName().c_str());
            // Déjà dans la base (re-mesure) : on garde ce que la fiche savait déjà
            // (alias, proches, traits, genres), seules les mesures sont refaites.
            ai::ReferenceEntry merged = job.entry;
            for (const auto& old : user.entries())
                if (ai::namesMatch(old.artist, merged.artist) && ai::namesMatch(old.title, merged.title)) {
                    merged.artist = old.artist;
                    merged.title = old.title;
                    if (merged.aliases.empty()) merged.aliases = old.aliases;
                    if (merged.similar.empty()) merged.similar = old.similar;
                    if (merged.traits.empty()) merged.traits = old.traits;
                    for (const auto& g : old.genres)
                        if (std::find(merged.genres.begin(), merged.genres.end(), g) == merged.genres.end()) merged.genres.push_back(g);
                    break;
                }
            user.add(merged);
            ++added;
        }
        std::printf("Mesuré en %.1f s sur %u cœur(s)\n", seconds, workers);
        if (!writeTextFile(db, user.serialize())) return 1;
        std::printf("%d son(s) mesuré(s) -> %s (%zu entrées)\n", added, db.c_str(), user.entries().size());
        return 0;
    }
    if (command == "titres") {
        if (input.empty() || db.empty()) return 1;
        const auto lines = readTitleList(input);
        const auto known = loadLibrary(db); // catalogue livré + base : le classement des playlists
        auto user = openDb(db);
        int disagreements = 0;
        for (const auto& l : lines) {
            ai::ReferenceEntry e;
            e.artist = l.artist;
            e.title = l.title;
            e.genres = l.genres;
            e.measured = false;
            // Le classement des playlists de la personne fait foi : on signale les désaccords.
            const auto m = known.find(l.artist);
            if (m.found() && m.kind == ai::ReferenceMatch::Kind::Artist && !l.genres.empty()) {
                std::vector<std::string> theirs;
                for (const auto* x : m.entries)
                    for (const auto& g : x->genres)
                        if (std::find(theirs.begin(), theirs.end(), g) == theirs.end()) theirs.push_back(g);
                const bool agree = std::any_of(l.genres.begin(), l.genres.end(), [&](const std::string& g) {
                    return std::any_of(theirs.begin(), theirs.end(), [&](const std::string& t) { return ai::namesMatch(g, t); });
                });
                if (!agree && !theirs.empty()) {
                    std::string t;
                    for (const auto& g : theirs) t += (t.empty() ? "" : ", ") + g;
                    std::printf("  désaccord : %s - %s : liste « %s », tes playlists « %s »\n", l.artist.c_str(), l.title.c_str(),
                                l.genres.front().c_str(), t.c_str());
                    ++disagreements;
                }
            }
            user.add(e);
        }
        if (!writeTextFile(db, user.serialize())) return 1;
        std::printf("%zu titre(s) importé(s), %d désaccord(s) avec tes playlists -> %s\n", lines.size(), disagreements, db.c_str());
        return 0;
    }
    if (command == "artistes") {
        if (input.empty() || db.empty()) return 1;
        const auto known = loadLibrary(db);
        auto user = openDb(db);
        std::ifstream in(input);
        std::string line;
        int imported = 0, disagreements = 0, withoutSimilar = 0;
        std::string notFound;
        std::map<std::string, int> unknownSimilar; // proches cités, absents de la base
        while (std::getline(in, line)) {
            ai::ReferenceEntry e;
            if (!ai::parseArtistLink(line, e)) continue;
            e.genres = canonicalGenres(e.genres);
            // « Artiste ; inconnu » : la recherche n'a rien trouvé de sûr (souvent un nom inventé).
            if (e.genres.empty() || ai::namesMatch(e.genres.front(), "inconnu")) {
                notFound += (notFound.empty() ? "" : ", ") + e.artist;
                continue;
            }
            const auto m = known.find(e.artist);
            const bool knownArtist = m.found() && m.exact && m.kind == ai::ReferenceMatch::Kind::Artist;
            // Le classement des playlists de la personne fait foi.
            if (knownArtist && !e.genres.empty()) {
                std::vector<std::string> theirs;
                for (const auto* x : m.entries)
                    for (const auto& g : x->genres)
                        if (std::find(theirs.begin(), theirs.end(), g) == theirs.end()) theirs.push_back(g);
                if (!theirs.empty() && std::none_of(theirs.begin(), theirs.end(), [&](const std::string& t) { return ai::namesMatch(e.genres.front(), t); })) {
                    std::string t;
                    for (const auto& g : theirs) t += (t.empty() ? "" : ", ") + g;
                    std::printf("  désaccord : %s : liste « %s », tes playlists « %s »\n", e.artist.c_str(), e.genres.front().c_str(), t.c_str());
                    ++disagreements;
                }
            }
            if (e.similar.empty()) ++withoutSimilar;
            for (const auto& s : e.similar)
                if (const auto k = known.find(s); !k.found() || k.kind != ai::ReferenceMatch::Kind::Artist) ++unknownSimilar[s];
            if (knownArtist) e.artist = m.entries.front()->artist; // écriture de la base (« La Fève », pas « la feve »)
            user.add(e);
            ++imported;
        }
        if (!writeTextFile(db, user.serialize())) return 1;
        std::printf("%d artiste(s) importé(s), %d sans proches, %d désaccord(s) avec tes playlists -> %s\n", imported, withoutSimilar,
                    disagreements, db.c_str());
        if (!notFound.empty()) std::printf("Introuvables, non importés : %s\n", notFound.c_str());
        if (!unknownSimilar.empty()) {
            std::string names;
            for (const auto& [name, count] : unknownSimilar) names += (names.empty() ? "" : ", ") + name;
            std::printf("%zu proche(s) cité(s) mais absents de la base (ils comptent dès qu'un son d'eux est mesuré) : %s\n",
                        unknownSimilar.size(), names.c_str());
        }
        return 0;
    }
    if (command == "csv") {
        std::string classesPath;
        for (int i = 2; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--classes") classesPath = argv[i + 1];
        if (input.empty() || db.empty()) return 1;
        // Familles -> mots de genre.
        std::vector<std::pair<std::string, std::vector<std::string>>> classes;
        {
            std::ifstream in(classesPath);
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty() || line[0] == '#') continue;
                const auto arrow = line.find("=>");
                if (arrow == std::string::npos) continue;
                const auto names = list(line.substr(0, arrow));
                if (!names.empty()) classes.emplace_back(names.front(), loose(line.substr(arrow + 2)));
            }
        }
        const auto tagsOf = [&](const std::string& family) {
            for (const auto& [name, tags] : classes)
                if (name == family) return tags;
            return family.empty() ? std::vector<std::string>{} : std::vector<std::string>{family};
        };
        std::ifstream in(input, std::ios::binary);
        std::string line;
        std::getline(in, line);
        if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3); // BOM
        const auto header = csvFields(line);
        const auto column = [&](const char* name) {
            for (std::size_t i = 0; i < header.size(); ++i)
                if (header[i] == name) return static_cast<int>(i);
            return -1;
        };
        const int cArtist = column("Artist Name(s)"), cTitle = column("Track Name"), cFamily = column("Genre_Classement");
        if (cArtist < 0 || cTitle < 0) {
            std::fputs("colonnes « Artist Name(s) » / « Track Name » introuvables\n", stderr);
            return 1;
        }
        auto user = openDb(db);
        std::map<std::string, std::map<std::string, int>> artistFamilies;
        int tracks = 0;
        while (std::getline(in, line)) {
            const auto f = csvFields(line);
            if (static_cast<int>(f.size()) <= std::max(cArtist, cTitle)) continue;
            const std::string family = cFamily >= 0 && cFamily < static_cast<int>(f.size()) ? f[static_cast<std::size_t>(cFamily)] : "";
            std::vector<std::string> artists;
            std::size_t start = 0;
            const std::string& names = f[static_cast<std::size_t>(cArtist)];
            for (std::size_t i = 0; i <= names.size(); ++i)
                if (i == names.size() || names[i] == ';') {
                    const auto a = list(names.substr(start, i - start));
                    if (!a.empty()) artists.push_back(a.front());
                    start = i + 1;
                }
            if (artists.empty()) continue;
            ai::ReferenceEntry e;
            e.artist = artists.front();
            e.title = cleanTitle(f[static_cast<std::size_t>(cTitle)]);
            e.genres = tagsOf(family);
            e.measured = false;
            if (!e.title.empty()) user.add(e);
            for (const auto& a : artists) ++artistFamilies[a][family];
            ++tracks;
        }
        // Un artiste reçoit les familles qui font au moins 20 % de ses titres.
        for (const auto& [name, families] : artistFamilies) {
            int total = 0;
            for (const auto& [fam, n] : families) total += n;
            ai::ReferenceEntry e;
            e.artist = name;
            e.measured = false;
            for (const auto& [fam, n] : families)
                if (n * 5 >= total)
                    for (const auto& t : tagsOf(fam))
                        if (std::find(e.genres.begin(), e.genres.end(), t) == e.genres.end()) e.genres.push_back(t);
            user.add(e);
        }
        if (!writeTextFile(db, user.serialize())) return 1;
        std::printf("%d titre(s), %zu artiste(s) -> %s (%zu entrées)\n", tracks, artistFamilies.size(), db.c_str(), user.entries().size());
        return 0;
    }
    if (command == "nom" || command == "noms") {
        if (db.empty()) return 1;
        auto user = openDb(db);
        std::vector<ai::ReferenceEntry> entries;
        if (command == "nom") {
            if (artist.empty()) return 1;
            ai::ReferenceEntry e;
            e.artist = artist;
            e.genres = list(genres);
            e.aliases = list(aliases);
            e.measured = false;
            entries.push_back(e);
        } else {
            std::ifstream in(input);
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty() || line[0] == '#') continue;
                std::vector<std::string> fields;
                std::size_t start = 0;
                for (std::size_t i = 0; i <= line.size(); ++i)
                    if (i == line.size() || line[i] == ';') {
                        fields.push_back(line.substr(start, i - start));
                        start = i + 1;
                    }
                ai::ReferenceEntry e;
                e.artist = list(fields[0]).empty() ? std::string() : list(fields[0]).front();
                if (e.artist.empty()) continue;
                if (fields.size() > 1) e.genres = loose(fields[1]);
                if (fields.size() > 2) e.aliases = loose(fields[2]);
                e.measured = false;
                entries.push_back(e);
            }
        }
        for (auto& e : entries) {
            std::string g;
            for (const auto& x : e.genres) g += (g.empty() ? "" : ", ") + x;
            std::printf("  %-30s [%s]\n", e.artist.c_str(), g.c_str());
            user.add(e);
        }
        if (!writeTextFile(db, user.serialize())) return 1;
        std::printf("%zu artiste(s) connu(s) de nom -> %s\n", entries.size(), db.c_str());
        return 0;
    }
    const auto library = loadLibrary(db);
    if (command == "list") {
        for (const auto& e : library.entries()) {
            std::string g;
            for (const auto& x : e.genres) g += (g.empty() ? "" : ", ") + x;
            if (e.measured)
                std::printf("  %-36s %6.1f LUFS  LRA %4.1f  [%s]\n", e.displayName().c_str(), e.signature.integratedLufs,
                            e.signature.loudnessRangeLu, g.c_str());
            else
                std::printf("  %-36s %-22s [%s]\n", e.displayName().c_str(), "(de nom, sans son)", g.c_str());
        }
        std::printf("%zu référence(s)\n", library.entries().size());
        return 0;
    }
    if (command == "find") {
        const auto m = library.find(input);
        if (!m.found()) {
            std::printf("« %s » : inconnu\n", input.c_str());
            return 2;
        }
        std::printf("« %s » -> %s\n", input.c_str(), m.how.c_str());
        for (const auto* e : m.entries) std::printf("  %s\n", e->displayName().c_str());
        return 0;
    }
    std::fprintf(stderr, "commande inconnue : %s\n", command.c_str());
    return 1;
}
