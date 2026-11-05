// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestRobustness.cpp — Entrées non fiables : ce que le plug-in lit sans
//  le maîtriser (fichiers audio ajoutés, leurs étiquettes, réponses de
//  MusicBrainz, base de références de la personne). Des milliers de versions
//  tronquées ou corrompues au hasard (graine fixe : reproductible) : aucune ne
//  doit planter, lever d'exception (une exception dans le thread d'import
//  arrêterait le DAW) ni rendre de valeur non finie.
// =============================================================================
#include "TestFramework.h"

#include "AudioRead.h"
#include "WavFile.h"
#include "ai/ReferenceLibrary.h"
#include "import/Json.h"
#include "import/ReferenceImport.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace omnis;

namespace {

std::string be32(std::uint32_t v) { return {char(v >> 24), char(v >> 16), char(v >> 8), char(v)}; }
std::string le32(std::uint32_t v) { return {char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; }

std::string seedId3() {
    const auto frame = [](const std::string& id, const std::string& data) { return id + be32(static_cast<std::uint32_t>(data.size())) + std::string(2, '\0') + data; };
    const std::string body = frame("TIT2", std::string(1, '\1') + "\xFF\xFE" + std::string("B\0R\0R\0", 6)) + frame("TPE1", std::string(1, '\3') + "GRY") +
                             frame("APIC", std::string(1, '\1') + "image/jpeg" + std::string(1, '\0') + "\x03" + std::string("d\0\0\0", 4) + "JPEGDATA");
    return "ID3" + std::string{3, 0, 0, 0, 0, char((body.size() >> 7) & 0x7F), char(body.size() & 0x7F)} + body + std::string(200, '\x55');
}

std::string seedFlac() {
    std::string block = le32(4) + "test" + le32(2);
    for (const std::string c : {"TITLE=BRR", "ARTIST=GRY"}) block += le32(static_cast<std::uint32_t>(c.size())) + c;
    const auto n = static_cast<std::uint32_t>(block.size());
    return "fLaC" + std::string{char(0x84), char(n >> 16), char(n >> 8), char(n)} + block;
}

std::string seedWav() {
    std::string info = "INFO";
    for (const auto& [id, v] : {std::pair{"INAM", std::string("BRR\0", 4)}, std::pair{"IART", std::string("GRY\0", 4)}}) info += id + le32(static_cast<std::uint32_t>(v.size())) + v;
    const std::string list = "LIST" + le32(static_cast<std::uint32_t>(info.size())) + info;
    // fmt + un peu de données PCM 16 bits stéréo à 48 kHz.
    std::string fmt = "fmt " + le32(16) + std::string{1, 0, 2, 0} + le32(48000) + le32(48000 * 4) + std::string{4, 0, 16, 0};
    std::string pcm(4 * 2000, '\0');
    for (std::size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<char>((i * 37) & 0xFF);
    const std::string data = "data" + le32(static_cast<std::uint32_t>(pcm.size())) + pcm;
    const std::string body = "WAVE" + fmt + list + data;
    return "RIFF" + le32(static_cast<std::uint32_t>(body.size())) + body;
}

const std::string kMbSeed =
    R"({"count":2,"isrc":"USUYG1488297","recordings":[{"id":"a","title":"poster boy","length":121000,"artist-credit":[{"name":"2hollis","joinphrase":" & "},{"name":"x"}],"isrcs":["USUYG1488297"]},{"title":"é😀","length":-5}]})";

const std::string kBaseSeed = "[GRY - BRR]\nartiste = GRY\ntitre = BRR\ngenres = rap fr | new wave fr\nlufs = -8.5\nplr = 7.2\nmid = 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21\n"
                              "side = 1 2 3\ndyn = x y\nmicro = 1 2 3 4\nlufs_court = -9 -8 -7\npompe = 2.5\nprofil = 12\ncle = F#m\n";

/// Variantes d'une graine : chaque troncature (graines courtes), octets remplacés,
/// champs de longueur mis à 0xFFFFFFFF, insertions.
std::vector<std::string> mutations(const std::string& seed, std::mt19937& rng, int count) {
    std::vector<std::string> out;
    for (std::size_t n = 0; n < seed.size(); n += std::max<std::size_t>(1, seed.size() / 64)) out.push_back(seed.substr(0, n));
    std::uniform_int_distribution<int> byte(0, 255), kind(0, 3);
    for (int i = 0; i < count && !seed.empty(); ++i) {
        std::string s = seed;
        std::uniform_int_distribution<std::size_t> at(0, s.size() - 1);
        const int edits = 1 + i % 6;
        for (int e = 0; e < edits; ++e) {
            const std::size_t p = at(rng);
            switch (kind(rng)) {
            case 0: s[p] = static_cast<char>(byte(rng)); break;
            case 1:
                for (std::size_t k = p; k < std::min(s.size(), p + 4); ++k) s[k] = '\xFF';
                break;
            case 2: s.insert(p, std::string(1 + static_cast<std::size_t>(byte(rng)) % 16, static_cast<char>(byte(rng)))); break;
            default: s.erase(p, std::min<std::size_t>(s.size() - p, 1 + static_cast<std::size_t>(byte(rng)) % 16)); break;
            }
            if (s.empty()) break;
            at = std::uniform_int_distribution<std::size_t>(0, s.size() - 1);
        }
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace

OMNIS_TEST("robustesse: étiquettes ID3 / FLAC / WAV tronquées ou corrompues (milliers de variantes) : rien ne plante") {
    std::mt19937 rng(20261105);
    int cases = 0;
    for (const auto& s : mutations(seedId3(), rng, 3000)) {
        (void)import::readId3Tags(s);
        ++cases;
    }
    for (const auto& s : mutations(seedFlac(), rng, 3000)) {
        (void)import::readFlacTags(s);
        ++cases;
    }
    for (const auto& s : mutations(seedWav(), rng, 3000)) {
        (void)import::readWavTags(s);
        ++cases;
    }
    std::printf("      %d variantes lues\n", cases);
    OMNIS_CHECK(cases > 9000);
}

OMNIS_TEST("robustesse: réponses MusicBrainz et base de références corrompues : rien ne plante, valeurs finies") {
    std::mt19937 rng(7);
    for (const auto& s : mutations(kMbSeed, rng, 3000)) {
        import::Json j;
        (void)import::parseJson(s, j);
        import::OnlineMatch m;
        (void)import::parseMusicBrainz(s, "2hollis", "poster boy", 121.0, m);
        OMNIS_CHECK(std::isfinite(m.seconds));
    }
    // JSON très imbriqué : refusé sans débordement de pile.
    import::Json deep;
    OMNIS_CHECK(!import::parseJson(std::string(100000, '[') + std::string(100000, ']'), deep));
    int parsed = 0;
    for (const auto& s : mutations(kBaseSeed, rng, 3000)) {
        ai::ReferenceLibrary lib;
        (void)lib.parse(s);
        for (const auto& e : lib.entries()) {
            OMNIS_CHECK(e.profileTitles >= 0);
            (void)lib.serialize();
            ++parsed;
        }
    }
    OMNIS_CHECK(parsed > 0);
}

OMNIS_TEST("robustesse: fichiers WAV, MP3 et FLAC abîmés ajoutés comme référence : lus ou refusés, jamais de plantage") {
    std::mt19937 rng(99);
    const auto dir = std::filesystem::temp_directory_path() / "omnis_fuzz_audio";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    int read = 0, refused = 0;
    const std::pair<const char*, std::string> seeds[] = {{"wav", seedWav()}, {"mp3", seedId3() + std::string(4000, '\xFF')}, {"flac", seedFlac() + std::string(500, '\0')}};
    for (const auto& [ext, seed] : seeds) {
        int i = 0;
        for (const auto& s : mutations(seed, rng, 150)) {
            const auto path = dir / ("GRY - BRR " + std::to_string(i++) + "." + ext);
            std::ofstream(path, std::ios::binary) << s;
            tools::AudioFile f;
            std::string error;
            if (tools::readAudioFile(path, f, error)) {
                ++read;
                for (const auto& ch : f.channels)
                    for (double v : ch) OMNIS_CHECK(std::isfinite(v));
            } else {
                ++refused;
            }
            (void)import::readFileTags(path);
            (void)import::readCoverImages(path);
        }
    }
    std::printf("      %d lus, %d refusés\n", read, refused);
    std::filesystem::remove_all(dir, ec);
}
