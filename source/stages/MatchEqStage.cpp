// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/MatchEqStage.cpp
// =============================================================================
#include "MatchEqStage.h"

#include "SubControlStage.h"

#include <cmath>
#include <numbers>

namespace omnis {

using params::ParamId;

MatchEqStage::MatchEqStage() {
    const auto* amount = params::findParameter(ParamId::MatchAmount);
    request_.amount = amount ? amount->defaultValue * 0.01 : 1.0;
}

MatchEqStage::~MatchEqStage() { bank_.stop(); }

void MatchEqStage::prepare(const ProcessSpec& spec) {
    bank_.stop();
    sampleRate_ = spec.sampleRate;
    // Même résolution que le Sub Control (~2.7 Hz) : une demi-octave à 20 Hz
    // ne fait que 8 Hz de large.
    const int n = SubControlStage::kernelLengthFor(sampleRate_);
    const int b = SubControlStage::blockSizeFor(sampleRate_);
    convolver_.prepare(b, n);
    bank_.prepare(sampleRate_, n, b, realtime_, request_);
    activeSlot_ = bank_.initialSlot();
    fadingSlot_ = nullptr;
    fadeRamp_.resize(static_cast<std::size_t>(b));
    for (int i = 0; i < b; ++i)
        fadeRamp_[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(std::numbers::pi * (i + 0.5) / b);
    latency_ = n / 2 + b;
    reset();
}

void MatchEqStage::reset() noexcept {
    convolver_.reset();
    framePos_ = 0;
    if (fadingSlot_) {
        MatchKernelBank::release(fadingSlot_);
        fadingSlot_ = nullptr;
    }
}

void MatchEqStage::setParameter(ParamId id, double plain, bool /*immediate*/) noexcept {
    // Pas de lisseur : le changement de noyau est fondu sur une trame complète.
    const auto raw = static_cast<std::uint32_t>(id);
    const auto midFirst = static_cast<std::uint32_t>(ParamId::MatchMidFirst);
    const auto sideFirst = static_cast<std::uint32_t>(ParamId::MatchSideFirst);
    constexpr auto bands = static_cast<std::uint32_t>(params::kMatchBands);
    if (id == ParamId::MatchAmount)
        request_.amount = plain * 0.01;
    else if (raw >= midFirst && raw < midFirst + bands)
        request_.midDb[raw - midFirst] = plain;
    else if (raw >= sideFirst && raw < sideFirst + bands)
        request_.sideDb[raw - sideFirst] = plain;
}

void MatchEqStage::frameBoundary() noexcept {
    if (fadingSlot_) {
        MatchKernelBank::release(fadingSlot_);
        fadingSlot_ = nullptr;
    }
    bank_.request(request_);
    if (MatchKernelBank::Slot* ready = bank_.takeReady()) {
        MatchKernelBank::markRetiring(activeSlot_);
        fadingSlot_ = activeSlot_;
        activeSlot_ = ready;
    }
    convolver_.processFrame(activeSlot_->kernel, fadingSlot_ ? &fadingSlot_->kernel : nullptr);
    framePos_ = 0;
}

void MatchEqStage::process(double* const* channels, int numSamples) noexcept {
    double* left = channels[0];
    double* right = channels[1];
    for (int i = 0; i < numSamples; ++i) {
        convolver_.write(framePos_, 0.5 * (left[i] + right[i]), 0.5 * (left[i] - right[i]));
        double mid = convolver_.outputA(framePos_);
        double side = convolver_.outputB(framePos_);
        if (fadingSlot_) {
            const double t = fadeRamp_[static_cast<std::size_t>(framePos_)];
            mid = convolver_.secondaryA(framePos_) + t * (mid - convolver_.secondaryA(framePos_));
            side = convolver_.secondaryB(framePos_) + t * (side - convolver_.secondaryB(framePos_));
        }
        left[i] = mid + side;
        right[i] = mid - side;
        if (++framePos_ == convolver_.blockSize()) frameBoundary();
    }
}

} // namespace omnis
