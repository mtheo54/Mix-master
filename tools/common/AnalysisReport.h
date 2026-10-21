// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/AnalysisReport.h — Rapport de métriques d'un fichier audio.
//
//  Utilise exactement les mêmes mesureurs que le plug-in (omnis_core) :
//  ce que l'outil affiche est ce que l'utilisateur lira dans le DAW.
//
//  Métriques :
//   - Sonie BS.1770-4 : intégrée, max momentary, max short-term, LRA
//   - True Peak (x4), sample peak, PLR = TP − I, PSR = TP − max(S)
//   - Par canal : RMS, facteur de crête, DC, skewness, kurtosis (excès)
//   - Stéréo : corrélation globale, ratio Side/Mid
//   - Écrêtage : nombre de « plateaux » >= 3 échantillons consécutifs à
//     |x| >= 0.999 (signature d'un clipping numérique déjà présent)
// =============================================================================
#pragma once

#include "WavFile.h"

#include <string>
#include <vector>

namespace omnis::tools {

struct ChannelReport {
    double rmsDb = 0.0;
    double samplePeakDb = 0.0;
    double truePeakDb = 0.0;
    double crestFactorDb = 0.0;
    double dcOffset = 0.0;
    double skewness = 0.0;
    double excessKurtosis = 0.0;
    int clippedRuns = 0;
};

/// Analyse par bande (réseau Linkwitz-Riley LR8 à somme passe-tout).
struct BandReport {
    double lowHz = 0.0, highHz = 0.0;
    double energyPercent = 0.0;   ///< part de l'énergie totale (L² + R²)
    double correlation = 0.0;     ///< corrélation L/R sur tout le fichier
    double minCorrelation = 0.0;  ///< pire fenêtre de 200 ms (passages hors phase)
    double sideToMidDb = 0.0;
    double crestMidDb = 0.0;      ///< crête / RMS du Mid dans la bande
};

struct AnalysisReport {
    double sampleRate = 0.0;
    double durationSeconds = 0.0;
    int numChannels = 0;

    double integratedLufs = 0.0;
    double maxMomentaryLufs = 0.0;
    double maxShortTermLufs = 0.0;
    double loudnessRangeLu = 0.0;
    double truePeakDb = 0.0;
    double samplePeakDb = 0.0;
    double plrDb = 0.0;
    double psrDb = 0.0;

    double correlation = 0.0;
    double sideToMidDb = 0.0;

    std::vector<ChannelReport> channels;
    std::vector<BandReport> bands; ///< vide sauf analyse par bandes demandée

    [[nodiscard]] std::string toText(const std::string& title = {}) const;
    [[nodiscard]] std::string toJson() const;
};

/// Mono : sonie mesurée sur le canal unique (G = 1), métriques stéréo neutres.
/// Plus de 2 canaux : seuls les deux premiers (L/R) sont analysés.
AnalysisReport analyze(const AudioFile& file, bool withBands = false);

/// Bornes des bandes d'analyse (Hz) : 60, 120, 250, 500, 2000, 6000.
const std::vector<double>& analysisBandEdges();

} // namespace omnis::tools
