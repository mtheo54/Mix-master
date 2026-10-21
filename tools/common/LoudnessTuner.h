// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/LoudnessTuner.h — Atteindre une sonie cible avec le moins de
//  distorsion possible.
//
//  La personne demande une intensité (« −6 LUFS », « très fort mais propre ») ;
//  il n'y a pas de mode figé. Pour chaque stratégie candidate (répartition du
//  gain entre clipper et limiteur, forme du clipper, découpage en bandes,
//  release…), le tuner :
//    1. extrait le passage le plus fort du morceau (30 s, fenêtre short-term
//       maximale) : c'est là que la distorsion se décide ;
//    2. cherche par dichotomie le gain total qui donne la sonie intégrée cible
//       (±0.05 LU) — sonie croissante avec le gain ;
//    3. mesure la distorsion non linéaire (DistortionAnalysis) à cette sonie.
//  Comparer à sonie égale est indispensable : plus fort paraît toujours mieux,
//  et moins fort distord toujours moins.
//
//  C'est le précurseur du prédicteur de paramètres du module 5 : le même
//  critère servira de fonction de coût à l'apprentissage.
// =============================================================================
#pragma once

#include "DistortionAnalysis.h"
#include "WavFile.h"

#include <string>
#include <utility>
#include <vector>

namespace omnis::tools {

struct TuningStrategy {
    std::string name;
    std::vector<std::pair<std::string, double>> fixed; ///< réglages fixes (clé -> plain)
    /// Paramètres qui reçoivent une part du gain total G : (clé, part). Les
    /// parts sont des fractions de G (ex. clip_drive 0.7, lim_gain 0.3).
    std::vector<std::pair<std::string, double>> gainShares;
};

struct TuningResult {
    TuningStrategy strategy;
    double totalGainDb = 0.0;
    double integratedLufs = 0.0;
    double truePeakDb = 0.0;
    bool reached = false;
    DistortionReport distortion;
};

struct TuningOptions {
    double targetLufs = -6.0;
    double excerptSeconds = 30.0;
    double maxGainDb = 48.0;
    double toleranceLu = 0.05;
};

/// Passage le plus fort (fenêtre glissante de `seconds` sur la sonie short-term).
AudioFile loudestExcerpt(const AudioFile& input, double seconds);

/// Rend `input` à travers la chaîne complète avec des réglages donnés.
AudioFile renderWithSettings(const AudioFile& input, const std::vector<std::pair<std::string, double>>& settings);

TuningResult tuneStrategy(const AudioFile& excerpt, const TuningStrategy& strategy, const TuningOptions& options);

/// Paramètres finaux (fixes + parts de gain) d'un résultat.
std::vector<std::pair<std::string, double>> settingsOf(const TuningResult& result);

} // namespace omnis::tools
