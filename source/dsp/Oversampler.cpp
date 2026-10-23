// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/Oversampler.cpp
// =============================================================================
#include "Oversampler.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace omnis::dsp {

void Oversampler::prepare(int maxBlockSize) {
    maxBlock_ = std::max(1, maxBlockSize);
    for (int s = 0; s < kMaxStages; ++s) {
        const auto coefs = designHalfband(kAttenuationDb, kTransitions[static_cast<std::size_t>(s)]);
        for (auto& ch : filters_) ch[static_cast<std::size_t>(s)].setCoefficients(coefs);
    }
    const auto size = static_cast<std::size_t>(maxBlock_) << kMaxStages;
    for (int ch = 0; ch < kChannels; ++ch) {
        bufA_[static_cast<std::size_t>(ch)].assign(size, 0.0);
        bufB_[static_cast<std::size_t>(ch)].assign(size, 0.0);
    }
    reset();
}

void Oversampler::reset() noexcept {
    for (auto& ch : filters_)
        for (auto& f : ch) f.reset();
}

void Oversampler::setStages(int stages) noexcept {
    stages = std::clamp(stages, 0, kMaxStages);
    if (stages == stages_) return;
    // Les étages nouvellement utilisés démarrent d'un état vide.
    for (int s = std::min(stages, stages_); s < kMaxStages; ++s)
        for (auto& ch : filters_) ch[static_cast<std::size_t>(s)].reset();
    stages_ = stages;
}

double* const* Oversampler::upsample(const double* const* input, int numSamples) noexcept {
    for (int ch = 0; ch < kChannels; ++ch) {
        const auto c = static_cast<std::size_t>(ch);
        double* a = bufA_[c].data();
        double* b = bufB_[c].data();
        std::copy(input[ch], input[ch] + numSamples, a);
        bool inA = true;
        int length = numSamples;
        for (int s = 0; s < stages_; ++s) {
            const double* src = inA ? a : b;
            double* dst = inA ? b : a;
            auto& f = filters_[c][static_cast<std::size_t>(s)];
            f.upsampleBlock(src, dst, length);
            length *= 2;
            inA = !inA;
        }
        result_[c] = inA ? a : b;
        resultInA_[c] = inA;
    }
    return result_.data();
}

void Oversampler::downsample(double* const* output, int numSamples) noexcept {
    for (int ch = 0; ch < kChannels; ++ch) {
        const auto c = static_cast<std::size_t>(ch);
        double* a = bufA_[c].data();
        double* b = bufB_[c].data();
        bool inA = resultInA_[c];
        int length = numSamples << stages_;
        for (int s = stages_ - 1; s >= 0; --s) {
            const double* src = inA ? a : b;
            double* dst = inA ? b : a;
            auto& f = filters_[c][static_cast<std::size_t>(s)];
            length /= 2;
            f.downsampleBlock(src, dst, length);
            inA = !inA;
        }
        const double* src = inA ? a : b;
        std::copy(src, src + numSamples, output[ch]);
    }
}

double Oversampler::phaseDelaySamples(int stages, double sampleRate) {
    if (stages <= 0) return 0.0;
    // Réponse impulsionnelle montée + descente, puis phase à 1 kHz.
    Oversampler os;
    os.prepare(64);
    os.setStages(stages);
    constexpr int kLength = 4096;
    std::vector<double> h(kLength, 0.0);
    std::vector<double> in(64, 0.0), dummy(64, 0.0);
    for (int pos = 0; pos < kLength; pos += 64) {
        std::fill(in.begin(), in.end(), 0.0);
        if (pos == 0) in[0] = 1.0;
        const double* ins[2] = {in.data(), in.data()};
        double* const* up = os.upsample(ins, 64);
        (void)up;
        double* outs[2] = {h.data() + pos, dummy.data()};
        os.downsample(outs, 64);
    }
    const double w = 2.0 * std::numbers::pi * 1000.0 / sampleRate;
    std::complex<double> acc{};
    for (int n = 0; n < kLength; ++n) acc += h[static_cast<std::size_t>(n)] * std::polar(1.0, -w * n);
    double phase = std::arg(acc);
    if (phase > 0.0) phase -= 2.0 * std::numbers::pi;
    return -phase / w;
}

} // namespace omnis::dsp
