// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/Element.h — Les éléments d'une prod et leurs groupes.
//
//  25 éléments (kick, open hat, lead, 808, reverse cymbal…) rangés en groupes :
//  DRUMS, 808 / BASSE, MÉLO, VOIX, EFX. OMNIS sur un bus dit lesquels il y
//  entend (stems/BusListener). Le nom d'un bus dans le DAW (« DRUMS », « MELO »,
//  « EFX », « 808 ») donne son groupe : data/elements.txt (formes des noms de
//  packs, découpés en mots : « SH_Kick_Hard_04 », « RevCrash », « 808Mafia »).
//
//  L'ordre des valeurs est publié dans la mémoire partagée : ne jamais le changer
//  sans changer la version de la disposition du lien (link/LinkBus.h).
// =============================================================================
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omnis::stems {

enum class Element : int {
    Auto = 0, ///< paramètre : laisser OMNIS reconnaître ; afficheur : pas encore reconnu
    Kick,
    Snare,
    Clap,
    HiHat,
    OpenHat,
    Cymbal,
    Perc,
    DrumLoop,
    Bass808,
    Bass,
    Keys,
    Guitar,
    Brass,
    Strings,
    Pad,
    Lead,
    Pluck,
    Vocal,
    Backs,
    Fx,            ///< EFX sans forme reconnue (transition, bruit, vinyle…)
    ReverseCymbal, ///< EFX : cymbale à l'envers, monte et s'arrête net
    Riser,         ///< EFX : montée longue (uplifter, sweep)
    Impact,        ///< EFX : gros coup qui traîne (boom, hit, downlifter)
    Other,
    Count
};
inline constexpr int kElementCount = static_cast<int>(Element::Count);

/// Groupes : DRUMS, 808 / BASSE, MÉLO (instruments, synthés), VOIX, EFX (effets).
enum class Family : int { Unknown, Drums, Bass, Instrument, Vocal, Fx };
inline constexpr int kFamilyCount = 6;

Family familyOf(Element e) noexcept;
/// « DRUMS », « 808 / BASSE », « MÉLO », « VOIX », « EFX », « ? » (affiché).
const char* familyLabel(Family f) noexcept;
/// Le groupe que le nom d'un bus annonce (« DRUMS », « Drum Bus », « EFX », « 808 ») ; Unknown sinon.
Family familyFromName(std::string_view busName);

/// Libellé affiché (« Open hat », « Cuivres / vents »…).
const char* elementLabel(Element e) noexcept;
/// Clé du fichier data/elements.txt (« openhat »).
const char* elementKey(Element e) noexcept;
std::optional<Element> elementFromKey(std::string_view key) noexcept;

/// Libellés du paramètre (Auto en tête) et de l'afficheur (« — » en tête).
const std::array<std::string_view, kElementCount>& elementChoiceLabels() noexcept;
const std::array<std::string_view, kElementCount>& elementMeterLabels() noexcept;

/// Mots d'un nom de piste : séparateurs, changements de casse et de lettre à
/// chiffre coupent (« SH_KickHard04 » -> « sh », « kick », « hard », « 04 »).
std::vector<std::string> nameWords(std::string_view trackName);

struct NameHint {
    Element element = Element::Auto; ///< Auto : le nom ne dit rien
    std::string matched;             ///< forme reconnue (« closed hat »)
    int strength = 0;                ///< longueur de la forme : plus long = plus sûr
};

/// Ce que le nom de la piste laisse penser (data/elements.txt).
NameHint elementFromName(std::string_view trackName);

} // namespace omnis::stems
