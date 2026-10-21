// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/ClipperStage.h — Étage 5 : Smart Wave-Shaping / Mastering Clipper.
//
//   base ─► suréchantillonnage (1x … 16x, défaut 8x, demi-bandes HIIR phase min.)
//        ─► u = x · drive / plafond ─► courbe ADAA (Hard / Soft Knee / Diode)
//        ─► × plafond ─► sous-échantillonnage ─► retard d'alignement
//  (composante Diode seule : bloqueur de DC 3 Hz au taux suréchantillonné)
//
//  Le clipper écrase les crêtes brèves (transitoires de kick, attaque de 808)
//  AVANT le limiteur : c'est ce qui permet les masters à −5/−4 LUFS du style
//  2hollis sans pompage, le limiteur n'ayant plus qu'à rattraper quelques
//  dixièmes de dB. Le repliement est combattu deux fois : suréchantillonnage
//  (les harmoniques créées restent au-dessus de la bande audio avant d'être
//  filtrées) et ADAA (moins d'harmoniques extrêmes générées).
//
//  Les trois courbes sont calculées en permanence et fondues selon le mode :
//  changer de mode pendant la lecture ne produit pas de clic.
//
//  Latence constante : retard de phase à 1 kHz de la chaîne 16x (le pire cas),
//  arrondi à l'échantillon ; un retard entier complète les facteurs plus bas.
// =============================================================================
#pragma once

#include "core/DelayLine.h"
#include "core/OnePoleSmoother.h"
#include "dsp/Oversampler.h"
#include "dsp/Waveshaper.h"
#include "engine/Stage.h"

#include <array>
#include <vector>

namespace omnis {

class ClipperStage final : public Stage {
public:
    ClipperStage();

    [[nodiscard]] const char* name() const noexcept override { return "Mastering Clipper"; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(double* const* channels, int numSamples) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override { return latency_; }
    void setParameter(params::ParamId id, double plainValue, bool immediate) noexcept override;

private:
    void applyOversamplingChoice() noexcept;
    void updateDcCoefficient() noexcept;

    double sampleRate_ = 48000.0;
    int latency_ = 0;
    int requestedStages_ = 3;
    std::array<int, dsp::Oversampler::kMaxStages + 1> paddingForStages_{};

    OnePoleSmoother drive_;   ///< gain linéaire
    OnePoleSmoother ceiling_; ///< gain linéaire
    OnePoleSmoother knee_;    ///< 0..1
    std::array<OnePoleSmoother, 3> modeWeight_; ///< Hard, Soft, Diode

    dsp::Oversampler oversampler_;
    std::array<dsp::AdaaClipper, 2> clippers_{};
    DelayLine padding_;
    std::array<double, 2> dcX1_{}, dcY1_{};
    double dcCoefficient_ = 0.0;

    // Paramètres au taux de base pour le bloc courant (tenus sur les sous-échantillons).
    std::vector<double> gainIn_, gainOut_, knees_, wHard_, wSoft_, wDiode_;
};

} // namespace omnis
