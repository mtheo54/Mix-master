// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/TonalBalance.h — L'équilibre tonal de la prod est-il respecté ?
//
//  Garde-fou obligatoire de tout réglage automatique : un traitement peut être
//  « propre » (peu de distorsion) et pourtant détruire la prod (ex. : partage
//  de marge du clipper qui a vidé le sub de 14 dB, mesuré après coup).
//
//  Spectre moyen (Welch, Hann 8192, Mid + Side) intégré par bandes d'octave
//  31.5 Hz … 16 kHz, puis écart sortie − entrée par bande, après retrait de
//  l'écart MOYEN pondéré par l'énergie (un simple changement de volume ne
//  compte pas). Un écart de +2 dB à 8 kHz signifie : les aigus ont gagné 2 dB
//  par rapport au reste du spectre.
// =============================================================================
#pragma once

#include "WavFile.h"

#include <string>
#include <vector>

namespace omnis::tools {

struct TonalBand {
    double centerHz = 0.0;
    double deviationDb = 0.0;
    double sharePercent = 0.0; ///< part de l'énergie de la prod d'origine
};

struct TonalReport {
    std::vector<TonalBand> bands;
    double maxDeviationDb = 0.0; ///< |écart| maximal sur les bandes significatives

    [[nodiscard]] std::string toText() const;
};

/// Bandes « significatives » : au moins 0.5 % de l'énergie de la prod.
TonalReport compareTonalBalance(const AudioFile& reference, const AudioFile& processed);

} // namespace omnis::tools
