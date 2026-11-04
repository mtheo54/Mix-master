// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestReferenceImport.cpp — Ajouter un fichier de référence : étiquettes,
//  nom « Artiste - Titre » (dans les deux sens), MusicBrainz (réponses simulées,
//  aucun réseau pendant les tests), mesure et écriture dans la base de la personne.
// =============================================================================
#include "TestFramework.h"

#include "WavFile.h"
#include "import/ReferenceImport.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>

using namespace omnis;
using namespace omnis::import;

namespace {

std::string be32(std::uint32_t v) { return {char(v >> 24), char(v >> 16), char(v >> 8), char(v)}; }
std::string le32(std::uint32_t v) { return {char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; }
std::string syncsafe(std::uint32_t v) { return {char((v >> 21) & 0x7F), char((v >> 14) & 0x7F), char((v >> 7) & 0x7F), char(v & 0x7F)}; }

/// Un bloc ID3v2.3 : TIT2 / TPE1 / TSRC en texte, APIC avec une « image ».
std::string id3(const std::string& title, const std::string& artist, const std::string& isrc, const std::string& picture) {
    const auto frame = [](const std::string& id, const std::string& data) { return id + be32(static_cast<std::uint32_t>(data.size())) + std::string(2, '\0') + data; };
    std::string body = frame("TIT2", std::string(1, '\3') + title) + frame("TPE1", std::string(1, '\3') + artist) + frame("TSRC", std::string(1, '\0') + isrc);
    if (!picture.empty()) body += frame("APIC", std::string(1, '\0') + "image/png" + std::string(1, '\0') + "\x03" + "couv" + std::string(1, '\0') + picture);
    return "ID3" + std::string{3, 0, 0} + syncsafe(static_cast<std::uint32_t>(body.size())) + body;
}

std::string flacWithComments(const std::string& title, const std::string& artist) {
    std::string block = le32(4) + "test";
    const std::string c1 = "TITLE=" + title, c2 = "ARTIST=" + artist;
    block += le32(2) + le32(static_cast<std::uint32_t>(c1.size())) + c1 + le32(static_cast<std::uint32_t>(c2.size())) + c2;
    const auto n = static_cast<std::uint32_t>(block.size());
    return "fLaC" + std::string{char(0x84), char(n >> 16), char(n >> 8), char(n)} + block;
}

std::string wavWithInfo(const std::string& title, const std::string& artist) {
    const auto sub = [](const std::string& id, std::string v) {
        v += '\0';
        if (v.size() & 1) v += '\0';
        return id + le32(static_cast<std::uint32_t>(v.size())) + v;
    };
    const std::string info = "INFO" + sub("INAM", title) + sub("IART", artist);
    const std::string list = "LIST" + le32(static_cast<std::uint32_t>(info.size())) + info;
    return "RIFF" + le32(static_cast<std::uint32_t>(4 + list.size())) + "WAVE" + list;
}

/// Réponse de MusicBrainz (recherche d'enregistrements), au format réel.
std::string mbRecording(const std::string& artist, const std::string& title, int lengthMs, const std::string& isrc = {}) {
    return R"({"count":1,"recordings":[{"id":"x","title":")" + title + R"(","length":)" + std::to_string(lengthMs) +
           R"(,"artist-credit":[{"name":")" + artist + R"(","joinphrase":""}])" + (isrc.empty() ? "" : R"(,"isrcs":[")" + isrc + R"("])") + "}]}";
}

std::filesystem::path tempDir(const char* name) {
    auto d = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(d, ec);
    std::filesystem::create_directories(d);
    return d;
}

/// Un son de 6 s (808 + accord) écrit en WAV.
void writeTone(const std::filesystem::path& path) {
    tools::AudioFile f;
    f.sampleRate = 48000.0;
    f.resize(2, 6 * 48000);
    for (int i = 0; i < f.numFrames(); ++i) {
        const double t = i / 48000.0;
        const double x = 0.3 * std::sin(2 * std::numbers::pi * 49.0 * t) + 0.05 * std::sin(2 * std::numbers::pi * 440.0 * t);
        f.channels[0][static_cast<std::size_t>(i)] = f.channels[1][static_cast<std::size_t>(i)] = x;
    }
    std::string error;
    tools::writeWav(path.string(), f, tools::SampleFormat::Pcm24, error);
}

} // namespace

OMNIS_TEST("import: nom de fichier « Artiste - Titre » (numéro, clip, prod, bpm, tirets bas retirés)") {
    auto p = splitFileName("GRY - BRR");
    OMNIS_CHECK(p.first == "GRY" && p.second == "BRR");
    p = splitFileName("01 - La Fève - MAUVAIS PAYEUR (Clip Officiel)");
    OMNIS_CHECK(p.first == "La Fève" && p.second == "MAUVAIS PAYEUR");
    p = splitFileName("Sloppy_Joe_-_SLAYR");
    OMNIS_CHECK(p.first == "Sloppy Joe" && p.second == "SLAYR");
    p = splitFileName("omyll - plucked - 135bpm");
    OMNIS_CHECK(p.first == "omyll" && p.second == "plucked");
    p = splitFileName("2hollis – poster boy [Official Video]");
    OMNIS_CHECK(p.first == "2hollis" && p.second == "poster boy");
    OMNIS_CHECK(!splitFileName("bissap ognion").ok()); // une seule partie : refusé
    OMNIS_CHECK(bareTitle("FE!N (feat. Playboi Carti) [Official Audio]") == "FE!N");
}

OMNIS_TEST("import: étiquettes ID3v2 (titre, artiste, ISRC, pochette), FLAC et WAV") {
    const std::string tag = id3("poster boy", "2hollis", "USUYG1488297", "PNGDATA");
    const auto t = readId3Tags(tag);
    OMNIS_CHECK(t.title == "poster boy" && t.artist == "2hollis" && t.isrc == "USUYG1488297");
    const auto f = readFlacTags(flacWithComments("BRR", "GRY"));
    OMNIS_CHECK(f.title == "BRR" && f.artist == "GRY");
    const auto w = readWavTags(wavWithInfo("Sloppy Joe", "SLAYR"));
    OMNIS_CHECK(w.title == "Sloppy Joe" && w.artist == "SLAYR");
    // La pochette d'un MP3 (APIC).
    const auto dir = tempDir("omnis_import_tags");
    std::ofstream(dir / "a.mp3", std::ios::binary) << tag;
    const auto covers = readCoverImages(dir / "a.mp3");
    OMNIS_CHECK(covers.size() == 1 && covers.front() == "PNGDATA");
}

OMNIS_TEST("import: MusicBrainz retenu seulement si artiste ET titre ET durée collent (homonymes écartés)") {
    OnlineMatch m;
    OMNIS_CHECK(parseMusicBrainz(mbRecording("slayr", "Sloppy Joe", 146000), "SLAYR", "Sloppy Joe", 146.0, m) && m.found);
    OMNIS_CHECK(parseMusicBrainz(mbRecording("slayr", "Sloppy Joe", 146000), "SLAYR", "Sloppy Joe", 120.0, m)); // extrait plus court : accepté
    OMNIS_CHECK(!parseMusicBrainz(mbRecording("slayr", "Sloppy Joe", 146000), "SLAYR", "Sloppy Joe", 400.0, m)); // version bien plus longue
    OMNIS_CHECK(!parseMusicBrainz(mbRecording("Gry", "Kissing", 200000), "GRY", "BRR", 125.0, m));  // l'homonyme : autre titre
    OMNIS_CHECK(!parseMusicBrainz(mbRecording("Duke Ellington", "Sloppy Joe", 195000), "SLAYR", "Sloppy Joe", 146.0, m)); // autre artiste
    OMNIS_CHECK(!parseMusicBrainz("{\"error\":\"busy\"}", "SLAYR", "Sloppy Joe", 146.0, m));
    // Adresses : tout est encodé (guillemets, espaces, accents).
    const auto url = musicBrainzSearchUrl("La Fève", "MAUVAIS PAYEUR");
    OMNIS_CHECK(url.find(' ') == std::string::npos && url.find('"') == std::string::npos && url.find("La%20F%C3%A8ve") != std::string::npos);
}

OMNIS_TEST("import: « Titre - Artiste » remis dans l'ordre (artiste connu, ou MusicBrainz) ; une seule requête si c'est clair") {
    const auto dir = tempDir("omnis_import_order");
    writeTone(dir / "Sloppy Joe - SLAYR.wav");
    writeTone(dir / "BRR - Inconnu.wav");
    const auto known = [](const std::string& n) { return n == "SLAYR"; };
    // Hors ligne : SLAYR est connu, il est l'artiste.
    auto id = identifyFile(dir / "Sloppy Joe - SLAYR.wav", 6.0, known, nullptr);
    OMNIS_CHECK(id.artist == "SLAYR" && id.title == "Sloppy Joe" && !id.asked);
    // En ligne, ordre incertain : la 2e requête (sens inverse) confirme et retourne l'ordre.
    int requests = 0;
    const Fetch fetch = [&](const std::string& url, std::string& body) {
        ++requests;
        body = url.find("recording%3A%22BRR%22") != std::string::npos && url.find("artist%3A%22Inconnu%22") != std::string::npos
                   ? mbRecording("Inconnu", "BRR", 6000)
                   : std::string("{\"count\":0,\"recordings\":[]}");
        return true;
    };
    id = identifyFile(dir / "BRR - Inconnu.wav", 6.0, [](const std::string&) { return false; }, &fetch);
    OMNIS_CHECK(id.asked && id.confirmed && id.artist == "Inconnu" && id.title == "BRR");
    OMNIS_CHECK(requests == 2);
    // Ordre clair (artiste connu) et absent de MusicBrainz : une seule requête, le nom gardé.
    requests = 0;
    id = identifyFile(dir / "Sloppy Joe - SLAYR.wav", 6.0, known, &fetch);
    OMNIS_CHECK(id.asked && !id.confirmed && id.artist == "SLAYR" && requests == 1);
}

OMNIS_TEST("import: fichier ajouté = mesuré, rangé dans la base de la personne ; sans artiste ni titre : refusé") {
    const auto dir = tempDir("omnis_import_base");
    const auto base = dir / "references_utilisateur.txt";
    writeTone(dir / "SLAYR - Sloppy Joe.wav");
    writeTone(dir / "bissap.wav");
    const auto known = [](const std::string& n) { return n == "SLAYR"; };
    const auto genres = [](const std::string&) { return std::vector<std::string>{"rage"}; };
    const auto refused = importReferenceFile(dir / "bissap.wav", base, known, genres, nullptr);
    OMNIS_CHECK(!refused.ok && refused.message.find("Artiste - Titre") != std::string::npos);
    const auto r = importReferenceFile(dir / "SLAYR - Sloppy Joe.wav", base, known, genres, nullptr);
    std::printf("      %s\n", r.message.c_str());
    OMNIS_CHECK(r.ok && r.entry.measured && r.entry.signature.activeFrames > 0);
    ai::ReferenceLibrary lib;
    std::ifstream in(base);
    std::string text((std::istreambuf_iterator<char>(in)), {});
    OMNIS_CHECK(lib.parse(text));
    OMNIS_CHECK(lib.entries().size() == 1 && lib.entries().front().artist == "SLAYR" && lib.entries().front().title == "Sloppy Joe");
    OMNIS_CHECK(lib.entries().front().measured && lib.entries().front().genres == std::vector<std::string>{"rage"});
}
