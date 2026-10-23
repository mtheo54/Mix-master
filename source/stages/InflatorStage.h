// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/InflatorStage.h — Étage 3 : Inflator (densité et chaleur).
//
//  Rendre un son plus dense et plus « plein » sans faire monter ses crêtes :
//  une courbe qui donne du gain aux passages faibles et s'aplatit en haut,
//      g(u) = u + (u − u^m) / (m − 1)      0 ≤ u ≤ 1,     g(u) = 1 au-delà
//  (impaire : f(x) = signe(x)·g(|x|)). g(1) = 1 et g'(1) = 0 : aucune
//  cassure, la crête reste à sa place. Pente à l'origine m / (m − 1) : de +6 dB
//  (m = 2, « Curve » −50, très dense et doux) à +1.6 dB (m = 6, « Curve » +50,
//  presque linéaire puis franc). La chaleur (« Warmth ») décentre la courbe :
//  f(x + b) − f(b) ajoute des harmoniques paires ; le continu qu'elle crée est
//  retiré (bloqueur de DC à 5 Hz).
//
//  Deux bandes, réglées séparément : la 808 (sous « Split », 120 Hz par
//  défaut) et le reste du mix. Séparation à phase linéaire complémentaire,
//  comme le clipper (haut = entrée retardée − bas).
//
//  En parallèle, et exact au repos :
//      sortie = sec + Σ_bande effet · [ (k0 − 1)·bande + harmoniques(bande) ]
//  - (k0 − 1)·bande : le gain de densité, au taux de base, parfaitement aligné
//    sur le signal sec (k0 = pente de la courbe au point de repos) ;
//  - harmoniques : f(drive·u)/drive − k0·u, calculé suréchantillonné (1x … 16x,
//    défaut 4x) puis redescendu : seule la partie non linéaire passe par les
//    filtres d'oversampling, le son d'origine ne les traverse jamais.
//  Effets à 0 % (défaut) : sortie = entrée retardée, au bit près.
//
//  Latence constante : N/2 + B de la séparation + retard de phase 16x arrondi.
// =============================================================================
#pragma once

#include "ClipperStage.h"

#include "core/DelayLine.h"
#include "core/OnePoleSmoother.h"
#include "dsp/FirKernelBank.h"
#include "dsp/Oversampler.h"
#include "dsp/StereoConvolver.h"
#include "engine/Stage.h"

#include <array>
#include <vector>

namespace omnis {

/// Courbe de l'Inflator (sans état) : exposée pour les tests.
struct InflatorCurve {
    /// Exposant m ∈ [2, 6] depuis « Curve » (−50 … +50).
    static double exponentFor(double curve) noexcept;
    /// g(|x|) avec le signe de x.
    static double shape(double x, double m) noexcept;
    /// Pente au point x (dérivée de shape).
    static double slope(double x, double m) noexcept;
};

class InflatorStage final : public Stage {
public:
    InflatorStage();
    ~InflatorStage() override;

    [[nodiscard]] const char* name() const noexcept override { return "Inflator"; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(double* const* channels, int numSamples) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override { return latency_; }
    void setParameter(params::ParamId id, double plainValue, bool immediate) noexcept override;
    void setRealtime(bool realtime) noexcept override { realtime_ = realtime; }

    /// FIR de séparation : la séparation ne décide que de ce qui passe dans
    /// chaque courbe (le son sec ne la traverse pas), 4096 taps à 48 kHz suffisent.
    static int splitKernelLengthFor(double sampleRate) noexcept;

private:
    using SplitBank = dsp::FirKernelBank<SplitKernelRequest, SplitKernelDesigner>;

    struct Band {
        dsp::Oversampler oversampler;
        std::array<double, 2> dcX1{}, dcY1{};
        std::array<std::vector<double>, 2> buffer;    ///< signal de la bande, taux de base
        std::array<std::vector<double>, 2> harmonics; ///< partie non linéaire, redescendue
    };

    void applyOversamplingChoice() noexcept;
    void splitFrameBoundary() noexcept;
    void harmonicsOf(Band& band, int numSamples) noexcept;

    double sampleRate_ = 48000.0;
    bool realtime_ = false;
    int latency_ = 0;
    int splitLatency_ = 0;
    int requestedStages_ = 2;
    std::array<int, dsp::Oversampler::kMaxStages + 1> osDelayForStages_{};
    int osMaxDelay_ = 0;
    double dcCoefficient_ = 0.0;

    OnePoleSmoother lowEffect_, highEffect_; ///< 0..1
    OnePoleSmoother drive_;                  ///< gain linéaire
    OnePoleSmoother exponent_;               ///< m
    OnePoleSmoother bias_;                   ///< décentrage (chaleur)
    OnePoleSmoother output_;                 ///< gain linéaire
    OnePoleSmoother splitLog_;               ///< ln(f_split)

    dsp::StereoConvolver splitter_;
    SplitBank bank_;
    SplitBank::Slot* activeSlot_ = nullptr;
    SplitBank::Slot* fadingSlot_ = nullptr;
    std::vector<double> fadeRamp_;
    int framePos_ = 0;
    std::array<std::vector<double>, 2> inputDelay_;
    int inputDelayPos_ = 0;

    Band low_, high_;
    std::array<std::vector<double>, 2> dry_;
    DelayLine align_;   ///< sec + bandes, retardés comme les harmoniques (6 canaux)
    DelayLine padding_; ///< jusqu'au pire cas 16x : latence constante

    // Paramètres au taux de base pour le bloc courant.
    std::vector<double> eLow_, eHigh_, drives_, exponents_, biases_, restShape_, restSlope_, outs_;
    double restBias_ = -1.0, restExponent_ = -1.0, restShapeValue_ = 0.0, restSlopeValue_ = 1.0; ///< point de repos en cache
};

} // namespace omnis
