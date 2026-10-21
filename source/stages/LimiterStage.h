// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/LimiterStage.h — Étage 6 : Lookahead Brickwall Limiter (true peak).
//
//  Calculateur de gain à garantie « brickwall » (structure min-hold + moyenne) :
//
//   1. Détection, liée stéréo : true peak (interpolation x4, même FIR Kaiser que
//      le TruePeakMeter, retard D = 24 échantillons) ou sample peak.
//   2. Gain requis  r[n] = min(1, plafond / crête[n]).
//   3. Relâchement : e[n] = r[n] si r[n] < e[n−1] (attaque instantanée), sinon
//      e[n] = e[n−1] + a_rel·(r[n] − e[n−1]) (remontée exponentielle).
//   4. Minimum glissant sur L + 1 échantillons (L = lookahead de 5 ms).
//   5. Moyenne glissante (fenêtre rectangulaire) sur L + 1 échantillons :
//      la rampe d'attaque dure exactement le lookahead, sans clic.
//   6. Gain appliqué au signal retardé de L + D.
//  Garantie : pour une crête à l'instant n0, toutes les valeurs moyennées à
//  l'instant n0 + L sont <= e[n0] (elles sont issues du minimum glissant qui
//  contient n0) : la moyenne l'est aussi, la crête ne peut pas dépasser.
//
//  En mode true peak, le gain varie lentement devant la fenêtre d'interpolation
//  (rampe de 5 ms contre 48 échantillons) : la crête vraie de sortie reste sous
//  le plafond à quelques centièmes de dB près ; une marge de 0.05 dB est prise.
//
//  Latence : L + D, constante (le mode sample peak garde le même retard).
// =============================================================================
#pragma once

#include "analysis/TruePeakMeter.h"
#include "core/OnePoleSmoother.h"
#include "engine/Stage.h"

#include <array>
#include <vector>

namespace omnis {

class LimiterStage final : public Stage {
public:
    LimiterStage();

    [[nodiscard]] const char* name() const noexcept override { return "Brickwall Limiter"; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(double* const* channels, int numSamples) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override { return latency_; }
    void setParameter(params::ParamId id, double plainValue, bool immediate) noexcept override;

    static constexpr double kLookaheadMs = 5.0;
    static constexpr double kTruePeakMarginDb = 0.05;

    /// Réduction de gain courante (dB, <= 0), pour les mesures.
    [[nodiscard]] double gainReductionDb() const noexcept { return lastGainDb_; }

private:
    double detect(double l, double r) noexcept;

    double sampleRate_ = 48000.0;
    int lookahead_ = 240;
    int latency_ = 264;
    bool truePeak_ = true;
    double releaseCoefficient_ = 0.0;
    double releaseMs_ = 60.0;

    OnePoleSmoother inputGain_;
    OnePoleSmoother ceiling_;

    // Détecteur true peak (historique miroir par canal).
    std::array<std::array<double, analysis::TruePeakMeter::kTapsPerPhase>, analysis::TruePeakMeter::kOversampling> tpCoefficients_{};
    std::array<std::array<double, 2 * analysis::TruePeakMeter::kTapsPerPhase>, 2> tpHistory_{};
    int tpPos_ = 0;

    double envelope_ = 1.0;
    // Minimum glissant : file monotone (valeurs croissantes) sur un anneau.
    std::vector<double> minValues_;
    std::vector<long long> minIndices_;
    int minHead_ = 0, minCount_ = 0;
    long long sampleIndex_ = 0;
    // Moyenne glissante.
    std::vector<double> boxRing_;
    int boxPos_ = 0;
    double boxSum_ = 0.0;
    // Retard du signal.
    std::array<std::vector<double>, 2> delay_;
    int delayPos_ = 0;

    double lastGainDb_ = 0.0;
};

} // namespace omnis
