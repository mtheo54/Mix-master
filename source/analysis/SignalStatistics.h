// =============================================================================
//  Abrasion Labs // OMNIS
//  analysis/SignalStatistics.h — Statistiques d'amplitude par canal.
//
//  Moments centrés calculés en ligne par l'algorithme de Welford étendu aux
//  ordres 3 et 4 (Terriberry, 2007) : numériquement stable même sur des heures
//  d'audio, contrairement à l'accumulation naïve de Σx³ et Σx⁴.
//
//      skewness  γ1 = sqrt(n)·M3 / M2^(3/2)     (asymétrie de la distribution)
//      kurtosis  γ2 = n·M4 / M2² - 3            (excès ; 0 = gaussien)
//
//  Interprétation mastering :
//   - kurtosis élevée (> 3) : signal « piqué », transitoires dominants, beaucoup
//     de marge crête/RMS -> il reste de la place pour clipper / limiter.
//   - kurtosis proche de -1.5 : distribution en « plateau » — signature d'un
//     signal déjà écrasé (clipping dur : l'amplitude s'accumule aux rails).
//   - skewness ≠ 0 : asymétrie (saturation paire, DC, clipper « diode »).
//
//  Fournit aussi crête, RMS, DC, facteur de crête. Utilisé hors ligne (outils)
//  et, à terme, par le thread d'analyse IA. Aucune allocation.
// =============================================================================
#pragma once

#include "../core/Decibels.h"

#include <cmath>
#include <cstdint>

namespace omnis::analysis {

class SignalStatistics {
public:
    void reset() noexcept { *this = SignalStatistics{}; }

    void add(double x) noexcept {
        const double n1 = static_cast<double>(n_);
        ++n_;
        const double n = static_cast<double>(n_);
        const double delta = x - mean_;
        const double deltaN = delta / n;
        const double deltaN2 = deltaN * deltaN;
        const double term1 = delta * deltaN * n1;
        mean_ += deltaN;
        m4_ += term1 * deltaN2 * (n * n - 3.0 * n + 3.0) + 6.0 * deltaN2 * m2_ - 4.0 * deltaN * m3_;
        m3_ += term1 * deltaN * (n - 2.0) - 3.0 * deltaN * m2_;
        m2_ += term1;
        sumSquares_ += x * x;
        const double ax = std::fabs(x);
        if (ax > peak_) peak_ = ax;
    }

    void add(const double* x, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) add(x[i]);
    }

    [[nodiscard]] std::uint64_t count() const noexcept { return n_; }
    [[nodiscard]] double mean() const noexcept { return mean_; } ///< offset DC
    [[nodiscard]] double peak() const noexcept { return peak_; }
    [[nodiscard]] double rms() const noexcept {
        return n_ ? std::sqrt(sumSquares_ / static_cast<double>(n_)) : 0.0;
    }

    [[nodiscard]] double skewness() const noexcept {
        if (n_ < 3 || m2_ <= 0.0) return 0.0;
        return std::sqrt(static_cast<double>(n_)) * m3_ / std::pow(m2_, 1.5);
    }

    [[nodiscard]] double excessKurtosis() const noexcept {
        if (n_ < 4 || m2_ <= 0.0) return 0.0;
        return static_cast<double>(n_) * m4_ / (m2_ * m2_) - 3.0;
    }

    /// Facteur de crête (sample peak / RMS) en dB.
    [[nodiscard]] double crestFactorDb() const noexcept {
        const double r = rms();
        return r > 0.0 ? gainToDb(peak_ / r) : 0.0;
    }

private:
    std::uint64_t n_ = 0;
    double mean_ = 0.0, m2_ = 0.0, m3_ = 0.0, m4_ = 0.0;
    double sumSquares_ = 0.0;
    double peak_ = 0.0;
};

} // namespace omnis::analysis
