// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestStems.cpp — Éléments, groupes, noms de pistes et de bus.
// =============================================================================
#include "TestFramework.h"

#include "stems/Element.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace omnis::stems;

OMNIS_TEST("stems: nom de piste -> élément (noms de packs, abréviations, collés)") {
    struct Case {
        const char* name;
        Element expected;
    };
    const Case cases[] = {
        {"SH_Kick_Hard_04_Cmin", Element::Kick},   {"PL_ClosedHat_12", Element::HiHat},     {"OH 03", Element::OpenHat},
        {"Snr Crack", Element::Snare},             {"clp", Element::Clap},                  {"808_Mafia_Bass_C", Element::Bass808},
        {"VOX_lead", Element::Vocal},              {"Rhodes Chords", Element::Keys},        {"gtr", Element::Guitar},
        {"trp", Element::Brass},                   {"JAYY SPLASH 03", Element::Cymbal},     {"BD_Punchy", Element::Kick},
        {"jayykick", Element::Kick},               {"Bass Drum 2", Element::Kick},          {"Trompette", Element::Brass},
        {"Adlibs", Element::Backs},                {"Shaker 1", Element::Perc},             {"Piano", Element::Keys},
        {"guitare", Element::Guitar},              {"OpenHat_Loose", Element::OpenHat},     {"Riser FX", Element::Riser},
        {"Insert 3", Element::Auto},               {"Audio 12", Element::Auto},             {"Cmin_Ghost_140", Element::Auto},
        // EFX : le groupe des effets (reverse cymbals, risers, impacts…).
        {"Rev Crash 03", Element::ReverseCymbal},  {"REVERSE_CYMBAL_Long", Element::ReverseCymbal}, {"RevCym 2", Element::ReverseCymbal},
        {"Uplifter 8 bars", Element::Riser},       {"FX Impact", Element::Impact},          {"Sub Drop", Element::Impact},
        {"Downlifter", Element::Impact},           {"Vinyl Crackle", Element::Fx},          {"Tape Stop", Element::Fx},
    };
    for (const auto& c : cases) {
        const auto hint = elementFromName(c.name);
        if (hint.element != c.expected)
            std::printf("      « %s » -> %s (attendu %s)\n", c.name, elementLabel(hint.element), elementLabel(c.expected));
        OMNIS_CHECK(hint.element == c.expected);
    }
    OMNIS_CHECK(nameWords("SH_KickHard04") == std::vector<std::string>({"sh", "kick", "hard", "04"}));
}

OMNIS_TEST("stems: groupes DRUMS / 808 / MÉLO / VOIX / EFX, nom d'un bus -> son groupe") {
    OMNIS_CHECK(familyOf(Element::ReverseCymbal) == Family::Fx && familyOf(Element::Riser) == Family::Fx && familyOf(Element::Impact) == Family::Fx);
    OMNIS_CHECK(std::string(familyLabel(Family::Fx)) == "EFX" && std::string(familyLabel(Family::Instrument)) == "MÉLO");
    OMNIS_CHECK(familyFromName("EFX") == Family::Fx);
    OMNIS_CHECK(familyFromName("FX Bus") == Family::Fx);
    OMNIS_CHECK(familyFromName("DRUMS") == Family::Drums);
    OMNIS_CHECK(familyFromName("Drum Bus") == Family::Drums);
    OMNIS_CHECK(familyFromName("MELO") == Family::Instrument);
    OMNIS_CHECK(familyFromName("Synths") == Family::Instrument);
    OMNIS_CHECK(familyFromName("808") == Family::Bass);
    OMNIS_CHECK(familyFromName("Insert 12") == Family::Unknown);
    // Chaque élément a sa clé et son libellé, relus à l'identique.
    for (int i = 1; i < kElementCount; ++i) OMNIS_CHECK(elementFromKey(elementKey(static_cast<Element>(i))) == static_cast<Element>(i));
}
