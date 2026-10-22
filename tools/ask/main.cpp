// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/ask — Ce que le plug-in comprend d'une demande (et retour d'écoute).
//
//    omnis_ask "ça manque de crispy, comme 2hollis mais garde ma 808"
//              [--db refs_utilisateur.txt] [--lexique lexique_utilisateur.txt]
//              [--son]                         (un son de référence est fourni)
//              [--log retours.tsv --note +1 --correction "presence=+1"]
// =============================================================================
#include "AiFiles.h"

#include "ai/Feedback.h"
#include "ai/MatchIntent.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace omnis;
using namespace omnis::tools;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::puts("Abrasion Labs // OMNIS — compréhension de la demande\n"
                  "  omnis_ask \"phrase\" [--db refs.txt] [--lexique lexique.txt] [--son]\n"
                  "            [--log retours.tsv --note -1|0|+1 --correction \"presence=+1, sub=-1\"]");
        return 1;
    }
    std::string db, lexiconPath, logPath, correction;
    bool audio = false;
    int rating = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--db" && i + 1 < argc) db = argv[++i];
        else if (a == "--lexique" && i + 1 < argc) lexiconPath = argv[++i];
        else if (a == "--son") audio = true;
        else if (a == "--log" && i + 1 < argc) logPath = argv[++i];
        else if (a == "--note" && i + 1 < argc) rating = std::atoi(argv[++i]);
        else if (a == "--correction" && i + 1 < argc) correction = argv[++i];
    }
    const auto lexicon = loadLexicon(lexiconPath);
    const auto library = loadLibrary(db);
    const auto u = ai::understandRequest(argv[1], lexicon, library, audio);

    std::printf("Demande : « %s »\n", argv[1]);
    std::printf("Compris :\n");
    for (const auto& s : u.request.understood) std::printf("  - %s\n", s.c_str());
    for (const auto& s : u.request.pending) std::printf("  - (à venir) %s\n", s.c_str());
    if (!u.interpretation.unknownWords.empty()) {
        std::printf("Mots que je ne connais pas encore :");
        for (const auto& w : u.interpretation.unknownWords) std::printf(" « %s »", w.c_str());
        std::printf("\n");
    }
    char loudness[64];
    if (u.request.targetLufs) std::snprintf(loudness, sizeof(loudness), "%.1f LUFS", *u.request.targetLufs);
    else if (u.request.loudnessOffsetLu == 0.0) std::snprintf(loudness, sizeof(loudness), "%s", u.hasReference() ? "de la référence" : "-10 LUFS (sans référence)");
    else std::snprintf(loudness, sizeof(loudness), "%+.1f LU %s", u.request.loudnessOffsetLu, u.hasReference() ? "autour de la référence" : "autour de -10 LUFS");
    std::printf("Ressemblance %.0f %%, tolérance tonale %.1f dB, sonie %s\n", 100.0 * u.request.resemblance, u.request.maxTonalDeviationDb,
                loudness);
    if (u.suggestion) std::printf("\nConseil : %s\n", u.suggestion->c_str());
    if (u.question) std::printf("\nQuestion : %s\n", u.question->c_str());

    if (!logPath.empty()) {
        ai::FeedbackRecord r;
        r.request = argv[1];
        r.hits = u.interpretation.hits;
        r.unknownWords = u.interpretation.unknownWords;
        r.unknownReferences = u.unknownReferences;
        r.reference = u.referenceName;
        r.rating = rating;
        if (!correction.empty()) {
            const auto c = ai::parseCorrection(correction);
            if (!c) {
                std::fprintf(stderr, "correction illisible : %s\n", correction.c_str());
                return 1;
            }
            r.correction = *c;
        }
        if (!ai::appendFeedback(logPath, r)) {
            std::fprintf(stderr, "écriture impossible : %s\n", logPath.c_str());
            return 1;
        }
        std::printf("\nRetour enregistré dans %s\n", logPath.c_str());
    }
    return u.question ? 2 : 0;
}
