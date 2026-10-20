// =============================================================================
//  Abrasion Labs // OMNIS
//  analysis/LoudnessMeter.h — Sonie ITU-R BS.1770-4 / EBU R128 (+ LRA EBU 3342).
//
//  Mesures fournies (toutes en LUFS, sauf LRA en LU) :
//   - Momentary  : fenêtre glissante rectangulaire de 400 ms
//   - Short-term : fenêtre glissante rectangulaire de 3 s
//   - Integrated : blocs de 400 ms à 75 % de recouvrement, porte absolue -70
//                  LUFS puis porte relative -10 LU (BS.1770-4 §2.3)
//   - LRA        : distribution des valeurs short-term (pas de 100 ms), porte
//                  absolue -70, porte relative -20 LU, écart P95 - P10.
//
//  Formule de sonie d'un bloc :
//        L = -0.691 + 10·log10( Σ_i G_i · z_i ),   z_i = (1/T)·Σ y_i²
//  avec y_i le canal i pondéré K et G_i = 1 pour L/R (stéréo uniquement ici).
//
//  Temps réel : toutes les mémoires (anneau de sous-blocs, histogrammes) sont
//  allouées dans prepare(). process() est O(n) sans allocation ; chaque pas
//  de 100 ms ajoute au plus une entrée dans chaque histogramme. La sonie
//  intégrée et la LRA sont calculées à la demande sur l'histogramme (O(8000)).
//
//  Pourquoi un histogramme ? Une mesure intégrée sur une session de plusieurs
//  heures ne peut pas stocker tous les blocs sans allocation. Avec des cases de
//  0.01 LU et la somme exacte des puissances par case, l'erreur se limite à la
//  case qui contient le seuil relatif (pondérée par sa puissance moyenne
//  réelle), soit bien en dessous de la tolérance ±0.1 LU de l'EBU Tech 3341.
// =============================================================================
#pragma once

#include "KWeightingFilter.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>

namespace omnis::analysis {

/// Histogramme de puissances de blocs indexé par sonie (cases de 0.01 LU).
class LoudnessHistogram {
public:
    static constexpr double kMinLufs = -70.0;
    static constexpr double kMaxLufs = 10.0;
    static constexpr double kBinWidth = 0.01;
    static constexpr int kNumBins = static_cast<int>((kMaxLufs - kMinLufs) / kBinWidth);

    void allocate();
    void reset() noexcept;

    /// Ajoute un bloc dont la puissance pondérée vaut `power`. Les blocs sous
    /// la porte absolue (-70 LUFS) sont ignorés (BS.1770-4 : Γa = -70 LUFS).
    void add(double power) noexcept;

    [[nodiscard]] std::uint64_t count() const noexcept { return totalCount_; }

    /// Moyenne de puissance des blocs au-dessus de (moyenne - relativeGateLu).
    /// Renvoie -inf s'il n'y a aucun bloc.
    [[nodiscard]] double gatedLoudness(double relativeGateLu) const noexcept;

    /// Écart entre percentiles hauts et bas après porte relative (LRA).
    [[nodiscard]] double loudnessRange(double relativeGateLu, double lowPercentile,
                                       double highPercentile) const noexcept;

private:
    [[nodiscard]] double relativeThreshold(double relativeGateLu) const noexcept;
    [[nodiscard]] int firstBinAbove(double thresholdLufs) const noexcept;

    std::unique_ptr<std::uint64_t[]> counts_;
    std::unique_ptr<double[]> powerSums_;
    std::uint64_t totalCount_ = 0;
    double totalPower_ = 0.0;
};

class LoudnessMeter {
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kShortTermSubBlocks = 30; // 3 s / 100 ms
    static constexpr int kMomentarySubBlocks = 4;  // 400 ms / 100 ms

    /// Appelée à chaque nouveau pas de 100 ms (thread de process()).
    using StepCallback = void (*)(void* user, double momentaryLufs, double shortTermLufs, bool shortTermValid);

    /// Non temps réel.
    void prepare(double sampleRate, int numChannels);

    void reset() noexcept;          ///< Remet tout à zéro (filtres, fenêtres, intégré, LRA, max).
    void resetIntegrated() noexcept; ///< Remet à zéro intégré, LRA et max sans toucher aux fenêtres.

    void process(const double* const* channels, int numSamples) noexcept;

    void setStepCallback(StepCallback callback, void* user) noexcept {
        stepCallback_ = callback;
        stepUser_ = user;
    }

    [[nodiscard]] double momentaryLufs() const noexcept { return momentary_; }
    [[nodiscard]] double shortTermLufs() const noexcept { return shortTerm_; }
    [[nodiscard]] double maxMomentaryLufs() const noexcept { return maxMomentary_; }
    [[nodiscard]] double maxShortTermLufs() const noexcept { return maxShortTerm_; }
    [[nodiscard]] double integratedLufs() const noexcept;
    [[nodiscard]] double loudnessRangeLu() const noexcept;

    [[nodiscard]] int subBlockLength() const noexcept { return subBlockLength_; }

    static double powerToLufs(double power) noexcept;

    static constexpr double kNegativeInfinity = -std::numeric_limits<double>::infinity();

private:
    void completeSubBlock() noexcept;

    std::array<KWeightingFilter, kMaxChannels> filters_{};
    int numChannels_ = 2;
    int subBlockLength_ = 4800;
    int subBlockPosition_ = 0;
    double subBlockSum_ = 0.0;

    // Anneau des puissances moyennes des 30 derniers sous-blocs de 100 ms.
    std::array<double, kShortTermSubBlocks> subBlockPowers_{};
    int ringIndex_ = 0;
    std::uint64_t completedSubBlocks_ = 0;

    double momentary_ = kNegativeInfinity;
    double shortTerm_ = kNegativeInfinity;
    double maxMomentary_ = kNegativeInfinity;
    double maxShortTerm_ = kNegativeInfinity;

    // Le pas d'intégration ne commence qu'après 4 sous-blocs réels depuis le
    // dernier resetIntegrated() (pas de blocs « à moitié silencieux »).
    std::uint64_t subBlocksSinceIntegratedReset_ = 0;

    LoudnessHistogram integratedHistogram_;
    LoudnessHistogram rangeHistogram_;

    StepCallback stepCallback_ = nullptr;
    void* stepUser_ = nullptr;
};

} // namespace omnis::analysis
