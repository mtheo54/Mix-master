// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/Engine.h — Moteur DSP : chaîne d'étages, PDC, bypass, mesures.
//
//  Indépendant de tout SDK : le wrapper VST3 et l'outil de rendu hors ligne
//  pilotent exactement le même code (ce que l'on mesure au banc est ce que
//  l'utilisateur entend dans son DAW).
//
//  Flux du signal (64 bits du début à la fin) :
//
//   entrée (f32/f64) ──► conversion double ──┬──► [retard = latence chaîne] ──► sec
//                                            │
//                                            └──► Input Gain ──► Étage 1 … N ──► Output Gain ──► traité
//
//   sortie = sec + mix · (traité − sec)      (mix lissé : 1 = actif, 0 = bypass)
//          ──► mesures (LUFS M/S/I, LRA, True Peak, corrélation)
//          ──► matrice de monitoring (Stéréo / Mid / Side, lissée)
//          ──► conversion vers le format hôte
//
//  Le bypass est donc aligné en temps (null-test parfait entre sec et traité
//  neutre) et commuté sans clic. Les étages continuent de tourner en bypass :
//  leurs états (enveloppes, historiques FIR) restent chauds, ce qui évite tout
//  transitoire au réengagement. Les mesures portent sur le signal de sortie
//  réel (bypass compris) mais avant le monitoring, pour qu'écouter le Side
//  seul ne fausse pas la lecture LUFS du master.
// =============================================================================
#pragma once

#include "Stage.h"

#include "../analysis/LoudnessMeter.h"
#include "../analysis/StereoMeter.h"
#include "../analysis/TruePeakMeter.h"
#include "../core/AudioBuffer.h"
#include "../core/DelayLine.h"
#include "../core/OnePoleSmoother.h"
#include "../core/Platform.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace omnis {

struct MeterReadout {
    double momentaryLufs = analysis::LoudnessMeter::kNegativeInfinity;
    double shortTermLufs = analysis::LoudnessMeter::kNegativeInfinity;
    double integratedLufs = analysis::LoudnessMeter::kNegativeInfinity;
    double loudnessRangeLu = 0.0;
    double truePeakDb = kMinusInfinityDb;   ///< max depuis le dernier reset
    double samplePeakDb = kMinusInfinityDb; ///< max depuis le dernier reset
    double correlation = 0.0;
    double sideToMidDb = kMinusInfinityDb;
};

class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// Non temps réel, avant prepare(). L'ordre d'ajout est l'ordre de la chaîne.
    void appendStage(std::unique_ptr<Stage> stage);
    [[nodiscard]] int numStages() const noexcept { return static_cast<int>(stages_.size()); }

    /// Non temps réel. Alloue tous les tampons. Les valeurs de paramètres déjà
    /// reçues sont conservées et appliquées sans lissage.
    void prepare(double sampleRate, int maxBlockSize);

    /// Temps réel. Vide tous les états (lignes de retard, étages, mesures).
    void reset() noexcept;

    /// Temps réel. Valeur « plain » (dB, index…) ; lissée selon la table.
    void setParameter(params::ParamId id, double plainValue) noexcept;

    /// Comme setParameter mais sans lissage (chargement d'état, initialisation).
    void setParameterImmediate(params::ParamId id, double plainValue) noexcept;

    /// Temps réel. Stéréo uniquement. `inputs` et `outputs` peuvent désigner les
    /// mêmes tampons (traitement en place de l'hôte). numSamples quelconque :
    /// découpé en interne en blocs de maxBlockSize.
    template <typename Sample>
    void process(const Sample* const* inputs, Sample* const* outputs, int numSamples) noexcept;

    /// Latence totale de la chaîne, constante après prepare().
    [[nodiscard]] int latencySamples() const noexcept { return latency_; }
    [[nodiscard]] double sampleRate() const noexcept { return spec_.sampleRate; }
    [[nodiscard]] int maxBlockSize() const noexcept { return spec_.maxBlockSize; }

    [[nodiscard]] MeterReadout meters() const noexcept;

    /// Remet à zéro sonie intégrée, LRA et crêtes mémorisées.
    void resetLoudnessHistory() noexcept;

    /// Accès en lecture aux mesureurs (outils hors ligne, tests).
    [[nodiscard]] const analysis::LoudnessMeter& loudnessMeter() const noexcept { return loudness_; }

private:
    void processChunk(int numSamples) noexcept;
    static void applyGain(OnePoleSmoother& gain, double* const* channels, int numSamples) noexcept;
    void applyMonitorMatrix(int numSamples) noexcept;
    void setMonitorTargets(int mode, bool immediate) noexcept;

    ProcessSpec spec_{};
    bool prepared_ = false;
    int latency_ = 0;

    std::vector<std::unique_ptr<Stage>> stages_;

    AudioBuffer work_;
    AudioBuffer dry_;
    DelayLine dryDelay_;

    OnePoleSmoother inputGain_;
    OnePoleSmoother outputGain_;
    OnePoleSmoother mix_; ///< 1 = traitement actif, 0 = bypass
    // Matrice de monitoring : L' = mLL·L + mLR·R ; R' = mRL·L + mRR·R
    OnePoleSmoother mLL_, mLR_, mRL_, mRR_;

    analysis::LoudnessMeter loudness_;
    analysis::TruePeakMeter truePeak_;
    analysis::StereoMeter stereo_;
};

// -----------------------------------------------------------------------------
template <typename Sample>
void Engine::process(const Sample* const* inputs, Sample* const* outputs, int numSamples) noexcept {
    if (!prepared_ || numSamples <= 0) return;
    ScopedNoDenormals noDenormals;

    int offset = 0;
    while (offset < numSamples) {
        const int n = std::min(numSamples - offset, spec_.maxBlockSize);
        for (int ch = 0; ch < kNumChannels; ++ch) {
            const Sample* src = inputs[ch] + offset;
            double* w = work_.channel(ch);
            double* d = dry_.channel(ch);
            for (int i = 0; i < n; ++i) {
                const double x = static_cast<double>(src[i]);
                w[i] = x;
                d[i] = x;
            }
        }
        // L'entrée de ce morceau est entièrement copiée avant toute écriture en
        // sortie : le traitement en place (inputs == outputs) est donc sûr.
        processChunk(n);
        for (int ch = 0; ch < kNumChannels; ++ch) {
            const double* w = work_.channel(ch);
            Sample* dst = outputs[ch] + offset;
            for (int i = 0; i < n; ++i) dst[i] = static_cast<Sample>(w[i]);
        }
        offset += n;
    }
}

} // namespace omnis
