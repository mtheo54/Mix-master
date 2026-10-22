// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/refdb — Base de références (signatures, jamais d'audio).
//
//    omnis_refdb add titre.wav --artiste "2hollis" [--titre "poster boy"]
//                [--genres "rage|hyperpop"] [--alias "2 hollis"] --db base.txt
//    omnis_refdb list [--db base.txt]
//    omnis_refdb find "2 holis" [--db base.txt]
// =============================================================================
#include "AiFiles.h"
#include "WavFile.h"

#include "analysis/SpectralSignature.h"

#include <cstdio>
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

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::puts("Abrasion Labs // OMNIS — base de références\n"
                  "  omnis_refdb add titre.wav --artiste NOM [--titre T] [--genres \"a|b\"] [--alias \"x|y\"] --db base.txt\n"
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
        else if (input.empty()) input = a;
    }

    if (command == "add") {
        if (input.empty() || artist.empty() || db.empty()) {
            std::fputs("add : fichier, --artiste et --db requis\n", stderr);
            return 1;
        }
        AudioFile f;
        std::string error;
        if (!readWav(input, f, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        ai::ReferenceEntry e;
        e.artist = artist;
        e.title = title;
        e.genres = list(genres);
        e.aliases = list(aliases);
        const auto& r = f.channels[std::min<std::size_t>(1, f.channels.size() - 1)];
        e.signature = analysis::analyzeSignature(f.channels[0].data(), r.data(), f.numFrames(), f.sampleRate);
        ai::ReferenceLibrary user;
        std::string text;
        if (readTextFile(db, text)) user.parse(text);
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
    const auto library = loadLibrary(db);
    if (command == "list") {
        for (const auto& e : library.entries()) {
            std::string g;
            for (const auto& x : e.genres) g += (g.empty() ? "" : ", ") + x;
            std::printf("  %-36s %6.1f LUFS  LRA %4.1f  [%s]\n", e.displayName().c_str(), e.signature.integratedLufs,
                        e.signature.loudnessRangeLu, g.c_str());
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
