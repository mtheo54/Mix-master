// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/LimiterStage.h — Étage 6 : Lookahead Brickwall Limiter (true peak).
//
//  Calculateur de gain à garantie « brickwall » (structure min-hold + moyenne) :
//
//   1. Détection true peak, liée stéréo (interpolation x4, même FIR Kaiser que
//      le TruePeakMeter, retard D = 24 échantillons). Pas de mode « club » en
//      sample peak : la propreté prime, le plafond vient de la demande.
//   2. Gain requis  r[n] = min(1, plafond / crête[n]).
//   3. Relâchement : e[n] = r[n] si r[n] < e[n−1] (attaque instantanée), sinon
//      e[n] = e[n−1] + a_rel·(r[n] − e[n−1]) (remontée exponentielle).
//      Release adaptatif : a_rel = a_base / (1 + 4·amount·activité), où
//      l'activité est la part du temps passé en réduction de gain (τ = 500 ms).
//      Crête isolée -> release rapide (transparence) ; limitation soutenue
//      (808 longue, refrain dense) -> jusqu'à 5x plus lent, le gain ne suit
//      plus chaque cycle du sub (pas de modulation des médiums).
//   4. Minimum glissant sur L + 1 échantillons (L = lookahead de 10 ms : la
//      rampe d'attaque, plus longue, crée moins de bandes latérales).
//   5. Moyenne glissante (fenêtre rectangulaire) sur L + 1 échantillons :
//      la rampe d'attaque dure exactement le lookahead, sans clic.
//   6. Gain appliqué au signal retardé de L + D.
//  Garantie : pour une crête à l'instant n0, toutes les valeurs moyennées à
//  l'instant n0 + L sont <= e[n0] (elles sont issues du minimum glissant qui
//  contient n0) : la moyenne l'est aussi, la crête ne peut pas dépasser.
//
//  Le gain varie lentement devant la fenêtre d'interpolation
//  (rampe de 10 ms contre 48 échantillons) : la crête vraie de sortie reste sous
//  le plafond à quelques centièmes de dB près ; une marge de 0.05 dB est prise.
//
//  Avant la détection, deux étages optionnels (neutres à 0, bit-exact) :
//   - Punch : un suiveur rapide et un suiveur lent du niveau (lié stéréo) ;
//     gain = (rapide / lent)^(2,5·Punch), borné à ±9 dB : les attaques montent, les
//     queues qui retombent descendent, le soutenu stable ne bouge pas. Le
//     limiteur remet les crêtes au plafond : le contraste attaque / corps reste.
//   - Upward : les passages calmes montent jusqu'à `Upward` dB (niveau RMS lié,
//     relatif au plafond : rien à moins de 12 dB du plafond, tout le gain à 36 dB
//     dessous, rien sous le bruit de fond à 70 dB) : les crêtes ne bougent pas.
//  Lien stéréo : gain requis par canal r_c et lié r = min(r_g, r_d) ; chaque canal
//  reçoit r·(r_c / r)^u, u = indépendance (Unlink) dosée entre attaques et
//  soutenu selon le détecteur de Punch. r·(r_c/r)^u <= r_c : la garantie tient
//  canal par canal. u = 0 : exactement le limiteur lié.
//
//  Latence : L + D, constante.
// =============================================================================
#pragma once

#include "analysis/TruePeakMeter.h"
#include "core/OnePoleSmoother.h"
#include "dsp/LookaheadGain.h"
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

    static constexpr double kLookaheadMs = 10.0;
    static constexpr double kTruePeakMarginDb = 0.05;

    /// Réduction de gain courante (dB, <= 0), pour les mesures.
    [[nodiscard]] double gainReductionDb() const noexcept { return lastGainDb_; }

private:
    /// Crêtes vraies des deux canaux.
    void detect(double l, double r, double& peakL, double& peakR) noexcept;

    double sampleRate_ = 48000.0;
    int lookahead_ = 240;
    int latency_ = 264;
    double releaseCoefficient_ = 0.0;
    double releaseMs_ = 60.0;
    double autoRelease_ = 1.0;      ///< 0..1
    double activity_ = 0.0;         ///< part du temps en réduction de gain
    double activityCoefficient_ = 0.0;

    OnePoleSmoother inputGain_;
    OnePoleSmoother ceiling_;

    // Détecteur true peak (historique miroir par canal).
    std::array<std::array<double, analysis::TruePeakMeter::kTapsPerPhase>, analysis::TruePeakMeter::kOversampling> tpCoefficients_{};
    std::array<std::array<double, 2 * analysis::TruePeakMeter::kTapsPerPhase>, 2> tpHistory_{};
    int tpPos_ = 0;

    dsp::LookaheadGain gainSmoother_, gainSmootherRight_;

    // Punch, upward, lien stéréo.
    OnePoleSmoother punch_, upwardDb_, unlinkTransient_, unlinkSustain_;
    double fastEnv_ = 0.0, slowEnv_ = 0.0, rmsEnv_ = 0.0, punchGain_ = 1.0, upwardGain_ = 1.0;
    double fastAttack_ = 0.0, fastRelease_ = 0.0, slowAttack_ = 0.0, slowRelease_ = 0.0;
    double rmsAttack_ = 0.0, rmsRelease_ = 0.0, gainGlide_ = 0.0, upGlide_ = 0.0;
    int holdSamples_ = 1200, fastHold_ = 0, slowHold_ = 0;
    static constexpr double kPunchExponent = 2.5; ///< gain = (rapide / lent)^(2,5 · Punch)
    // Retard du signal.
    std::array<std::vector<double>, 2> delay_;
    int delayPos_ = 0;

    double lastGainDb_ = 0.0;
};

} // namespace omnis
