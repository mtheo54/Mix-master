// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/MultibandStage.h — Étage 4 : dynamique multibande (« tenir la 808 »).
//
//  Pourquoi (calibration module 4, §5) : les crêtes de la prod sont celles de
//  la 808, et le clipper + limiteur la réduisaient de 6.4 dB contre 3.9 dB
//  pour les médiums. La sonie de la référence vient d'une 808 TENUE dans le
//  temps (variation 36 dB contre 52). On la tient ici, avant toute
//  saturation : ses coups les plus forts sont baissés, un gain de
//  compensation rend l'énergie de la bande -> même équilibre tonal, crêtes
//  plus basses, et le limiteur n'a plus à l'écraser.
//
//  Séparation à phase linéaire, reconstruction exacte : trois FIR de 8192
//  taps à partir de passe-bas Linkwitz-Riley à phase nulle L_k = 1/(1+(f/f_k)⁴)
//      B0 = L1      B1 = L2 − L1      B2 = L3 − L2      B3 = x retardé − Σ
//  (Σ B = 1 exactement : ratio 1 et compensation 0 = retard pur.)
//
//  Par bande, liée en stéréo :
//    - détecteur RMS sur une fenêtre glissante TOURNÉE VERS L'AVANT ([s, s+W]
//      pour l'échantillon s, grâce au retard de l'audio) : au début d'un coup
//      elle est déjà pleine (une fenêtre centrée était à moitié dans le
//      silence et laissait passer l'attaque). W = 50 ms pour le sub (1.5 cycle
//      d'une 808 à 30 Hz ; avec 25 ms la détection ondulait et compressait
//      0.5 dB de trop), 20 ms au-dessus ;
//    - courbe à genou doux de 6 dB ; seuil en dBFS RMS (un sinus à 0 dBFS
//      crête = −3 dBFS RMS) ;
//    - gain requis -> dsp::LookaheadGain (anticipation 10 ms, minimum glissant
//      + rampe) : le gain atteint sa valeur AVANT le coup, sans pointe ;
//    - attaque > 10 ms : lissage supplémentaire de la baisse de gain, qui
//      laisse volontairement passer le transitoire (punch) ;
//    - relâchement : remontée exponentielle du gain.
//  Une première version (détecteur RMS à une constante de 30 ms, gain lissé
//  en dB, anticipation 10 ms) laissait passer les attaques de 808 : le
//  détecteur réagissait plus lentement que l'anticipation (mesuré en test).
//  Conséquence de la fenêtre vers l'avant : elle voit la fin d'une note avant
//  qu'elle arrive ; le relâchement reste donc lent (≥ 80 ms) pour ne pas pomper.
//
//  Latence : N/2 + B + 10 ms + 50 ms = 4096 + 256 + 2646 à 44.1 kHz (159 ms).
// =============================================================================
#pragma once

#include "core/OnePoleSmoother.h"
#include "dsp/FirKernelBank.h"
#include "dsp/LookaheadGain.h"
#include "dsp/LinearPhaseDesigner.h"
#include "dsp/StereoConvolver.h"
#include "core/Decibels.h"
#include "engine/Stage.h"

#include <array>
#include <vector>

namespace omnis {

struct MbKernelRequest {
    double splitLow = 120.0, splitMid = 800.0, splitHigh = 5000.0;
    int band = 0; ///< 0, 1 ou 2 (la bande 3 est le complément)

    [[nodiscard]] bool nearlyEquals(const MbKernelRequest& o) const noexcept;
    [[nodiscard]] double magnitude(double frequency) const noexcept;
};

/// Même réponse de bande sur L et R.
class MbKernelDesigner {
public:
    void prepare(double sampleRate, int kernelLength, int blockSize) { designer_.prepare(sampleRate, kernelLength, blockSize); }
    void design(const MbKernelRequest& r, dsp::StereoConvolver::Kernel& out) noexcept {
        const auto mag = [&](double f) { return r.magnitude(f); };
        designer_.design(mag, mag, out);
    }

private:
    dsp::LinearPhaseDesigner designer_;
};

class MultibandStage final : public Stage {
public:
    MultibandStage();
    ~MultibandStage() override;

    [[nodiscard]] const char* name() const noexcept override { return "Multi-Band Dynamics"; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(double* const* channels, int numSamples) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override { return latency_; }
    void setParameter(params::ParamId id, double plainValue, bool immediate) noexcept override;
    void setRealtime(bool realtime) noexcept override { realtime_ = realtime; }

    static constexpr double kLookaheadMs = 10.0; ///< rampe du gain
    static constexpr double kRmsWindowMs[params::kMbBands] = {50.0, 20.0, 20.0, 20.0}; ///< fenêtre du détecteur
    static constexpr double kKneeDb = 6.0;
    static int kernelLengthFor(double sampleRate) noexcept;

    /// Réduction de gain courante d'une bande (dB, ≤ 0) — mesures / tests.
    [[nodiscard]] double gainReductionDb(int band) const noexcept { return gainToDb(bands_[static_cast<std::size_t>(band)].gain); }

private:
    using Bank = dsp::FirKernelBank<MbKernelRequest, MbKernelDesigner>;
    static constexpr int kFiltered = 3;

    struct Band {
        double thresholdDb = 0.0, ratio = 1.0, attackMs = 30.0, releaseMs = 250.0;
        /// Puissance sous laquelle le gain est exactement 0 dB (début du genou), avec
        /// une marge de 1e-9 : la comparaison en puissance évite log10 et pow.
        double kneeStartPower = 0.0;
        void updateKneeStart() noexcept { kneeStartPower = dbToPower(thresholdDb - 0.5 * kKneeDb) * (1.0 - 1e-9); }
        OnePoleSmoother makeup; ///< gain linéaire
        double attackCoeff = 1.0, releaseCoeff = 0.0;
        std::vector<double> powerRing; ///< fenêtre RMS
        std::vector<double> powerDelay; ///< recentrage sur la fenêtre la plus longue
        int ringPos = 0, delayPos = 0;
        double powerSum = 0.0;
        dsp::LookaheadGain lookahead;
        double gain = 1.0; ///< réduction lissée (linéaire)
        std::array<std::vector<double>, 2> delay; ///< anticipation
    };

    [[nodiscard]] MbKernelRequest requestFor(int band) const noexcept;
    void updateTimeConstants(Band& b) noexcept;
    void frameBoundary() noexcept;
    [[nodiscard]] double computeGainDb(const Band& b, double levelDb) const noexcept;

    double sampleRate_ = 48000.0;
    bool realtime_ = false;
    int latency_ = 0, splitLatency_ = 0, lookahead_ = 0, audioDelay_ = 0;

    OnePoleSmoother splitLog_[3]; ///< ln(f) des trois séparations

    std::array<dsp::StereoConvolver, kFiltered> convolvers_;
    std::array<Bank, kFiltered> banks_;
    std::array<Bank::Slot*, kFiltered> active_{}, fading_{};
    std::vector<double> fadeRamp_;
    int framePos_ = 0;
    std::array<std::vector<double>, 2> inputDelay_; ///< pour la bande complémentaire
    int inputDelayPos_ = 0, lookPos_ = 0;

    std::array<Band, params::kMbBands> bands_;
};

} // namespace omnis
