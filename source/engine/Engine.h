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
//  Rack : chaque étage a son interrupteur (paramètre « … Module ») : éteint,
//  il rend son entrée retardée de sa propre latence (bit-exact, fondu de 20 ms,
//  l'étage continue de tourner pour rester chaud). L'ordre des étages
//  (ChainOrder, rang d'une permutation) change en passant brièvement au son
//  sec : fondu, permutation, attente d'une latence complète, retour.
//  Écoute Delta d'un module : seulement ce qu'il change (sa sortie moins son
//  entrée), alignée sur la fin de la chaîne. Gain Match : la sortie écoutée est
//  ramenée à la sonie court terme de l'entrée (lissée) ; ni l'un ni l'autre
//  ne touche aux mesures.
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
#include <cmath>
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

    /// Non temps réel, avant prepare(). L'ordre d'ajout est l'ordre de la chaîne
    /// par défaut ; `onParameter` : l'interrupteur de l'étage (aucun : toujours allumé).
    void appendStage(std::unique_ptr<Stage> stage, params::ParamId onParameter = params::ParamId::Bypass);
    [[nodiscard]] int numStages() const noexcept { return static_cast<int>(stages_.size()); }

    /// Non temps réel, avant prepare(). Défaut : false (rendu hors ligne
    /// déterministe). Le plug-in passe à true hors mode kOffline de l'hôte.
    void setRealtimeMode(bool realtime) noexcept;

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

    /// Temps réel : mesure seulement (sonie, crêtes, corrélation) un signal traité
    /// ailleurs — la sortie d'un stem, qui ne passe pas par la chaîne du master.
    template <typename Sample>
    void meterOnly(const Sample* const* channels, int numSamples) noexcept;

    /// Latence totale de la chaîne, constante après prepare().
    [[nodiscard]] int latencySamples() const noexcept { return latency_; }
    [[nodiscard]] double sampleRate() const noexcept { return spec_.sampleRate; }
    [[nodiscard]] int maxBlockSize() const noexcept { return spec_.maxBlockSize; }

    [[nodiscard]] MeterReadout meters() const noexcept;

    /// Remet à zéro sonie intégrée, LRA et crêtes mémorisées.
    void resetLoudnessHistory() noexcept;

    /// Nombre de fois où un résultat non fini a été intercepté en sortie (états
    /// vidés, sortie mise à zéro le temps du bloc). 0 en fonctionnement normal.
    [[nodiscard]] int faultCount() const noexcept { return faults_; }

    /// Entrée de l'hôte : NaN et infinis -> 0, amplitude bornée à ±10 000 (+80 dBFS).
    /// Un seul NaN venu d'un plug-in précédent empoisonnerait sinon les filtres
    /// récursifs et la convolution jusqu'à la remise à zéro.
    static double sanitizeInput(double x) noexcept {
        constexpr double kLimit = 1.0e4;
        return std::isfinite(x) ? std::clamp(x, -kLimit, kLimit) : 0.0;
    }

    /// Accès en lecture aux mesureurs (outils hors ligne, tests).
    [[nodiscard]] const analysis::LoudnessMeter& loudnessMeter() const noexcept { return loudness_; }

    /// Ordre courant des étages (indices d'ajout), et écart de sonie compensé par Gain Match (dB).
    [[nodiscard]] const std::vector<int>& stageOrder() const noexcept { return order_; }
    [[nodiscard]] double gainMatchDb() const noexcept { return gainToDbSafe(matchGain_.current()); }
    /// Un étage par son nom (outils, interface) : nullptr si absent.
    [[nodiscard]] Stage* stage(int index) noexcept { return index >= 0 && index < numStages() ? stages_[static_cast<std::size_t>(index)].get() : nullptr; }

private:
    void applyParameter(params::ParamId id, double plainValue, bool immediate) noexcept;
    void processChunk(int numSamples) noexcept;
    void recoverFromFault() noexcept;
    static void applyGain(OnePoleSmoother& gain, double* const* channels, int numSamples) noexcept;
    void applyMonitorMatrix(int numSamples) noexcept;
    void setMonitorTargets(int mode, bool immediate) noexcept;
    void updateReorder(int n) noexcept;
    void updateDelta() noexcept;
    static double gainToDbSafe(double g) noexcept { return g > 0.0 ? 20.0 * std::log10(g) : -120.0; }

    /// Un emplacement du rack : l'interrupteur et le chemin sec de l'étage.
    struct Slot {
        params::ParamId onId = params::ParamId::Bypass;
        OnePoleSmoother on;
        DelayLine dry;
        int latency = 0;
    };
    std::vector<Slot> slots_;
    std::vector<int> order_;      ///< indices d'étages dans l'ordre de traitement
    std::vector<int> wantedOrder_;
    OnePoleSmoother orderMix_;    ///< 0 pendant une permutation
    int orderHold_ = 0;           ///< échantillons de son sec restants après permutation
    AudioBuffer stageIn_;         ///< entrée retardée de l'étage courant
    // Delta : 0 = éteint, sinon 1 + indice d'étage.
    int deltaWanted_ = 0, deltaActive_ = 0;
    OnePoleSmoother deltaMix_;
    AudioBuffer delta_;
    DelayLine deltaDelay_;
    // Gain Match.
    bool gainMatch_ = false;
    analysis::LoudnessMeter inputLoudness_;
    OnePoleSmoother matchGain_;
    OnePoleSmoother matchMix_;
    ProcessSpec spec_{};
    bool prepared_ = false;
    int latency_ = 0;
    int faults_ = 0;

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
void Engine::meterOnly(const Sample* const* channels, int numSamples) noexcept {
    if (!prepared_ || numSamples <= 0) return;
    for (int offset = 0; offset < numSamples;) {
        const int n = std::min(numSamples - offset, spec_.maxBlockSize);
        for (int ch = 0; ch < kNumChannels; ++ch) {
            double* w = work_.channel(ch);
            for (int i = 0; i < n; ++i) w[i] = sanitizeInput(static_cast<double>(channels[ch][offset + i]));
        }
        double* const* w = work_.channels();
        loudness_.process(w, n);
        truePeak_.process(w, n);
        stereo_.process(w[0], w[1], n);
        offset += n;
    }
}

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
                const double x = sanitizeInput(static_cast<double>(src[i]));
                w[i] = x;
                d[i] = x;
            }
        }
        // L'entrée de ce morceau est entièrement copiée avant toute écriture en
        // sortie : le traitement en place (inputs == outputs) est donc sûr.
        processChunk(n);
        bool fault = false;
        for (int ch = 0; ch < kNumChannels; ++ch) {
            const double* w = work_.channel(ch);
            Sample* dst = outputs[ch] + offset;
            for (int i = 0; i < n; ++i) {
                const double y = w[i];
                fault = fault || !std::isfinite(y);
                dst[i] = static_cast<Sample>(std::isfinite(y) ? y : 0.0);
            }
        }
        if (fault) recoverFromFault();
        offset += n;
    }
}

} // namespace omnis
