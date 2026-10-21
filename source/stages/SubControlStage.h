// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/SubControlStage.h — Étage 1 : Sub Control & Mono Maker.
//
//  Fonctions : Low Cut (Butterworth 24 dB/oct, 5–60 Hz), Mono Maker M/S (« EQ
//  elliptique » de la gravure vinyle : le Side est retiré sous f_mono), Sub Gain
//  (plateau grave sur le Mid), synthé sous-harmonique (octave basse).
//
//  Deux chemins tournent en permanence et sont fondus selon « Sub Phase » :
//
//   LINEAR (défaut) — FIR à phase linéaire, convolution partitionnée UPOLS.
//     Mid et Side passent chacun dans UN noyau qui réunit low cut, Mono Maker
//     et Sub Gain (voir SubKernelBank.h). Aucune rotation de phase : la forme
//     d'onde du sub et des transitoires est conservée.
//     Mesure qui a motivé ce choix (calibration module 2, fichiers utilisateur) :
//     sur la référence 2hollis (déjà écrêtée), la version IIR fait monter le true
//     peak de +7.8 dB, la version linéaire de +0.6 dB pour le même résultat mono.
//
//   ANALOG — IIR TPT à latence nulle (low cut BW4 sur L/R, crossover LR4/LR8 sur
//     le Side + passe-tout jumeau sur le Mid, plateau SVF). Caractère « console »
//     (rotation de phase grave), retardé pour garder une latence constante.
//
//  Le synthé sous-harmonique (diviseur d'octave, type dbx 120) est appliqué sur
//  la sortie, commun aux deux chemins :
//     bande 45–100 Hz du Mid -> bascule à hystérésis (signe inversé à chaque
//     cycle -> carrée à f/2) × enveloppe -> passe-bas 24 dB/oct 55 Hz + passe-haut
//     20 Hz -> ajouté au Mid. À 100 %, l'octave basse sort au niveau de la
//     fondamentale (calibré sur un sinus à 80 Hz).
//
//  Latence : N/2 + B (N taps du FIR, B taille de partition), constante pour une
//  fréquence d'échantillonnage donnée : 8704 échantillons (197 ms) à 44.1 kHz.
//  Les noyaux sont recalculés à chaque frontière de trame quand un réglage a
//  bougé (thread de calcul en temps réel, calcul immédiat hors ligne) et le
//  changement est fondu sur une trame complète (cosinus surélevé).
// =============================================================================
#pragma once

#include "SubKernelBank.h"

#include "core/OnePoleSmoother.h"
#include "dsp/Crossover.h"
#include "dsp/StereoConvolver.h"
#include "dsp/SvfFilter.h"
#include "engine/Stage.h"

#include <array>
#include <cstdint>
#include <vector>

namespace omnis {

class SubControlStage final : public Stage {
public:
    SubControlStage();
    ~SubControlStage() override;

    [[nodiscard]] const char* name() const noexcept override { return "Sub Control & Mono Maker"; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(double* const* channels, int numSamples) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override { return latency_; }
    void setParameter(params::ParamId id, double plainValue, bool immediate) noexcept override;
    void setRealtime(bool realtime) noexcept override { realtime_ = realtime; }

    static constexpr int kControlInterval = 32;

    /// Taille du FIR et des partitions pour une fréquence donnée (≈ 0.37 s de noyau).
    static int kernelLengthFor(double sampleRate) noexcept;
    static int blockSizeFor(double sampleRate) noexcept;

private:
    void updateAnalogCoefficients() noexcept;
    [[nodiscard]] bool anySmoothing() const noexcept;
    [[nodiscard]] SubKernelRequest currentRequest() const noexcept;
    void frameBoundary() noexcept;
    double processSynth(double mid) noexcept;

    double sampleRate_ = 48000.0;
    bool realtime_ = false;
    int latency_ = 0;
    std::uint64_t clock_ = 0;
    bool analogDirty_ = true;

    // Paramètres lissés (fréquences lissées en log pour un glissement musical).
    OnePoleSmoother lowCutLog_, monoFreqLog_, subFreqLog_;
    OnePoleSmoother monoAmount_; ///< 0..1
    OnePoleSmoother slopeMix_;   ///< 0 = LR4, 1 = LR8
    OnePoleSmoother subGainDb_;
    OnePoleSmoother synthAmount_; ///< 0..1
    OnePoleSmoother analogMix_;   ///< 0 = Linear, 1 = Analog

    // ---- Chemin LINEAR ------------------------------------------------------
    dsp::StereoConvolver convolver_;
    SubKernelBank bank_;
    SubKernelBank::Slot* activeSlot_ = nullptr;
    SubKernelBank::Slot* fadingSlot_ = nullptr; ///< ancien noyau pendant le fondu
    std::vector<double> fadeRamp_;
    int framePos_ = 0;

    // ---- Chemin ANALOG ------------------------------------------------------
    std::array<dsp::SvfCoefficients, 2> lowCut_{};
    std::array<std::array<dsp::SvfState, 2>, 2> lowCutState_{};
    dsp::LinkwitzRileyCrossover<1> sideXo4_, sideXo8_;
    dsp::LinkwitzRileyAllpass<1> midAp4_, midAp8_;
    dsp::SvfCoefficients subShelf_{};
    dsp::SvfState subShelfState_{};
    std::vector<double> analogDelayMid_, analogDelaySide_; ///< alignement sur la latence FIR
    int analogDelayPos_ = 0;

    // ---- Synthé sous-harmonique ---------------------------------------------
    dsp::SvfCoefficients synthInHp_{}, synthInLp_{}, synthOutLp1_{}, synthOutLp2_{}, synthOutHp_{};
    dsp::SvfState synthInHpS_{}, synthInLpS_{}, synthOutLp1S_{}, synthOutLp2S_{}, synthOutHpS_{};
    double envAttack_ = 0.0, envRelease_ = 0.0;
    double envelope_ = 0.0;
    bool armed_ = false;
    bool polarity_ = false;
};

} // namespace omnis
