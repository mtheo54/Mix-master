// =============================================================================
//  Abrasion Labs // OMNIS
//  analysis/TruePeakMeter.cpp
// =============================================================================
#include "TruePeakMeter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace omnis::analysis {

namespace {

/// Fonction de Bessel modifiée de première espèce d'ordre 0, par série :
///   I0(x) = Σ_k ((x/2)^k / k!)²   — converge très vite pour x < 20.
double besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    const double halfX = 0.5 * x;
    for (int k = 1; k < 64; ++k) {
        term *= halfX / k;
        const double t2 = term * term;
        sum += t2;
        if (t2 < sum * 1.0e-17) break;
    }
    return sum;
}

double sinc(double x) {
    if (std::fabs(x) < 1.0e-12) return 1.0;
    const double px = std::numbers::pi * x;
    return std::sin(px) / px;
}

} // namespace

TruePeakMeter::TruePeakMeter() {
    constexpr double beta = 7.0;
    const double i0Beta = besselI0(beta);
    for (int p = 0; p < kOversampling; ++p) {
        double dcGain = 0.0;
        for (int k = 0; k < kTapsPerPhase; ++k) {
            const double t = static_cast<double>(kHalfLength - k) - static_cast<double>(p) / kOversampling;
            const double r = t / kHalfLength;
            const double window = std::fabs(r) < 1.0 ? besselI0(beta * std::sqrt(1.0 - r * r)) / i0Beta : 0.0;
            coefficients_[p][k] = sinc(t) * window;
            dcGain += coefficients_[p][k];
        }
        for (double& c : coefficients_[p]) c /= dcGain;
    }
    // La phase 0 est l'identité exacte (sinc nul aux entiers non nuls) : on
    // force les zéros pour éliminer le résidu d'arrondi de sin(π·n).
    for (int k = 0; k < kTapsPerPhase; ++k) coefficients_[0][k] = (k == kHalfLength) ? 1.0 : 0.0;
}

void TruePeakMeter::prepare(int numChannels) noexcept {
    numChannels_ = std::clamp(numChannels, 1, kMaxChannels);
    reset();
}

void TruePeakMeter::reset() noexcept {
    for (auto& h : history_) h.fill(0.0);
    writePos_ = 0;
    resetPeaks();
}

void TruePeakMeter::resetPeaks() noexcept {
    truePeak_.fill(0.0);
    samplePeak_.fill(0.0);
    lastBlockPeak_ = 0.0;
}

void TruePeakMeter::process(const double* const* channels, int numSamples) noexcept {
    double blockPeak = 0.0;
    int pos = writePos_;
    for (int ch = 0; ch < numChannels_; ++ch) {
        auto& hist = history_[ch];
        const double* x = channels[ch];
        double tp = truePeak_[ch];
        double sp = samplePeak_[ch];
        pos = writePos_;
        for (int i = 0; i < numSamples; ++i) {
            const double s = x[i];
            sp = std::max(sp, std::fabs(s));
            // hist[pos .. pos+N-1] contient x[m-N+1 … m] dans l'ordre
            // chronologique ; on veut w[k] = x[m-k], donc w[k] = hist[pos+N-1-k].
            hist[pos] = s;
            hist[pos + kTapsPerPhase] = s;
            pos = (pos + 1) % kTapsPerPhase;
            const double* window = hist.data() + pos; // x[m-N+1] … x[m]
            double peak = std::fabs(window[kTapsPerPhase - 1 - kHalfLength]); // phase 0
            for (int p = 1; p < kOversampling; ++p) {
                const auto& c = coefficients_[p];
                double acc = 0.0;
                for (int k = 0; k < kTapsPerPhase; ++k) acc += c[k] * window[kTapsPerPhase - 1 - k];
                peak = std::max(peak, std::fabs(acc));
            }
            tp = std::max(tp, peak);
            blockPeak = std::max(blockPeak, peak);
        }
        truePeak_[ch] = tp;
        samplePeak_[ch] = sp;
    }
    writePos_ = pos;
    lastBlockPeak_ = blockPeak;
}

double TruePeakMeter::truePeakMax() const noexcept {
    double m = 0.0;
    for (int ch = 0; ch < numChannels_; ++ch) m = std::max(m, truePeak_[ch]);
    return m;
}

double TruePeakMeter::samplePeakMax() const noexcept {
    double m = 0.0;
    for (int ch = 0; ch < numChannels_; ++ch) m = std::max(m, samplePeak_[ch]);
    return m;
}

} // namespace omnis::analysis
