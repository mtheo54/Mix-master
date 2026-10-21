// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/Oversampler.h — Suréchantillonnage stéréo 1x … 16x, cascade de demi-bandes.
//
//  Chaque doublement est un demi-bande HIIR (Halfband.h). Seul le premier étage
//  (taux de base <-> 2x) doit être raide : il protège la bande audio. Les
//  suivants n'ont qu'à rejeter les images au-dessus de ~0.54·fs et peuvent être
//  bien plus courts :
//      étage   taux          transition tw   coefficients (120 dB)
//      1       fs   -> 2fs   0.02            12   bande passante 0.46·fs (20.3 kHz à 44.1)
//      2       2fs  -> 4fs   0.13            ~6
//      3       4fs  -> 8fs   0.19            ~4
//      4       8fs  -> 16fs  0.22            ~3
//  Phase minimale : aucun pré-écho avant le clipper (le punch de l'attaque est
//  préservé), au prix d'un retard de groupe faible et dépendant de la fréquence ;
//  sa valeur à 1 kHz est mesurée dans prepare() et annoncée comme latence.
//
//  Le facteur peut changer en cours de lecture sans allocation (tous les
//  tampons sont dimensionnés pour 16x) ; les étages réactivés repartent à zéro.
// =============================================================================
#pragma once

#include "Halfband.h"

#include <array>
#include <vector>

namespace omnis::dsp {

class Oversampler {
public:
    static constexpr int kMaxStages = 4; // 16x
    static constexpr int kChannels = 2;

    /// Non temps réel.
    void prepare(int maxBlockSize);
    void reset() noexcept;

    /// 0 = 1x, 1 = 2x, … 4 = 16x. Temps réel.
    void setStages(int stages) noexcept;
    [[nodiscard]] int stages() const noexcept { return stages_; }
    [[nodiscard]] int factor() const noexcept { return 1 << stages_; }

    /// Monte `numSamples` échantillons de base ; renvoie les tampons suréchantillonnés
    /// (numSamples · factor() échantillons par canal).
    double* const* upsample(const double* const* input, int numSamples) noexcept;
    /// Redescend les tampons renvoyés par upsample() (éventuellement modifiés).
    void downsample(double* const* output, int numSamples) noexcept;

    /// Retard de phase à 1 kHz (montée + descente), en échantillons au taux de base.
    [[nodiscard]] static double phaseDelaySamples(int stages, double sampleRate);

    /// Transition relative de chaque étage (voir tableau ci-dessus).
    static constexpr std::array<double, kMaxStages> kTransitions{0.02, 0.13, 0.19, 0.22};
    static constexpr double kAttenuationDb = 120.0;

private:
    int stages_ = 3;
    int maxBlock_ = 0;
    std::array<std::array<Halfband2x, kMaxStages>, kChannels> filters_{};
    // Deux tampons de travail par canal (ping-pong), dimensionnés pour 16x.
    std::array<std::vector<double>, kChannels> bufA_, bufB_;
    std::array<double*, kChannels> result_{};
    std::array<bool, kChannels> resultInA_{};
};

} // namespace omnis::dsp
