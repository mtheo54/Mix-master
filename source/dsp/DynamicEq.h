// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/DynamicEq.h — EQ dynamique à 4 bandes, en Mid / Side, sans latence.
//
//  Une bande ne corrige que lorsqu'elle DÉPASSE son équilibre habituel dans le
//  mix : son niveau rapporté au niveau de tout le signal (rapport mesuré par la
//  bande elle-même), comparé à sa moyenne lente (~4 s). Le seuil est donc en dB
//  au-dessus de l'habitude, et ne dépend ni du volume d'entrée ni de la sonie
//  de la prod : la sifflante, la note de 808 qui gonfle, le hi-hat qui pique
//  sont pris au moment où ils sortent du lot ; le reste du temps, rien.
//
//  Filtre : SVF TPT (Cytomic) par bande et par canal. La même sortie passe-bande
//  sert à la détection et à la correction :
//
//      bp = k·v1 (gain 0 dB au centre)      y = x + (G − 1)·bp
//
//  G = 1 -> y = x au bit près (bande inactive, ou rien à corriger). Pas de
//  coefficient recalculé par échantillon : le gain glisse linéairement entre
//  deux points de contrôle (toutes les 32 trames), la fréquence et le Q glissent
//  en log à chaque point de contrôle.
//
//  Gain : le dépassement d au-dessus du seuil (coude doux de 2 dB), retiré en
//  entier (la bande revient à son seuil ; le rapport bande / mix plafonne déjà
//  à 0 dB, il comprime par nature), borné par la plage. Plage < 0 : la bande
//  est retenue ; > 0 : elle ressort (accentuer une caisse claire qui claque).
//
//  Temps réel : aucune allocation, aucun verrou ; setParameter() est appelé
//  depuis le thread audio.
// =============================================================================
#pragma once

#include "params/Parameters.h"

#include <array>

namespace omnis::dsp {

class DynamicEq {
public:
    static constexpr int kBands = params::kDynBands;
    static constexpr int kControlPeriod = 32;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParameter(params::ParamId id, double plain) noexcept;

    /// Au moins une bande allumée ou en train de revenir à 0 dB.
    [[nodiscard]] bool active() const noexcept;

    /// Un échantillon Mid / Side, modifié sur place.
    void process(double& mid, double& side) noexcept;

    /// Gain appliqué en ce moment par la bande (dB, 0 si éteinte).
    [[nodiscard]] double gainDb(int band) const noexcept;

    struct BandSettings {
        bool on = false;
        double hz = 1000.0, q = 1.0;
        double rangeDb = -4.0;     ///< < 0 : retient ; > 0 : fait ressortir
        double thresholdDb = 4.0;  ///< au-dessus de l'équilibre habituel
        params::DynChannel channel = params::DynChannel::Stereo;
    };
    [[nodiscard]] const BandSettings& settings(int band) const noexcept { return bands_[static_cast<std::size_t>(band)].set; }

private:
    struct Svf {
        double ic1 = 0.0, ic2 = 0.0;
    };
    struct Band {
        BandSettings set;
        double hz = 1000.0, q = 1.0; ///< valeurs lissées
        double a1 = 1.0, a2 = 0.0, a3 = 0.0, k = 1.0;
        Svf mid, side;
        double envBand = 0.0, envAll = 0.0; ///< puissances (bande, tout le signal)
        double habitDb = 0.0;
        bool habitKnown = false;
        double gain = 1.0, gainStep = 0.0; ///< gain linéaire et pente vers la cible
        bool running = false;              ///< filtre actif (allumée ou retour à 0 dB)
    };

    void control(Band& b) noexcept;
    void updateCoefficients(Band& b) const noexcept;
    void updateTimes() noexcept;

    std::array<Band, kBands> bands_{};
    double sampleRate_ = 48000.0;
    double attackMs_ = 5.0, releaseMs_ = 150.0;
    double attackCoef_ = 0.0, releaseCoef_ = 0.0, habitCoef_ = 0.0;
    int countdown_ = kControlPeriod;
};

} // namespace omnis::dsp
