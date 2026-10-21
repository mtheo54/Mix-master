// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/DistortionAnalysis.h — Mesure objective de la distorsion ajoutée.
//
//  But : chiffrer « ça distord » sans dépendre de l'oreille à chaque essai, en
//  séparant ce qui est légitime dans un master de ce qui ne l'est pas :
//
//    légitime     : changements de gain lents (limiteur, compresseur), EQ,
//                   rotation de phase -> expliqués par un filtre linéaire
//                   qui varie doucement dans le temps ;
//    distorsion   : harmoniques, intermodulation, repliement -> énergie à des
//                   fréquences / instants que l'entrée ne contenait pas.
//
//  Méthode (en Mid/Side, TFCT Hann 4096, recouvrement 75 %) :
//   1. filtre linéaire invariant bin par bin sur tout le fichier :
//        H_k = Σ_t Y·X* / Σ_t |X|²   -> absorbe EQ, retard, phase, Mono Maker ;
//   2. par trame et par bande de tiers d'octave, un gain complexe g appliqué à
//      H·X (moindres carrés) -> absorbe les variations lentes de niveau ;
//   3. résidu R = Σ |Y − g·H·X|² : énergie qu'aucun filtre linéaire, même
//      modulé lentement, ne peut produire -> distorsion non linéaire.
//  Exprimée en dB sous le signal, par bande et globalement avec la pondération
//  K de BS.1770 (l'oreille est plus sensible aux médiums qu'au sub).
//  Validation : gain pur -> -173 dB ; retard, EQ -> plancher (voir tests).
//
//  Limite assumée : une distorsion qui tombe exactement sur des partiels déjà
//  présents est en partie absorbée. C'est pourquoi la mesure compte surtout
//  comme COMPARAISON entre deux réglages à sonie égale.
// =============================================================================
#pragma once

#include "WavFile.h"

#include <string>
#include <vector>

namespace omnis::tools {

struct DistortionBand {
    double centerHz = 0.0;
    double distortionDb = 0.0; ///< énergie non linéaire / énergie expliquée
};

struct DistortionReport {
    std::vector<DistortionBand> bands;
    double weightedDb = 0.0;  ///< global, pondéré K
    double subDb = 0.0;       ///< 30–120 Hz
    double lowMidDb = 0.0;    ///< 120–500 Hz
    double midDb = 0.0;       ///< 500 Hz–5 kHz (voix, caisse claire)
    double highDb = 0.0;      ///< 5–16 kHz

    [[nodiscard]] std::string toText(const std::string& title = {}) const;
};

/// `reference` et `processed` doivent être alignés (même longueur conseillée).
DistortionReport measureDistortion(const AudioFile& reference, const AudioFile& processed);

} // namespace omnis::tools
