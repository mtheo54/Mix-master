// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/LinearPhaseDesigner.cpp
// =============================================================================
#include "LinearPhaseDesigner.h"

#include <algorithm>
#include <cmath>

namespace omnis::dsp {

namespace {

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    const double h = 0.5 * x;
    for (int k = 1; k < 64; ++k) {
        term *= h / k;
        const double t2 = term * term;
        sum += t2;
        if (t2 < sum * 1e-17) break;
    }
    return sum;
}

} // namespace

void LinearPhaseDesigner::prepare(double sampleRate, int kernelLength, int blockSize) {
    sampleRate_ = sampleRate;
    length_ = kernelLength;
    fft_.prepare(2 * kernelLength);
    spectrum_.assign(static_cast<std::size_t>(2 * kernelLength), {});
    window_.resize(static_cast<std::size_t>(kernelLength));
    timeA_.assign(window_.size(), 0.0);
    timeB_.assign(window_.size(), 0.0);
    const double i0Beta = besselI0(kKaiserBeta);
    const double half = 0.5 * kernelLength;
    for (int n = 0; n < kernelLength; ++n) {
        const double r = (n - half) / half;
        window_[static_cast<std::size_t>(n)] = besselI0(kKaiserBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0Beta;
    }
    builder_.prepare(blockSize, kernelLength);
}

void LinearPhaseDesigner::finish(StereoConvolver::Kernel& out) noexcept {
    const int m = 2 * length_;
    fft_.inverse(spectrum_.data());
    // Réponse à phase nulle centrée sur N/2, fenêtrée -> FIR causal à phase linéaire.
    const int half = length_ / 2;
    for (int n = 0; n < length_; ++n) {
        const int idx = ((n - half) % m + m) % m;
        const double w = window_[static_cast<std::size_t>(n)];
        timeA_[static_cast<std::size_t>(n)] = spectrum_[static_cast<std::size_t>(idx)].real() * w;
        timeB_[static_cast<std::size_t>(n)] = spectrum_[static_cast<std::size_t>(idx)].imag() * w;
    }
    builder_.build(timeA_.data(), timeB_.data(), out);
}

} // namespace omnis::dsp
