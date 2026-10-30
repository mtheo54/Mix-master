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
//    omnis_refdb image photo.jpg --artiste "2hollis" --credit "Photo : X, CC BY-SA 4.0" --db base.txt
//                (photo d'artiste SOUS LICENCE LIBRE : le crédit est obligatoire et
//                montré dans la console ; copiée dans le dossier images/ d'OMNIS)
//    omnis_refdb pochette pochette.jpg --artiste "2hollis" --titre "poster boy" --db base.txt
//                (pochette du titre : droits du label, pour ton usage ; à ne pas
//                livrer avec le plug-in sans leur accord)
//    omnis_refdb deezer --artiste "La Fève" [--titre T] --db base.txt
//                (API publique de Deezer, sans clé : photo de l'artiste, pochette
//                et lien de chaque titre de la base, illustrés en mémoire (aucune image d'origine gardée) ;
//                usage personnel, à ne pas livrer avec le plug-in vendu)
//    omnis_refdb discographie playlist.csv [--artistes "euhZ|Gry=BRR"] [--retirer "GRY=Matilda"]
//                [--proches 1] [--sortie dossier]
//                (tous les titres de chaque artiste de la playlist et de --artistes,
//                puis de leurs proches sur Deezer : artistes.tsv, titres.tsv,
//                introuvables.txt ; reprend où il s'était arrêté grâce au cache)
//    omnis_refdb cartoon photo.jpg [--sortie illustration.png]   |   omnis_refdb cartoon --db base.txt
//                (illustration peinte OMNIS, gfx/OmnisAvatarStylizer ; --db : anciennes
//                images de images/ -> cache/avatars et cache/covers)
//    omnis_refdb list [--db base.txt]
//    omnis_refdb find "2 holis" [--db base.txt]
// =============================================================================
#include "AudioRead.h"
#include "AiFiles.h"
#include "Deezer.h"
#include "AudioRead.h"
#include "WavFile.h"

#include "analysis/SpectralSignature.h"
#include "core/UserDataDir.h"
#include "gfx/AvatarCache.h"
#include "gfx/OmnisAvatarStylizer.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <thread>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
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

