// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/Fft.cpp
// =============================================================================
#include "Fft.h"

#include <cassert>
#include <cmath>
#include <numbers>
#include <utility>

namespace omnis::dsp {

void Fft::prepare(int size) {
    assert(size >= 2 && (size & (size - 1)) == 0);
    size_ = size;
    twiddles_.resize(static_cast<std::size_t>(size / 2));
    for (int k = 0; k < size / 2; ++k)
        twiddles_[static_cast<std::size_t>(k)] = std::polar(1.0, -2.0 * std::numbers::pi * k / size);
    // Facteurs rangés par étage : w_k = twiddles_[k · N/L], les mêmes valeurs.
    stages_.resize(static_cast<std::size_t>(size));
    for (int length = 2; length <= size; length <<= 1)
        for (int k = 0; k < length / 2; ++k)
            stages_[static_cast<std::size_t>(length / 2 - 1 + k)] = twiddles_[static_cast<std::size_t>(k * (size / length))];

    int bits = 0;
    while ((1 << bits) < size) ++bits;
    bitReverse_.resize(static_cast<std::size_t>(size));
    for (int i = 0; i < size; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        bitReverse_[static_cast<std::size_t>(i)] = r;
    }
}

template <bool Inverse>
void Fft::transform(Complex* data) const noexcept {
    const int n = size_;
    for (int i = 0; i < n; ++i) {
        const int j = bitReverse_[static_cast<std::size_t>(i)];
        if (j > i) std::swap(data[i], data[j]);
    }
    // Premier étage : w = 1, u ± v (identique au produit par (1, 0)).
    for (int start = 0; start < n; start += 2) {
        const Complex u = data[start], v = data[start + 1];
        data[start] = u + v;
        data[start + 1] = u - v;
    }
    for (int length = 4; length <= n; length <<= 1) {
        const int half = length >> 1;
        const Complex* w = stages_.data() + (half - 1);
        for (int start = 0; start < n; start += length) {
            Complex* lo = data + start;
            Complex* hi = lo + half;
            for (int k = 0; k < half; ++k) {
                const Complex wk = Inverse ? std::conj(w[k]) : w[k];
                const Complex u = lo[k];
                const Complex v = cmul(hi[k], wk);
                lo[k] = u + v;
                hi[k] = u - v;
            }
        }
    }
}

template void Fft::transform<false>(Complex*) const noexcept;
template void Fft::transform<true>(Complex*) const noexcept;

void Fft::inverse(Complex* data) const noexcept {
    transform<true>(data);
    const double scale = 1.0 / size_;
    for (int i = 0; i < size_; ++i) data[i] *= scale;
}

} // namespace omnis::dsp
