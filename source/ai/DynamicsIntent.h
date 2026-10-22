// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/DynamicsIntent.h — Tenir la prod dans le temps comme la référence.
//
//  Trait persistant mesuré (calibration module 4, §5) : la VARIATION de chaque
//  bande dans le temps (P90 − P10 du niveau, analysis::SpectralSignature).
//  La 808 de SLAYR varie de 36 dB, celle de la prod de 52 : c'est la tenue,
//  pas l'EQ, qui fait la sonie de la référence.
//
//  Règles, par bande du multibande (sub < 120 Hz < bas < 800 < médium < 5 k < aigu) :
//   1. Écart de tenue = variation de la prod − variation de la référence
//      (moyenne des nœuds de la bande). Moins de 2 dB : on ne touche pas.
//   2. On n'en referme qu'une fraction (la ressemblance de la demande), et
//      jamais plus de 60 % de la variation de la prod (sa vie, ses nuances).
//   3. Seuil = médiane du niveau de la bande (analysis::BandLevels) : seuls
//      les moments au-dessus de son niveau habituel sont tenus. Ratio tel que
//      la moitié haute de la variation se réduise de l'écart voulu, borné à
//      4:1 (2:1 pour une région protégée, « garde ma 808 ») : au-delà, la 808
//      perd sa vie.
//   4. Attaque : 10 ms dans le sub (la 808 est tenue dès le coup, grâce à
//      l'anticipation) ; 20–30 ms au-dessus, pour laisser passer les
//      transitoires — davantage si la demande parle de punch.
//   5. Compensation : recalée par mesure (outil) pour rendre à chaque bande
//      son énergie moyenne -> l'équilibre tonal de l'intention ne bouge pas.
// =============================================================================
#pragma once

#include "MatchIntent.h"

#include "analysis/BandLevels.h"
#include "analysis/SpectralSignature.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace omnis::ai {

struct BandDynamics {
    double thresholdDb = 0.0;
    double ratio = 1.0;
    double attackMs = 10.0;
    double releaseMs = 200.0;
    double makeupDb = 0.0;
    [[nodiscard]] bool active() const noexcept { return ratio > 1.001; }
};

struct DynamicsIntent {
    std::array<double, 3> splitsHz{120.0, 800.0, 5000.0};
    std::array<BandDynamics, params::kMbBands> bands{};
    std::vector<std::string> explanation;
};

/// `prodLevels` : niveaux de la prod À L'ENTRÉE du multibande (après le Match EQ).
DynamicsIntent planDynamics(const SpectralSignature& prod, const SpectralSignature& reference,
                            const analysis::BandLevels& prodLevels, const MatchRequest& request, const Descriptor& wishes);

/// Réglages (clé -> valeur) pour le moteur.
std::vector<std::pair<std::string, double>> dynamicsSettings(const DynamicsIntent& intent);

const char* mbBandName(int band) noexcept;

} // namespace omnis::ai
