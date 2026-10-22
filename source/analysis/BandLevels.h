// =============================================================================
//  Abrasion Labs // OMNIS
//  analysis/BandLevels.h — Niveau de chaque bande du multibande dans le temps.
//
//  Mêmes bandes que stages/MultibandStage (Linkwitz-Riley 24 dB/oct à phase
//  nulle, |B|² appliqué au spectre), même mesure que son détecteur (RMS
//  stéréo, dBFS : un sinus à 0 dBFS crête = −3 dBFS). Trames de 4096
//  échantillons (~90 ms, l'échelle d'un coup de 808), recouvrement 50 %,
//  silences exclus (−20 dB sous le 95e centile). Sert à placer les seuils
//  (médiane de la bande) et à vérifier la compensation (énergie moyenne).
// =============================================================================
#pragma once

#include "params/Parameters.h"

#include <array>

namespace omnis::analysis {

struct BandLevels {
    std::array<double, params::kMbBands> p10{}, p50{}, p90{}, meanDb{}; ///< dBFS RMS
};

BandLevels measureBandLevels(const double* left, const double* right, int numFrames, double sampleRate,
                             const std::array<double, 3>& splitsHz);

} // namespace omnis::analysis
