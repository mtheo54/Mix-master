// =============================================================================
//  Abrasion Labs // OMNIS
//  analysis/BandLevels.cpp
// =============================================================================
#include "BandLevels.h"

#include "core/Decibels.h"
#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace omnis::analysis {

namespace {

double lowpass(double f, double fc) {
    const double w = f / fc, w2 = w * w;
    return 1.0 / (1.0 + w2 * w2);
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return -200.0;
    const auto k = static_cast<std::size_t>(p * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

} // namespace

BandLevels measureBandLevels(const double* left, const double* right, int numFrames, double sampleRate,
                             const std::array<double, 3>& splits) {
    constexpr int kN = 4096;
    constexpr int kBands = params::kMbBands;
    dsp::Fft fft(kN);
    std::vector<double> window(kN);
    double windowPower = 0.0;
    for (int n = 0; n < kN; ++n) {
        window[static_cast<std::size_t>(n)] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * n / kN);
        windowPower += window[static_cast<std::size_t>(n)] * window[static_cast<std::size_t>(n)];
    }
    // |B_b(f)|² par bin.
    std::vector<std::array<double, kBands>> weight(kN / 2 + 1);
    for (int k = 0; k <= kN / 2; ++k) {
        const double f = k * sampleRate / kN;
        const double l1 = lowpass(f, splits[0]), l2 = lowpass(f, splits[1]), l3 = lowpass(f, splits[2]);
        const double b[kBands] = {l1, std::max(0.0, l2 - l1), std::max(0.0, l3 - l2), 1.0 - l3};
        for (int i = 0; i < kBands; ++i) weight[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)] = b[i] * b[i];
    }

    std::vector<std::array<double, kBands>> frames;
    std::vector<double> totals;
    std::vector<std::complex<double>> buf(kN);
    for (int start = 0; start + kN <= numFrames; start += kN / 2) {
        for (int n = 0; n < kN; ++n) {
            const auto i = static_cast<std::size_t>(start + n);
            buf[static_cast<std::size_t>(n)] = {left[i] * window[static_cast<std::size_t>(n)], right[i] * window[static_cast<std::size_t>(n)]};
        }
        fft.forward(buf.data());
        std::array<double, kBands> p{};
        for (int k = 0; k <= kN / 2; ++k) {
            const auto z = buf[static_cast<std::size_t>(k)];
            const auto zc = std::conj(buf[static_cast<std::size_t>((kN - k) % kN)]);
            // Puissance moyenne L/R : ½(|L|² + |R|²), spectre unilatéral.
            const double power = 0.5 * (std::norm(0.5 * (z + zc)) + std::norm(0.5 * (z - zc)));
            const double side = (k == 0 || k == kN / 2) ? 1.0 : 2.0;
            for (int b = 0; b < kBands; ++b) p[static_cast<std::size_t>(b)] += side * power * weight[static_cast<std::size_t>(k)][static_cast<std::size_t>(b)];
        }
        double total = 0.0;
        for (auto& v : p) {
            v /= kN * windowPower; // carré moyen (Parseval, fenêtre normalisée)
            total += v;
        }
        frames.push_back(p);
        totals.push_back(total);
    }
    BandLevels out;
    if (frames.empty()) return out;
    const double gate = percentile(totals, 0.95) * 0.01;
    std::array<std::vector<double>, kBands> levels;
    std::array<double, kBands> sum{};
    int count = 0;
    for (std::size_t f = 0; f < frames.size(); ++f) {
        if (totals[f] <= gate || totals[f] <= 0.0) continue;
        ++count;
        for (int b = 0; b < kBands; ++b) {
            levels[static_cast<std::size_t>(b)].push_back(powerToDb(frames[f][static_cast<std::size_t>(b)], -200.0));
            sum[static_cast<std::size_t>(b)] += frames[f][static_cast<std::size_t>(b)];
        }
    }
    for (int b = 0; b < kBands; ++b) {
        const auto i = static_cast<std::size_t>(b);
        out.p10[i] = percentile(levels[i], 0.10);
        out.p50[i] = percentile(levels[i], 0.50);
        out.p90[i] = percentile(levels[i], 0.90);
        out.meanDb[i] = powerToDb(sum[i] / std::max(1, count), -200.0);
    }
    return out;
}

} // namespace omnis::analysis
