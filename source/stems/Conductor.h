// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/Conductor.h — Le master chef d'orchestre : il repère qu'un stem en
//  gêne un autre et PROPOSE une retouche. Rien n'est appliqué sans accord.
//
//  Un masquage, c'est un élément qui occupe la place d'un autre AU MÊME
//  MOMENT (stems/StemTimeline, blocs alignés sur le temps du projet) :
//    - le grave d'un instrument, d'une voix, d'un FX, d'un drum (hors kick)
//      dans la zone où vit la 808, la basse ou le kick (sous 300 Hz, là où
//      leur énergie est à moins de 20 dB de leur maximum) ;
//    - un instrument dans la présence de la voix lead (1,2 … 6 kHz).
//  Mesure : dans chaque bande de la zone et chaque bloc où les deux jouent,
//  l'écart (dB) entre le gêneur et la victime ; on retient la médiane sur les
//  blocs, et la bande la plus au-dessus de son seuil. Grave : dans le cœur de
//  la victime (à 10 dB de son maximum), gêne si le gêneur est à moins de 8 dB
//  d'elle ; dans le haut de son corps (10 à 20 dB sous son maximum), s'il y
//  est plus fort qu'elle ; et la zone doit porter au moins 2 % de l'énergie du
//  gêneur. Voix : à moins de 3 dB. Au moins 4 s ensemble.
//
//  Retouche proposée, toujours sur le gêneur (la victime est le cœur du son) :
//    - grave : passe-haut 12 dB/oct en haut de la bande gênante ; si
//      ce grave est le cœur du gêneur (≥ 40 % de son énergie), un plateau
//      −3 dB à la place ;
//    - voix : cloche −2 ou −3 dB là où l'écart est le plus faible.
//  Chaque proposition a une clé stable (gêneur, victime, sorte) : acceptée ou
//  refusée, elle n'est plus reproposée.
// =============================================================================
#pragma once

#include "Correction.h"
#include "Element.h"
#include "StemTimeline.h"

#include <cstdint>
#include <string>
#include <vector>

namespace omnis::stems {

struct ConductorStem {
    std::uint64_t id = 0;           ///< identifiant stable du stem (jeton de son emplacement)
    Element element = Element::Auto;
    std::string name;               ///< nom de la piste
    const StemTimeline* timeline = nullptr;
};

struct Proposal {
    enum class Kind : int { LowMasking = 1, VocalMasking = 2 };
    std::uint32_t key = 0;     ///< stable : (gêneur, victime, sorte)
    Kind kind = Kind::LowMasking;
    int target = -1;           ///< index du gêneur dans l'entrée (celui qu'on retouche)
    int victim = -1;           ///< index de l'élément gêné
    Correction correction;
    double gapDb = 0.0;        ///< écart gêneur − victime dans la bande la plus gênante
    double excessDb = 0.0;     ///< au-dessus du seuil de gêne de cette bande (> 0 : gêne)
    double sharedSeconds = 0.0;
    double zoneLowHz = 0.0, zoneHighHz = 0.0;
    std::string text;          ///< la question posée à la personne
    struct Band {
        double lowHz, highHz, gapDb, thresholdDb;
        int blocks;
    };
    std::vector<Band> bands;   ///< détail de la zone (médianes), pour comprendre
};

class Conductor {
public:
    static constexpr double kLowGapDb = -8.0;     ///< cœur de la victime du grave
    static constexpr double kLowBodyGapDb = 0.0;  ///< haut de son corps
    static constexpr double kVocalGapDb = -3.0;
    static constexpr double kMinSharedSeconds = 4.0;

    /// Propositions, la plus gênante d'abord.
    [[nodiscard]] static std::vector<Proposal> propose(const std::vector<ConductorStem>& stems);
    /// Bilan de chaque paire (gêneur possible, victime) écoutée assez longtemps
    /// ensemble, gênante ou non (`correction.type` None : pas de gêne).
    [[nodiscard]] static std::vector<Proposal> evaluate(const std::vector<ConductorStem>& stems);
    [[nodiscard]] static std::uint32_t keyFor(std::uint64_t target, std::uint64_t victim, Proposal::Kind kind) noexcept;
};

} // namespace omnis::stems
