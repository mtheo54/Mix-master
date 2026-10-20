// =============================================================================
//  Abrasion Labs // OMNIS
//  core/AudioBuffer.h — Tampon audio planaire double précision, aligné SIMD.
//
//  Un seul bloc mémoire contigu, alloué dans prepare() (hors thread audio) ;
//  chaque canal commence sur une frontière de 64 octets et sa longueur est
//  arrondie au multiple de 8 doubles supérieur, de sorte que les boucles
//  vectorisées AVX2/AVX-512/NEON n'aient jamais de canal désaligné.
// =============================================================================
#pragma once

#include "Platform.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>

namespace omnis {

class AudioBuffer {
public:
    AudioBuffer() = default;
    AudioBuffer(int numChannels, int numFrames) { allocate(numChannels, numFrames); }

    AudioBuffer(const AudioBuffer&) = delete;
    AudioBuffer& operator=(const AudioBuffer&) = delete;
    AudioBuffer(AudioBuffer&&) noexcept = default;
    AudioBuffer& operator=(AudioBuffer&&) noexcept = default;

    /// Non temps réel. Le contenu est remis à zéro.
    void allocate(int numChannels, int numFrames) {
        assert(numChannels >= 0 && numChannels <= kMaxChannels && numFrames >= 0);
        constexpr std::size_t doublesPerLine = kSimdAlignment / sizeof(double);
        stride_ = (static_cast<std::size_t>(numFrames) + doublesPerLine - 1) / doublesPerLine * doublesPerLine;
        numChannels_ = numChannels;
        numFrames_ = numFrames;
        const std::size_t total = std::max<std::size_t>(stride_ * static_cast<std::size_t>(numChannels), 1);
        data_.reset(static_cast<double*>(::operator new(total * sizeof(double), std::align_val_t{kSimdAlignment})));
        std::memset(data_.get(), 0, total * sizeof(double));
        for (int ch = 0; ch < kMaxChannels; ++ch)
            channels_[ch] = ch < numChannels ? data_.get() + stride_ * static_cast<std::size_t>(ch) : nullptr;
    }

    void clear() noexcept {
        if (data_) std::memset(data_.get(), 0, stride_ * static_cast<std::size_t>(numChannels_) * sizeof(double));
    }

    void clear(int numFrames) noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            std::memset(channels_[ch], 0, static_cast<std::size_t>(std::min(numFrames, numFrames_)) * sizeof(double));
    }

    [[nodiscard]] int numChannels() const noexcept { return numChannels_; }
    [[nodiscard]] int numFrames() const noexcept { return numFrames_; }

    double* channel(int ch) noexcept { return channels_[ch]; }
    const double* channel(int ch) const noexcept { return channels_[ch]; }
    double* const* channels() noexcept { return channels_; }
    const double* const* channels() const noexcept { return channels_; }

    static constexpr int kMaxChannels = 8;

private:
    struct AlignedDelete {
        void operator()(double* p) const noexcept { ::operator delete(p, std::align_val_t{kSimdAlignment}); }
    };

    std::unique_ptr<double, AlignedDelete> data_;
    double* channels_[kMaxChannels] = {};
    std::size_t stride_ = 0;
    int numChannels_ = 0;
    int numFrames_ = 0;
};

} // namespace omnis
