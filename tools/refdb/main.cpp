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
//    omnis_refdb list [--db base.txt]
//    omnis_refdb find "2 holis" [--db base.txt]
// =============================================================================
#include "AudioRead.h"
#include "AiFiles.h"
#include "AudioRead.h"
#include "WavFile.h"

#include "analysis/SpectralSignature.h"

#include <algorithm>
#include <cctype>
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

bool measure(const std::string& path, ai::ReferenceEntry& e, std::string& error) {
    AudioFile f;
    if (!readAudioFile(path, f, error)) return false;
    const auto& r = f.channels[std::min<std::size_t>(1, f.channels.size() - 1)];
    e.signature = analysis::analyzeSignature(f.channels[0].data(), r.data(), f.numFrames(), f.sampleRate);
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
        else if (a == "--classes" && i + 1 < argc) ++i;
        else if (input.empty()) input = a;
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
        auto user = openDb(db);
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(input, ec))
            if (entry.is_regular_file() && isReadableAudio(entry.path().string())) files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        int added = 0;
        for (const auto& path : files) {
            // « Artiste - Titre » ; sans tiret : le nom de fichier est l'artiste.
            const std::string stem = path.stem().string();
            ai::ReferenceEntry e;
            const auto dash = stem.find(" - ");
            e.artist = dash == std::string::npos ? stem : stem.substr(0, dash);
            e.title = dash == std::string::npos ? std::string() : stem.substr(dash + 3);
            e.genres = list(genres);
            std::string error;
            if (!measure(path.string(), e, error)) {
                std::printf("  ignoré : %s (%s)\n", path.filename().string().c_str(), error.c_str());
                continue;
            }
            std::printf("  %-44s %6.1f LUFS  LRA %4.1f\n", e.displayName().c_str(), e.signature.integratedLufs, e.signature.loudnessRangeLu);
            user.add(e);
            ++added;
        }
        if (!writeTextFile(db, user.serialize())) return 1;
        std::printf("%d son(s) mesuré(s) -> %s (%zu entrées)\n", added, db.c_str(), user.entries().size());
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
