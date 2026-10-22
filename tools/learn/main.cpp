// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/learn — Apprentissage (local, puis communautaire).
//
//  Machine de la personne :
//    omnis_learn local retours.tsv --lexique lexique_utilisateur.txt
//    omnis_learn export retours.tsv --sortie agrege.txt      (anonyme)
//  Serveur :
//    omnis_learn fusion a.txt b.txt … --sortie fusion.txt
//    omnis_learn communaute fusion.txt --sortie lexique_v2.txt
//                [--min-personnes 10] [--min-observations 20]
// =============================================================================
#include "AiFiles.h"

#include "ai/Feedback.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace omnis;
using namespace omnis::tools;

namespace {

void print(const ai::LearningReport& r) {
    std::printf("%d retour(s) utilisé(s)\n", r.recordsUsed);
    for (const auto& c : r.changes) std::printf("  - %s\n", c.c_str());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::puts("Abrasion Labs // OMNIS — apprentissage\n"
                  "  omnis_learn local retours.tsv --lexique lexique_utilisateur.txt\n"
                  "  omnis_learn export retours.tsv --sortie agrege.txt\n"
                  "  omnis_learn fusion a.txt b.txt ... --sortie fusion.txt\n"
                  "  omnis_learn communaute fusion.txt --sortie lexique.txt [--min-personnes 10] [--min-observations 20]");
        return 1;
    }
    const std::string command = argv[1];
    std::vector<std::string> inputs;
    std::string lexiconPath, output;
    ai::CommunityRules rules;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--lexique" && i + 1 < argc) lexiconPath = argv[++i];
        else if (a == "--sortie" && i + 1 < argc) output = argv[++i];
        else if (a == "--min-personnes" && i + 1 < argc) rules.minContributors = std::atoi(argv[++i]);
        else if (a == "--min-observations" && i + 1 < argc) rules.minObservations = std::atoi(argv[++i]);
        else inputs.push_back(a);
    }
    if (inputs.empty()) return 1;

    if (command == "local") {
        if (lexiconPath.empty()) return 1;
        auto lexicon = loadLexicon(lexiconPath);
        print(ai::learnFromFeedback(lexicon, ai::loadFeedback(inputs[0])));
        if (!writeTextFile(lexiconPath, lexicon.serialize())) return 1;
        std::printf("Lexique utilisateur : %s\n", lexiconPath.c_str());
        return 0;
    }
    if (command == "export") {
        const std::string text = ai::exportAggregate(ai::loadFeedback(inputs[0]));
        if (output.empty()) std::fputs(text.c_str(), stdout);
        else if (!writeTextFile(output, text)) return 1;
        return 0;
    }
    if (command == "fusion") {
        std::vector<std::string> all;
        for (const auto& path : inputs) {
            std::string t;
            if (readTextFile(path, t)) all.push_back(t);
        }
        const std::string merged = ai::mergeAggregates(all);
        if (output.empty()) std::fputs(merged.c_str(), stdout);
        else if (!writeTextFile(output, merged)) return 1;
        return 0;
    }
    if (command == "communaute") {
        std::string merged;
        if (!readTextFile(inputs[0], merged)) return 1;
        auto lexicon = loadLexicon(lexiconPath);
        print(ai::applyCommunity(lexicon, merged, rules));
        if (!output.empty() && !writeTextFile(output, lexicon.serialize())) return 1;
        return 0;
    }
    std::fprintf(stderr, "commande inconnue : %s\n", command.c_str());
    return 1;
}