/// Nom de fichier stable, sans espaces ni accents : « La Fève » -> « la_feve ».
std::string slug(const std::string& text) {
    // Lettres accentuées du latin (UTF-8 sur deux octets, C3 xx) -> sans accent.
    static const char* const kLatin = "AAAAAAACEEEEIIIIDNOOOOO OUUUUYTsaaaaaaaceeeeiiiidnooooo ouuuuyty";
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        if (c == 0xC3 && i + 1 < text.size()) {
            const auto n = static_cast<unsigned char>(text[i + 1]);
            if (n >= 0x80 && n <= 0xBF) {
                c = static_cast<unsigned char>(kLatin[n - 0x80]);
                ++i;
            }
        }
        if (c < 0x80 && std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? std::string("image") : out;
}

bool readBinaryFile(const std::string& path, std::string& out) {
    std::ifstream in(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

/// Octets d'une image -> illustration OMNIS au chemin donné (cache).
bool illustrate(const std::string& bytes, const std::filesystem::path& target, std::string& error) {
    gfx::RgbaImage art;
    if (!gfx::OmnisAvatarStylizer().stylize(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), art, &error)) return false;
    return gfx::OmnisAvatarStylizer::savePng(target, art, &error);
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
                  "  omnis_refdb image photo.jpg --artiste NOM --credit \"Photo : X, CC BY 4.0\" --db base.txt\n"
                  "  omnis_refdb pochette pochette.jpg --artiste NOM --titre T [--credit C] [--lien URL] --db base.txt\n"
                  "  omnis_refdb deezer --artiste NOM [--titre T] --db base.txt   (photo, pochettes et liens depuis Deezer)\n"
                  "  omnis_refdb cartoon image.jpg [--sortie illustration.png] | cartoon --db base.txt   (illustrations OMNIS)\n"
                  "  omnis_refdb discographie playlist.csv [--artistes \"a|b\"] [--proches 1] [--sortie dossier]   (tous les titres, et ceux des proches)\n"
                  "  omnis_refdb list [--db base.txt]\n"
                  "  omnis_refdb find \"requête\" [--db base.txt]");
        return 1;
    }
    const std::string command = argv[1];
    std::string db, artist, title, genres, aliases, input, credit, link;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--db" && i + 1 < argc) db = argv[++i];
        else if (a == "--artiste" && i + 1 < argc) artist = argv[++i];
        else if (a == "--titre" && i + 1 < argc) title = argv[++i];
        else if (a == "--genres" && i + 1 < argc) genres = argv[++i];
        else if (a == "--alias" && i + 1 < argc) aliases = argv[++i];
        else if (a == "--credit" && i + 1 < argc) credit = argv[++i];
        else if (a == "--lien" && i + 1 < argc) link = argv[++i];
        else if ((a == "--classes" || a == "--titres" || a == "--artistes" || a == "--sortie" || a == "--cache" || a == "--proches" || a == "--retirer") && i + 1 < argc) ++i;
        else if (input.empty()) input = a;
    }

    if (command == "image" || command == "pochette") {
        // Une image fournie (photo libre, pochette) devient une illustration dans le
        // cache ; l'image d'origine n'est pas copiée.
        const bool photo = command == "image";
        if (db.empty() || input.empty() || artist.empty() || (!photo && title.empty())) {
            std::fprintf(stderr, "%s : fichier, --artiste%s et --db\n", command.c_str(), photo ? " --credit" : " --titre");
            return 1;
        }
        if (photo && credit.empty()) {
            std::fprintf(stderr, "image : le crédit est obligatoire (« Photo : Auteur, CC BY-SA 4.0 ») : une photo se montre avec son auteur et sa licence\n");
            return 1;
        }
        auto user = openDb(db);
        std::vector<ai::ReferenceEntry> touched;
        for (const auto& e : user.entries())
            if (ai::namesMatch(e.artist, artist) && (photo || ai::namesMatch(e.title, title))) touched.push_back(e);
        if (touched.empty()) {
            std::fprintf(stderr, "%s : « %s%s » n'est pas dans %s\n", command.c_str(), artist.c_str(), photo ? "" : (" - " + title).c_str(), db.c_str());
            return 1;
        }
        std::string bytes, error;
        if (!readBinaryFile(input, bytes)) {
            std::fprintf(stderr, "%s : fichier illisible : %s\n", command.c_str(), input.c_str());
            return 1;
        }
        const auto target = photo ? gfx::avatars::artistPath(touched.front().artist) : gfx::avatars::coverPath(touched.front().artist, touched.front().title);
        if (!illustrate(bytes, target, error)) {
            std::fprintf(stderr, "%s : %s\n", command.c_str(), error.c_str());
            return 1;
        }
        for (auto e : touched) {
            if (photo) e.image.clear(), e.imageCredit = credit + " (illustration OMNIS)";
            else e.cover.clear(), e.coverCredit = (credit.empty() ? std::string("Pochette") : credit) + " (illustration OMNIS)";
            if (!link.empty()) e.link = link;
            user.add(e);
        }
        if (!writeTextFile(db, user.serialize())) return 1;
        std::printf("%s -> %s (%zu fiche(s))\n", command.c_str(), utf8Path(target).c_str(), touched.size());
        return 0;
    }

    if (command == "deezer") {
        if (db.empty() || artist.empty()) {
            std::fprintf(stderr, "deezer : --artiste NOM [--titre T] --db base.txt\n");
            return 1;
        }
        namespace dz = tools::deezer;
        dz::Fetcher net(utf8Path(std::filesystem::temp_directory_path() / "omnis_deezer"));
        net.keep = false; // recherches : toujours fraîches
        const auto get = [&](const std::string& url, std::string& text) {
            std::fflush(stdout);
            text = net.get(url);
            if (text.empty()) std::fprintf(stderr, "  pas de réponse de Deezer (internet ? curl ?) : %s\n", url.c_str());
            return !text.empty();
        };
        std::string text;
        if (!get(dz::artistSearchUrl(artist), text)) return 1;
        const auto artists = dz::parseArtists(text);
        const auto* found = dz::pickArtist(artists, artist);
        if (!found) {
            std::fprintf(stderr, "deezer : « %s » introuvable. Proposés :", artist.c_str());
            for (std::size_t i = 0; i < std::min<std::size_t>(5, artists.size()); ++i) std::fprintf(stderr, " %s ;", artists[i].name.c_str());
            std::fprintf(stderr, "\n");
            return 1;
        }
        std::printf("Deezer : %s (%lld fans) %s\n", found->name.c_str(), found->fans, found->link.c_str());
        // L'image téléchargée reste en mémoire : seule son illustration est écrite.
        std::map<std::string, std::string> fetched; // adresse -> octets (une pochette sert à tout l'album)
        const auto illustrateUrl = [&](const std::string& url, const std::filesystem::path& target) {
            std::string error;
            if (url.empty()) return false;
            std::error_code ec;
            if (std::filesystem::exists(target, ec)) return true; // déjà illustrée
            std::fflush(stdout);
            auto it = fetched.find(url);
            if (it == fetched.end()) it = fetched.emplace(url, net.fetchBytes(url, error)).first;
            if (it->second.empty() || !illustrate(it->second, target, error)) {
                std::fprintf(stderr, "  %s\n", error.c_str());
                return false;
            }
            return true;
        };
        auto user = openDb(db);
        std::vector<ai::ReferenceEntry> touched;
        for (const auto& e : user.entries())
            if (ai::namesMatch(e.artist, artist) && (title.empty() || ai::namesMatch(e.title, title))) touched.push_back(e);
        if (touched.empty()) {
            // Pas encore dans la base : une fiche de nom, qui gardera crédits et lien
            // quand le son sera mesuré (scan, ou écoute dans FL).
            ai::ReferenceEntry e;
            e.artist = found->name;
            e.title = title;
            e.genres = list(genres);
            e.measured = false;
            touched.push_back(e);
        }
        const bool hasPhoto = illustrateUrl(found->picture, gfx::avatars::artistPath(found->name));
        if (hasPhoto) std::printf("  avatar -> %s\n", utf8Path(gfx::avatars::artistPath(found->name)).c_str());
        int covers = 0;
        for (auto& e : touched) {
            e.artist = e.artist.empty() ? found->name : e.artist;
            if (hasPhoto) e.image.clear(), e.imageCredit = "Illustration OMNIS d'après Deezer";
            if (e.title.empty()) {
                if (e.link.empty()) e.link = found->link;
                continue;
            }
            std::vector<dz::Track> tracks;
            const dz::Track* t = nullptr;
            for (const auto& url : dz::trackSearchUrls(found->name, e.title)) {
                if (!get(url, text)) break;
                tracks = dz::parseTracks(text);
                if ((t = dz::pickTrack(tracks, found->name, e.title)) != nullptr) break;
            }
            if (!t) {
                std::printf("  %-40s pas trouvé sur Deezer\n", e.title.c_str());
                continue;
            }
            if (illustrateUrl(t->cover, gfx::avatars::coverPath(e.artist, e.title))) {
                e.cover.clear();
                e.coverCredit = "Illustration OMNIS d'après la pochette (Deezer)";
                ++covers;
            }
            e.link = t->link;
            std::printf("  %-40s album « %s » %s\n", e.title.c_str(), t->album.c_str(), t->link.c_str());
        }
        for (auto& e : touched) user.add(e);
        if (!writeTextFile(db, user.serialize())) return 1;
        std::printf("%zu fiche(s) complétée(s), %d pochette(s) illustrée(s) -> %s\n", touched.size(), covers, utf8Path(gfx::avatars::root()).c_str());
        std::printf("Aucune image d'origine n'est gardée. Les illustrations restent tirées de Deezer : pour la version vendue, accord des artistes / labels.\n");
        return 0;
    }

    if (command == "cartoon") {
        // Une image : omnis_refdb cartoon photo.jpg [--sortie illustration.png]
        // Toute la base : omnis_refdb cartoon --db base.txt (anciennes images du
        // dossier images/ -> illustrations du cache ; les fiches ne les citent plus).
        std::string outFile;
        for (int i = 2; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--sortie") outFile = argv[i + 1];
        std::string bytes, error;
        if (!input.empty()) {
            if (outFile.empty()) outFile = utf8Path(std::filesystem::path(std::u8string(input.begin(), input.end())).replace_extension()) + "_illustration.png";
            if (!readBinaryFile(input, bytes) || !illustrate(bytes, std::filesystem::path(std::u8string(outFile.begin(), outFile.end())), error)) {
                std::fprintf(stderr, "cartoon : %s\n", error.empty() ? ("fichier illisible : " + input).c_str() : error.c_str());
                return 1;
            }
            std::printf("illustration -> %s\n", outFile.c_str());
            return 0;
        }
        if (db.empty()) {
            std::fputs("cartoon : une image, ou --db base.txt pour les anciennes images de la base\n", stderr);
            return 1;
        }
        const auto dir = userDataDir() / "images";
        auto user = openDb(db);
        int done = 0;
        std::vector<ai::ReferenceEntry> updated;
        for (const auto& e : user.entries()) {
            auto copy = e;
            const auto migrate = [&](std::string& file, std::string& creditText, const std::filesystem::path& target) {
                if (file.empty()) return;
                const auto source = dir / std::filesystem::path(std::u8string(file.begin(), file.end()));
                std::error_code ec;
                if (!std::filesystem::exists(target, ec) && (!readBinaryFile(utf8Path(source), bytes) || !illustrate(bytes, target, error))) {
                    std::fprintf(stderr, "  %s : %s\n", file.c_str(), error.empty() ? "illisible" : error.c_str());
                    return;
                }
                std::printf("  %s -> %s\n", file.c_str(), utf8Path(target).c_str());
                file.clear();
                // Ancienne mention de la version cartoon : remplacée.
                for (const std::string old : {" (redessinée en cartoon)", " (redessinee en cartoon)"})
                    for (auto at = creditText.find(old); at != std::string::npos; at = creditText.find(old)) creditText.erase(at, old.size());
                if (creditText.find("illustration OMNIS") == std::string::npos) creditText += " (illustration OMNIS)";
                ++done;
            };
            migrate(copy.image, copy.imageCredit, gfx::avatars::artistPath(e.artist));
            if (!e.title.empty()) migrate(copy.cover, copy.coverCredit, gfx::avatars::coverPath(e.artist, e.title));
            if (copy.image != e.image || copy.cover != e.cover) updated.push_back(copy);
        }
        for (auto& e : updated) user.add(e);
        if (!writeTextFile(db, user.serialize())) return 1;
        std::printf("%d illustration(s) ; les fiches ne citent plus les images d'origine de %s (tu peux les supprimer)\n", done, utf8Path(dir).c_str());
        return 0;
    }

    if (command == "discographie") {
        // Les artistes : ceux d'une playlist (export CSV, colonne « Artist Name(s) »,
        // seuls les noms sont lus), ceux de --artistes, puis leurs proches (Deezer).
        std::string wanted, outDir = "discographies", cacheDir, removeList;
        int depth = 1;
        for (int i = 2; i + 1 < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--artistes") wanted = argv[i + 1];
            else if (a == "--retirer") removeList = argv[i + 1];
            else if (a == "--sortie") outDir = argv[i + 1];
            else if (a == "--cache") cacheDir = argv[i + 1];
            else if (a == "--proches") depth = std::clamp(std::atoi(argv[i + 1]), 0, 3);
        }
        std::vector<std::pair<std::string, std::string>> seeds; // nom, origine
        const auto addSeed = [&](const std::string& name, const std::string& origin) {
            if (name.empty()) return;
            for (const auto& [n, o] : seeds)
                if (ai::namesMatch(n, name)) return;
            seeds.emplace_back(name, origin);
        };
        if (!input.empty()) {
            std::ifstream in(std::filesystem::path(std::u8string(input.begin(), input.end())), std::ios::binary);
            std::string line;
            std::getline(in, line);
            if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
            const auto header = csvFields(line);
            int cArtist = -1;
            for (std::size_t i = 0; i < header.size(); ++i)
                if (header[i] == "Artist Name(s)") cArtist = static_cast<int>(i);
            if (cArtist < 0) {
                std::fputs("discographie : colonne « Artist Name(s) » introuvable\n", stderr);
                return 1;
            }
            while (std::getline(in, line)) {
                const auto f = csvFields(line);
                if (static_cast<int>(f.size()) <= cArtist) continue;
                // Plusieurs artistes séparés par « ; » (« Tyler, The Creator » garde sa virgule).
                std::size_t start = 0;
                const std::string& cell = f[static_cast<std::size_t>(cArtist)];
                for (std::size_t i = 0; i <= cell.size(); ++i)
                    if (i == cell.size() || cell[i] == ';') {
                        std::string n = cell.substr(start, i - start);
                        while (!n.empty() && n.front() == ' ') n.erase(0, 1);
                        while (!n.empty() && n.back() == ' ') n.pop_back();
                        addSeed(n, "playlist");
                        start = i + 1;
                    }
            }
        }
        // « Nom=titre connu » : le titre désigne la bonne fiche parmi les homonymes.
        std::map<std::string, std::string> hints;
        // « Artiste=titre » qui n'est pas de lui (fiche Deezer mêlée) : retiré.
        std::set<std::string> removals;
        for (const auto& r : list(removeList))
            if (const auto eq = r.find('='); eq != std::string::npos) removals.insert(slug(r.substr(0, eq)) + "|" + slug(r.substr(eq + 1)));
        for (const auto& n : list(wanted)) {
            const auto eq = n.find('=');
            const std::string name = n.substr(0, eq);
            if (eq != std::string::npos) hints[name] = n.substr(eq + 1);
            addSeed(name, "demandé");
        }
        if (seeds.empty()) {
            std::fputs("discographie : playlist.csv et/ou --artistes \"a|b\" [--proches 1] [--sortie dossier]\n", stderr);
            return 1;
        }
        namespace dz = tools::deezer;
        namespace fs = std::filesystem;
        if (cacheDir.empty()) cacheDir = utf8Path(fs::temp_directory_path() / "omnis_deezer");
        dz::Fetcher net(cacheDir);
        const fs::path out(std::u8string(outDir.begin(), outDir.end()));
        std::error_code ec;
        fs::create_directories(out, ec);
        std::ofstream artistsOut(out / "artistes.tsv", std::ios::binary | std::ios::trunc);
        std::ofstream tracksOut(out / "titres.tsv", std::ios::binary | std::ios::trunc);
        std::ofstream missOut(out / "introuvables.txt", std::ios::binary | std::ios::trunc);
        const auto cell = [](std::string v) {
            for (char& c : v)
                if (c == '\t' || c == '\n' || c == '\r') c = ' ';
            return v;
        };
        artistsOut << "deezer_id\tartiste\tfans\tsorties\ttitres\torigine\tlien\n";
        tracksOut << "artiste\ttitre\tinterprète\tsortie\ttype\tdate\tdurée_s\tisrc\tlien\n";
        std::printf("%zu artiste(s) de départ ; cache %s\n", seeds.size(), cacheDir.c_str());

        // 1. Les noms -> fiches Deezer (le plus suivi des homonymes ; les autres signalés).
        struct Who {
            dz::Artist a;
            std::string origin;
            std::string hintAlbum; ///< sortie du titre connu (« Gry=BRR » -> BRIBES)
            int hops = 0;          ///< 1 : collabore avec un artiste de départ ; 2 : avec un collaborateur
        };
        std::vector<Who> todo;
        std::map<long long, std::size_t> seen;
        // Un artiste par les crédits d'un titre : ceux que la recherche d'artistes
        // ne remonte pas (« Omyll », crédité sur MALADEUX de mïrï), ou le bon homonyme.
        std::string creditAlbum; // sortie du titre trouvé par byCredits
        const auto byCredits = [&](const std::string& name, const std::string& known) -> dz::Artist {
            const auto found = dz::parseTracks(net.get("https://api.deezer.com/search/track?q=" + dz::urlEncode(name + (known.empty() ? "" : " " + known)) + "&limit=25"));
            std::vector<std::string> urls;
            for (const auto& t : found)
                if (urls.size() < 10 && (known.empty() || ai::namesMatch(t.title, known) || ai::namesMatch(dz::bareTitle(t.title), known)))
                    urls.push_back("https://api.deezer.com/track/" + std::to_string(t.id));
            for (const auto& text : net.getAll(urls)) {
                const auto detail = dz::parseTrackDetail(text);
                for (const auto& c : detail.contributors)
                    if (dz::sameArtist(c.name, name)) {
                        creditAlbum = detail.track.album;
                        const auto full = dz::parseArtist(net.get("https://api.deezer.com/artist/" + std::to_string(c.id)));
                        return full.id != 0 ? full : c;
                    }
            }
            return {};
        };
        {
            std::vector<std::string> urls;
            for (const auto& [n, o] : seeds) urls.push_back(dz::artistSearchUrl(n));
            const auto answers = net.getAll(urls);
            for (std::size_t i = 0; i < seeds.size(); ++i) {
                const auto& [name, origin] = seeds[i];
                const auto found = dz::parseArtists(answers[i]);
                if (const auto hint = hints.find(name); hint != hints.end()) {
                    const auto a = byCredits(name, hint->second);
                    if (a.id != 0) {
                        if (seen.emplace(a.id, todo.size()).second) todo.push_back({a, origin + " (" + hint->second + ")", creditAlbum});
                        continue;
                    }
                    missOut << name << "\t« " << hint->second << " » introuvable sous ce nom : recherche par le nom seul\n";
                }
                const auto* a = dz::pickArtist(found, name);
                if (!a) {
                    const auto c = byCredits(name, {});
                    if (c.id != 0) {
                        if (seen.emplace(c.id, todo.size()).second) todo.push_back({c, origin + ", trouvé par ses crédits", {}});
                        continue;
                    }
                    missOut << name << "\tpas sur Deezer (ou nom différent)";
                    for (std::size_t k = 0; k < std::min<std::size_t>(3, found.size()); ++k) missOut << (k ? ", " : " ; proposés : ") << found[k].name;
                    missOut << "\n";
                    continue;
                }
                if (seen.emplace(a->id, todo.size()).second) todo.push_back({*a, origin, {}});
                // Homonymes : pour un petit artiste, Deezer coupe souvent son œuvre en
                // plusieurs fiches (ou la mêle à d'autres) : toutes gardées, à vérifier
                // (« Nom=titre connu » dans --artistes tranche). Pour un artiste connu,
                // les homonymes sont d'autres gens : écartés.
                for (const auto& other : found) {
                    if (other.id == a->id || !dz::sameArtist(other.name, name)) continue;
                    // Gardés seulement pour un nom donné à la main (« demandé ») : ceux d'une
                    // playlist sont presque toujours d'autres gens (« Azure », « gio »…).
                    const bool small = a->fans < 5000 && origin == "demandé";
                    missOut << name << (small ? "\thomonyme gardé (à vérifier ; « " + name + "=titre » pour choisir) : " : "\thomonyme écarté : ")
                            << other.name << " (" << other.fans << " fans) " << other.link << "\n";
                    if (small && seen.emplace(other.id, todo.size()).second) todo.push_back({other, origin + ", homonyme à vérifier", {}});
                }
            }
        }
        const std::size_t seedCount = todo.size();
        // 2. Les proches (Deezer : 20 par artiste), sur `depth` niveaux.
        std::size_t levelStart = 0;
        for (int level = 0; level < depth; ++level) {
            const std::size_t levelEnd = todo.size();
            std::vector<std::string> urls;
            // Un artiste désigné par un titre (« Gry=BRR ») a une fiche ambiguë : ses
            // « proches » Deezer sont ceux de l'homonyme. Sa scène vient de ses collaborations.
            for (std::size_t i = levelStart; i < levelEnd; ++i)
                urls.push_back(todo[i].hintAlbum.empty() ? "https://api.deezer.com/artist/" + std::to_string(todo[i].a.id) + "/related?limit=50" : std::string());
            const auto answers = net.getAll(urls);
            for (std::size_t i = levelStart; i < levelEnd; ++i)
                for (const auto& r : dz::parseArtistList(answers[i - levelStart]))
                    if (seen.emplace(r.id, todo.size()).second) todo.push_back({r, "proche de " + todo[i].a.name, {}});
            levelStart = levelEnd;
        }
        std::printf("%zu artiste(s) avec les proches\n", todo.size());

        // 3. Toutes les sorties de chaque artiste, puis tous leurs titres.
        long long totalTracks = 0;
        for (std::size_t n = 0; n < todo.size(); ++n) {
            const Who who = todo[n]; // copie : la liste grandit en route (collaborateurs)
            std::vector<dz::Album> albums;
            for (std::string url = "https://api.deezer.com/artist/" + std::to_string(who.a.id) + "/albums?limit=100"; !url.empty();) {
                dz::Page page;
                const auto part = dz::parseAlbums(net.get(url), &page);
                albums.insert(albums.end(), part.begin(), part.end());
                url = dz::safeUrl(page.next) ? page.next : std::string();
            }
            // Une fiche Deezer qui mêle deux homonymes (GRy : un DJ et un rappeur) : le
            // titre connu dit lequel ; ses sorties d'un autre genre sont mises de côté.
            // Seulement une fiche assez grosse où le genre du titre connu est minoritaire :
            // un petit artiste qui change de genre garde tout.
            int keepGenre = 0;
            if (!who.hintAlbum.empty() && albums.size() >= 8) {
                int genre = 0;
                for (const auto& al : albums)
                    if (ai::namesMatch(al.title, who.hintAlbum)) genre = al.genre;
                const auto sameCount = std::count_if(albums.begin(), albums.end(), [&](const auto& al) { return al.genre == genre; });
                if (genre > 0 && static_cast<double>(sameCount) < 0.7 * static_cast<double>(albums.size())) keepGenre = genre;
            }
            if (keepGenre > 0) {
                const int genre = keepGenre;
                {
                    std::vector<dz::Album> same;
                    for (const auto& al : albums) {
                        if (al.genre == genre) same.push_back(al);
                        else missOut << who.a.name << "\tsortie d'un autre genre écartée (autre artiste du même nom ?) : " << al.title << " ("
                                     << al.date << ") " << al.link << "\n";
                    }
                    albums.swap(same);
                }
            }
            // Du plus ancien au plus récent : un titre repris (single puis album) garde sa première sortie.
            std::stable_sort(albums.begin(), albums.end(), [](const auto& x, const auto& y) { return x.date < y.date; });
            std::vector<std::string> urls;
            for (const auto& al : albums) urls.push_back("https://api.deezer.com/album/" + std::to_string(al.id) + "/tracks?limit=200");
            const auto answers = net.getAll(urls);
            std::map<std::string, bool> kept; // titre normalisé
            int count = 0;
            const auto removed = [&](const std::string& name) {
                if (!removals.count(slug(who.a.name) + "|" + slug(name))) return false;
                missOut << who.a.name << "\tretiré (pas de lui) : " << name << "\n";
                return true;
            };
            // Petit artiste de départ, ou collaborateur proche : on suit ses collaborations.
            // Seulement autour des artistes donnés à la main (une playlist entière ferait
            // des milliers d'artistes : ses proches Deezer suffisent).
            const bool scene = who.a.fans < 5000 && ((n < seedCount && who.origin.rfind("demandé", 0) == 0) || who.hops > 0) && who.hops < 2;
            std::vector<long long> ownTracks;
            for (std::size_t k = 0; k < albums.size(); ++k) {
                for (const auto& t : dz::parseTracks(answers[k])) {
                    if (removed(t.title) || !kept.emplace(slug(t.title), true).second) continue;
                    if (scene) ownTracks.push_back(t.id);
                    tracksOut << cell(who.a.name) << '\t' << cell(t.title) << '\t' << cell(t.artist) << '\t' << cell(albums[k].title) << '\t'
                              << albums[k].type << '\t' << albums[k].date << '\t' << t.durationS << '\t' << t.isrc << '\t' << t.link << '\n';
                    ++count;
                }
            }
            // Les featurings d'un petit artiste de départ : ses titres sur les sorties
            // des autres (Deezer ne les range pas dans sa discographie).
            std::vector<long long> partners;
            if (scene) {
                const auto found = dz::parseTracks(net.get("https://api.deezer.com/search/track?q=" + dz::urlEncode(who.a.name) + "&limit=50"));
                std::vector<std::string> details;
                for (const auto& t : found)
                    if (t.artistId != who.a.id) details.push_back("https://api.deezer.com/track/" + std::to_string(t.id));
                std::vector<dz::TrackDetail> feats;
                for (const auto& text : net.getAll(details)) {
                    auto d = dz::parseTrackDetail(text);
                    if (std::any_of(d.contributors.begin(), d.contributors.end(), [&](const auto& c) { return c.id == who.a.id; })) feats.push_back(std::move(d));
                }
                // Fiche mêlée : les featurings aussi doivent être du bon genre.
                std::vector<int> featGenres(feats.size(), keepGenre);
                if (keepGenre > 0) {
                    std::vector<std::string> albumsUrls;
                    for (const auto& d : feats) albumsUrls.push_back("https://api.deezer.com/album/" + std::to_string(d.albumId));
                    const auto answers2 = net.getAll(albumsUrls);
                    for (std::size_t k = 0; k < feats.size(); ++k) featGenres[k] = dz::parseAlbumGenre(answers2[k]);
                }
                for (std::size_t k = 0; k < feats.size(); ++k) {
                    const auto& d = feats[k];
                    if (featGenres[k] == keepGenre && !removed(d.track.title))
                        for (const auto& c : d.contributors) partners.push_back(c.id);
                    if (featGenres[k] != keepGenre || removed(d.track.title)) {
                        missOut << who.a.name << "\tfeaturing d'un autre genre écarté : " << d.track.title << " (" << d.track.artist << ") " << d.track.link << "\n";
                        continue;
                    }
                    if (!kept.emplace(slug(d.track.title), true).second) continue;
                    tracksOut << cell(who.a.name) << '\t' << cell(d.track.title) << '\t' << cell(d.track.artist) << '\t' << cell(d.track.album) << '\t'
                              << "featuring" << '\t' << d.albumDate << '\t' << d.track.durationS << '\t' << d.track.isrc << '\t' << d.track.link << '\n';
                    ++count;
                }
            }
            // Sa scène : les artistes crédités sur ses titres (et ceux de ses featurings),
            // puis leurs proches Deezer ; les collaborateurs suivent à leur tour (2 pas).
            if (scene) {
                std::vector<std::string> trackUrls;
                for (const auto id : ownTracks) trackUrls.push_back("https://api.deezer.com/track/" + std::to_string(id));
                for (const auto& text : net.getAll(trackUrls))
                    for (const auto& c : dz::parseTrackDetail(text).contributors) partners.push_back(c.id);
                std::vector<std::string> newcomers;
                for (const auto id : partners)
                    if (id != who.a.id && !seen.count(id) &&
                        std::find(newcomers.begin(), newcomers.end(), "https://api.deezer.com/artist/" + std::to_string(id)) == newcomers.end())
                        newcomers.push_back("https://api.deezer.com/artist/" + std::to_string(id));
                const auto fiches = net.getAll(newcomers);
                std::vector<std::string> relatedUrls;
                std::vector<std::size_t> added;
                for (const auto& text : fiches) {
                    const auto a = dz::parseArtist(text);
                    if (a.id == 0 || !seen.emplace(a.id, todo.size()).second) continue;
                    added.push_back(todo.size());
                    todo.push_back({a, "collabore avec " + who.a.name, {}, who.hops + 1});
                    relatedUrls.push_back("https://api.deezer.com/artist/" + std::to_string(a.id) + "/related?limit=50");
                }
                const auto related = net.getAll(relatedUrls);
                for (std::size_t k = 0; k < added.size(); ++k)
                    for (const auto& r : dz::parseArtistList(related[k]))
                        if (seen.emplace(r.id, todo.size()).second) todo.push_back({r, "proche de " + todo[added[k]].a.name, {}, 2});
            }
            totalTracks += count;
            artistsOut << who.a.id << '\t' << cell(who.a.name) << '\t' << who.a.fans << '\t' << albums.size() << '\t' << count << '\t'
                       << cell(who.origin) << '\t' << who.a.link << '\n';
            artistsOut.flush();
            tracksOut.flush();
            std::printf("  [%zu/%zu] %-28s %4zu sorties %5d titres  (%s)\n", n + 1, todo.size(), who.a.name.c_str(), albums.size(), count,
                        who.origin.c_str());
            std::fflush(stdout);
        }
        std::printf("%zu artistes, %lld titres -> %s (%lld demandes à Deezer)\n", todo.size(), totalTracks, utf8Path(out).c_str(), net.requests());
        return 0;
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
                    merged.image = old.image, merged.imageCredit = old.imageCredit, merged.cover = old.cover;
                    merged.coverCredit = old.coverCredit, merged.link = old.link;
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
