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

void Fft::transform(Complex* data, bool inverse) const noexcept {
    const int n = size_;
    for (int i = 0; i < n; ++i) {
        const int j = bitReverse_[static_cast<std::size_t>(i)];
        if (j > i) std::swap(data[i], data[j]);
    }
    for (int length = 2; length <= n; length <<= 1) {
        const int half = length >> 1;
        const int step = n / length;
        for (int start = 0; start < n; start += length) {
            for (int k = 0; k < half; ++k) {
                Complex w = twiddles_[static_cast<std::size_t>(k * step)];
                if (inverse) w = std::conj(w);
                const Complex u = data[start + k];
                const Complex v = cmul(data[start + k + half], w);
                data[start + k] = u + v;
                data[start + k + half] = u - v;
            }
        }
    }
}

void Fft::inverse(Complex* data) const noexcept {
    transform(data, true);
    const double scale = 1.0 / size_;
    for (int i = 0; i < size_; ++i) data[i] *= scale;
}

} // namespace omnis::dsp
