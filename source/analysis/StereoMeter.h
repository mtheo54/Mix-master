// =============================================================================
//  Abrasion Labs // OMNIS
//  analysis/StereoMeter.h — Corrélation de phase dynamique et ratio Mid/Side.
//
//  Trois moyennes exponentielles (constante de temps τ, 300 ms par défaut,
//  proche de l'inertie d'un corrélateur à aiguille) :
//      E[LR], E[L²], E[R²]
//  d'où :
//      corrélation  r   = E[LR] / sqrt(E[L²]·E[R²])          ∈ [-1, +1]
//      Mid  M = (L+R)/2  -> E[M²] = (E[L²] + 2E[LR] + E[R²]) / 4
//      Side S = (L-R)/2  -> E[S²] = (E[L²] - 2E[LR] + E[R²]) / 4
//      ratio S/M (dB) = 10·log10(E[S²] / E[M²])
//
//  r = +1 : mono parfait ; 0 : décorrélé (stéréo large) ; -1 : opposition de
//  phase (annulation en mono — à surveiller sur sub et kick).
// =============================================================================
#pragma once

#include "../core/Decibels.h"

#include <cmath>

namespace omnis::analysis {

class StereoMeter {
public:
    void prepare(double sampleRate, double timeConstantMs = 300.0) noexcept {
        coefficient_ = 1.0 - std::exp(-1.0 / (timeConstantMs * 1.0e-3 * sampleRate));
        reset();
    }

    void reset() noexcept { lr_ = ll_ = rr_ = 0.0; }

    void process(const double* left, const double* right, int numSamples) noexcept {
        const double a = coefficient_;
        double lr = lr_, ll = ll_, rr = rr_;
        for (int i = 0; i < numSamples; ++i) {
            const double l = left[i];
            const double r = right[i];
            lr += a * (l * r - lr);
            ll += a * (l * l - ll);
            rr += a * (r * r - rr);
        }
        lr_ = lr;
        ll_ = ll;
        rr_ = rr;
    }

    /// Corrélation de phase ; 0 si le signal est sous -120 dBFS (silence).
    [[nodiscard]] double correlation() const noexcept {
        const double denom = std::sqrt(ll_ * rr_);
        return denom > 1.0e-12 ? std::fmax(-1.0, std::fmin(1.0, lr_ / denom)) : 0.0;
    }

    [[nodiscard]] double midPower() const noexcept { return 0.25 * (ll_ + 2.0 * lr_ + rr_); }
    [[nodiscard]] double sidePower() const noexcept { return 0.25 * (ll_ - 2.0 * lr_ + rr_); }

    /// Ratio Side/Mid en dB (-200 si mono parfait).
    [[nodiscard]] double sideToMidDb() const noexcept {
        const double m = midPower();
        return m > 1.0e-24 ? powerToDb(sidePower() / m) : kMinusInfinityDb;
    }

private:
    double coefficient_ = 1.0;
    double lr_ = 0.0, ll_ = 0.0, rr_ = 0.0;
};

} // namespace omnis::analysis
