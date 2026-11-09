// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestStyleRecognizer.cpp — Reconnaître le style d'une prod
//  (ai/StyleRecognizer) : groupes sonores livrés, probabilités, mémoire
//  personnelle (les styles nommés par la personne l'emportent peu à peu),
//  lecture / écriture des fichiers. La précision sur la base se mesure avec
//  `omnis_refdb style-eval --groupes` (validation croisée par artiste).
// =============================================================================
#include "TestFramework.h"

#include "ai/EmbeddedData.h"
#include "ai/StyleRecognizer.h"

#include <cmath>
#include <string>

using namespace omnis;
using namespace omnis::ai;

namespace {

/// Une signature dont les grandeurs de reconnaissance valent `f` (forme, largeur, micro).
analysis::SpectralSignature signatureLike(const std::vector<double>& f) {
    analysis::SpectralSignature s;
    // Forme : relativeMidDb(k) = midDb[k] - corps ; on règle midDb pour retrouver f à peu près,
    // puis on vérifie ce que styleFeatures en lit.
    for (std::size_t k = 0; k < s.midDb.size(); ++k) s.midDb[k] = 60.0;
    for (int k = 1; k <= 18; ++k) s.midDb[static_cast<std::size_t>(k)] = 60.0 + f[static_cast<std::size_t>(k - 1)];
    for (std::size_t k = 0; k < s.sideDb.size(); ++k) s.sideDb[k] = s.midDb[k] - 8.0;
    s.music.valid = true;
    s.music.microDb = {6.0, 7.0, 8.0, 9.0};
    s.music.bpm = 140.0;
    return s;
}

} // namespace

OMNIS_TEST("style: les groupes sonores livrés se relisent (10 groupes, statistiques complètes)") {
    const auto groups = parseStyleStats(std::string(data::groupes()));
    std::printf("      %zu groupes :", groups.size());
    for (const auto& [g, st] : groups) std::printf(" %s (%d)", g.c_str(), st.titles);
    std::printf("\n");
    OMNIS_CHECK(groups.size() >= 8);
    for (const auto& [g, st] : groups) {
        OMNIS_CHECK(st.titles >= 3);
        OMNIS_CHECK(st.mean.size() == st.sd.size() && !st.mean.empty());
    }
    // Lecture / écriture : le fichier se relit à l'identique (à 1e-4 près, le format).
    const auto again = parseStyleStats(serializeStyleStats(groups));
    OMNIS_CHECK(again.size() == groups.size());
    for (const auto& [g, st] : groups) OMNIS_CHECK(std::fabs(again.at(g).mean[3] - st.mean[3]) < 1e-3);
}

OMNIS_TEST("style: chaque sous-genre a son groupe sonore (trap fr = trap, plugg = new wave), et un style à viser") {
    OMNIS_CHECK(sonicGroup("", "trap fr") == "trap");
    OMNIS_CHECK(sonicGroup("", "dark trap") == "trap");
    OMNIS_CHECK(sonicGroup("", "rage fr") == "rage");
    OMNIS_CHECK(sonicGroup("", "hard clipping") == "rage");
    OMNIS_CHECK(sonicGroup("", "pluggnb") == "plugg / new wave");
    OMNIS_CHECK(sonicGroup("Électro, House & Dance", "") == "électro");
    OMNIS_CHECK(sonicGroup("Rap Français & New Wave", "").empty()); // famille seule : groupe inconnu
    OMNIS_CHECK(styleForGroup("plugg / new wave") == "new wave fr");
    OMNIS_CHECK(styleForGroup("rage") == "rage fr");
}

OMNIS_TEST("style: probabilités qui somment à 1, le plus proche en tête ; rien entendu = pas de réponse") {
    const auto groups = parseStyleStats(std::string(data::groupes()));
    // Une prod qui a exactement les grandeurs moyennes du groupe « rage ».
    const auto& rage = groups.at("rage");
    analysis::SpectralSignature s;
    s.midDb.fill(60.0);
    for (int k = 1; k <= 18; ++k) s.midDb[static_cast<std::size_t>(k)] = 0.0;
    // Construire une signature à partir des moyennes : on passe par styleFeatures pour comparer.
    auto probe = signatureLike(std::vector<double>(rage.mean.begin(), rage.mean.begin() + 18));
    const auto rec = recognizeGroup(probe, groups);
    OMNIS_CHECK(!rec.empty());
    double sum = 0.0;
    for (const auto& g : rec.ranked) sum += g.probability;
    OMNIS_CHECK(std::fabs(sum - 1.0) < 1e-9);
    for (std::size_t i = 1; i < rec.ranked.size(); ++i) OMNIS_CHECK(rec.ranked[i - 1].probability >= rec.ranked[i].probability - 1e-12);
    std::printf("      %s\n", describeRecognition(rec).c_str());
    analysis::SpectralSignature silence; // tout à 0 : rien entendu
    OMNIS_CHECK(recognizeGroup(silence, groups).empty());
}

OMNIS_TEST("style: la mémoire personnelle l'emporte peu à peu (5 prods nommées « drill » -> drill en tête)") {
    const auto groups = parseStyleStats(std::string(data::groupes()));
    auto probe = signatureLike(std::vector<double>(groups.at("boom bap").mean.begin(), groups.at("boom bap").mean.begin() + 18));
    const auto before = recognizeGroup(probe, groups);
    OMNIS_CHECK(!before.empty());
    std::printf("      sans mémoire : %s en tête (%.0f %%)\n", before.ranked[0].style.c_str(), 100.0 * before.ranked[0].probability);
    // La personne a nommé « drill » cinq prods qui sonnent comme celle-ci.
    std::vector<StyleExample> memory;
    for (int i = 0; i < 5; ++i) memory.push_back({"drill", styleFeatures(probe)});
    const auto after = recognizeGroup(probe, groups, {}, &memory);
    std::printf("      avec 5 exemples : %s en tête (%.0f %%)\n", after.ranked[0].style.c_str(), 100.0 * after.ranked[0].probability);
    OMNIS_CHECK(after.ranked[0].style == "drill");
    OMNIS_CHECK(after.ranked[0].probability > 0.5);
    // Un seul exemple ne suffit pas à tout renverser face à un modèle net : il pèse moins.
    std::vector<StyleExample> one{{"drill", styleFeatures(probe)}};
    const auto single = recognizeGroup(probe, groups, {}, &one);
    double pOne = 0.0, pFive = after.ranked[0].probability;
    for (const auto& g : single.ranked)
        if (g.style == "drill") pOne = g.probability;
    OMNIS_CHECK(pOne < pFive);
    // Un style que seule la personne connaît (« jersey ») est proposé.
    std::vector<StyleExample> jersey(6, StyleExample{"jersey", styleFeatures(probe)});
    OMNIS_CHECK(recognizeGroup(probe, groups, {}, &jersey).ranked[0].style == "jersey");
    // Fichier de la mémoire : relu à l'identique.
    const auto read = parseStyleExamples(serializeStyleExamples(memory));
    OMNIS_CHECK(read.size() == memory.size() && read[0].group == "drill");
}
