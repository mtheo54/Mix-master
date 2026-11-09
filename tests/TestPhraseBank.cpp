// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestPhraseBank.cpp — Banque de phrases réelles (beatmaker : parlé,
//  SMS, argot, anglais) : chacune doit agir sur le bon axe, DANS LE BON SENS.
//  Garde-fou contre les inversions (« mon kick est trop fort » montait le
//  kick : « mon » lu « monte » ; « passe le grave en mono » l'élargissait :
//  « passe » lu « pas »). Ajouter ici chaque phrase réelle mal comprise.
// =============================================================================
#include "TestFramework.h"

#include "ai/Lexicon.h"

#include <cstdio>
#include <string>

using namespace omnis::ai;

namespace {

struct Phrase {
    const char* text;
    Axis axis;
    int sign; ///< +1 : l'axe doit monter, -1 : baisser
};

constexpr Phrase kBank[] = {
    {"mon kick est trop fort", Axis::Bass, -1},
    {"mon 808 est trop forte", Axis::Sub, -1},
    {"ma 808 est trop faible", Axis::Sub, +1},
    {"monte le sub", Axis::Sub, +1},
    {"baisse le sub", Axis::Sub, -1},
    {"coupe un peu les basses", Axis::Sub, -1},
    {"y a trop de basse", Axis::Sub, -1},
    {"ya pa assez de basse", Axis::Sub, +1},
    {"la basse elle bave", Axis::Sub, -1},
    {"c'est boueux dans le bas", Axis::LowMid, -1},
    {"trop boueux", Axis::LowMid, -1},
    {"ça manque de clarté", Axis::Presence, +1},
    {"plus brillant", Axis::Presence, +1},
    {"moins brillant", Axis::Presence, -1},
    {"les aigus piquent", Axis::Presence, -1},
    {"les aigus sont trop forts", Axis::Presence, -1},
    {"ça manque d'aigus", Axis::Air, +1},
    {"plus d'air en haut", Axis::Air, +1},
    {"c'est trop sombre", Axis::Air, +1},
    {"c'est trop brillant", Axis::Air, -1},
    {"ça siffle", Axis::Presence, -1},
    {"plus chaud", Axis::LowMid, +1},
    {"c'est trop froid", Axis::LowMid, +1},
    {"plus large", Axis::Width, +1},
    {"moins large", Axis::Width, -1},
    {"passe le grave en mono", Axis::Width, -1},
    {"mets le grave en mono", Axis::Width, -1},
    {"c'est trop large", Axis::Width, -1},
    {"c'est trop étroit", Axis::Width, +1},
    {"plus fort", Axis::Loudness, +1},
    {"moins fort", Axis::Loudness, -1},
    {"pas trop fort", Axis::Loudness, -1},
    {"c'est pas assez fort", Axis::Loudness, +1},
    {"monte le volume", Axis::Loudness, +1},
    {"baisse le volume", Axis::Loudness, -1},
    {"plus de punch", Axis::Punch, +1},
    {"ça tape pas assez", Axis::Punch, +1},
    {"ça claque pas", Axis::Punch, +1},
    {"c'est mou", Axis::Punch, +1},
    {"c'est écrasé", Axis::Density, -1},
    {"trop compressé", Axis::Density, -1},
    {"ça pompe", Axis::Density, -1},
    {"plus dense", Axis::Density, +1},
    {"plus de colle", Axis::Density, +1},
    {"plus de saturation", Axis::Saturation, +1},
    {"sans saturation", Axis::Saturation, -1},
    {"ça sature", Axis::Saturation, -1},
    {"c'est trop sale", Axis::Saturation, -1},
    {"plus propre", Axis::Cleanliness, +1},
    {"plus sale", Axis::Cleanliness, -1},
    {"more bass", Axis::Sub, +1},
    {"less bass", Axis::Sub, -1},
    {"too much bass", Axis::Sub, -1},
    {"more punch", Axis::Punch, +1},
    {"make it louder", Axis::Loudness, +1},
    {"make it wider", Axis::Width, +1},
    {"too bright", Axis::Air, -1},
    {"more air", Axis::Air, +1},
    {"pas de disto", Axis::Saturation, -1},
    {"le kick il tape pas", Axis::Punch, +1},
};

} // namespace

OMNIS_TEST("banque de phrases: chaque demande agit sur le bon axe, dans le bon sens") {
    int wrong = 0;
    for (const auto& p : kBank) {
        const auto in = interpret(p.text, Lexicon::builtin());
        const double v = in.desire[p.axis];
        const bool ok = p.sign > 0 ? v > 1e-9 : v < -1e-9;
        if (!ok) {
            ++wrong;
            std::printf("      ✗ « %s » : %s %+.2f (attendu %s)\n", p.text, axisKey(p.axis), v, p.sign > 0 ? "+" : "-");
        }
    }
    std::printf("      %d / %zu phrases justes\n", static_cast<int>(sizeof(kBank) / sizeof(kBank[0])) - wrong, sizeof(kBank) / sizeof(kBank[0]));
    OMNIS_CHECK(wrong == 0);
}
