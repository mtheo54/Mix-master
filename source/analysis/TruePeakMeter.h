// =============================================================================
//  Abrasion Labs // OMNIS
//  analysis/TruePeakMeter.h — Mesure de crête vraie (ITU-R BS.1770-4, Annexe 2).
//
//  Principe : sur-échantillonner x4 puis prendre le maximum de |x|. Les crêtes
//  inter-échantillons (ISP) que l'on rate en lisant seulement les échantillons
//  peuvent dépasser de +3 dB (et plus) le sample peak : c'est précisément ce qui
//  sature les convertisseurs N/A et les encodeurs AAC/MP3/Opus.
//
//  Interpolateur : FIR polyphase 4 phases, sinc fenêtré Kaiser, demi-longueur
//  L = 24 échantillons d'entrée (48 taps par phase), β = 7 (≈ 70 dB de réjection).
//    h_p[k] = sinc(L - k - p/4) · w_Kaiser(L - k - p/4),  k = 0 … 2L-1
//  appliqué à x[m-k] ; la sortie de phase p correspond à l'instant m - L + p/4.
//
//  Propriétés :
//   - La phase 0 vaut exactement δ[k - L] : l'échantillon d'origine est reproduit
//     sans erreur, donc true peak >= sample peak par construction.
//   - Chaque phase est normalisée à un gain DC unitaire.
//   - Bande passante plate à ±0.01 dB jusqu'à 0.454·fs (20 kHz à 44.1 kHz),
//     ±0.1 dB jusqu'à 0.46·fs : nettement plus large que le filtre 48 taps
//     d'exemple de l'annexe 2, qui sous-estime les crêtes proches de Nyquist.
//
//  Coût : 3 × 48 MAC par échantillon et par canal. Aucune allocation.
// =============================================================================
#pragma once

#include <array>

namespace omnis::analysis {

class TruePeakMeter {
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kOversampling = 4;
    static constexpr int kHalfLength = 24;
    static constexpr int kTapsPerPhase = 2 * kHalfLength;

    TruePeakMeter();

    void prepare(int numChannels) noexcept;
    void reset() noexcept;     ///< Vide l'historique et les crêtes.
    void resetPeaks() noexcept; ///< Remet seulement les crêtes mémorisées à zéro.

    void process(const double* const* channels, int numSamples) noexcept;

    /// Crête vraie maximale (linéaire) depuis le dernier reset, par canal.
    [[nodiscard]] double truePeak(int channel) const noexcept { return truePeak_[channel]; }
    [[nodiscard]] double samplePeak(int channel) const noexcept { return samplePeak_[channel]; }

    /// Maximum tous canaux confondus.
    [[nodiscard]] double truePeakMax() const noexcept;
    [[nodiscard]] double samplePeakMax() const noexcept;

    /// Crête vraie du dernier bloc traité (pour un afficheur avec retombée).
    [[nodiscard]] double lastBlockTruePeak() const noexcept { return lastBlockPeak_; }

    /// Coefficients (exposés pour les tests de réponse en fréquence).
    [[nodiscard]] const std::array<std::array<double, kTapsPerPhase>, kOversampling>& coefficients() const noexcept {
        return coefficients_;
    }

private:
    std::array<std::array<double, kTapsPerPhase>, kOversampling> coefficients_{};

    // Historique « miroir » : chaque échantillon est écrit à pos et pos+N, ce qui
    // garantit une fenêtre contiguë de N échantillons sans test de bouclage.
    std::array<std::array<double, 2 * kTapsPerPhase>, kMaxChannels> history_{};
    int writePos_ = 0;
    int numChannels_ = 2;

    std::array<double, kMaxChannels> truePeak_{};
    std::array<double, kMaxChannels> samplePeak_{};
    double lastBlockPeak_ = 0.0;
};

} // namespace omnis::analysis
