// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/StemTimeline.cpp
// =============================================================================
#include "StemTimeline.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace omnis::stems {

void StemTimeline::prepare(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    fft_.prepare(kBlockFrames);
    spectrumMid_.assign(kBlockFrames, {});
    spectrumSide_.assign(kBlockFrames, {});
    window_.resize(kBlockFrames);
    for (int i = 0; i < kBlockFrames; ++i)
        window_[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / kBlockFrames);
    const double binHz = sampleRate_ / kBlockFrames;
    for (int b = 0; b <= kTimelineBands; ++b)
        binEdges_[static_cast<std::size_t>(b)] =
            std::clamp(static_cast<int>(std::lround(kTimelineEdges[static_cast<std::size_t>(b)] / binHz)), 1, kBlockFrames / 2);
    lowBin_ = std::clamp(static_cast<int>(std::lround(150.0 / binHz)), 1, kBlockFrames / 2);
    pendingMid_.assign(kBlockFrames, 0.0);
    pendingSide_.assign(kBlockFrames, 0.0);
    pendingHas_.assign(kBlockFrames, false);
    reset();
}

void StemTimeline::reset() {
    blocks_.clear();
    std::fill(pendingMid_.begin(), pendingMid_.end(), 0.0);
    std::fill(pendingSide_.begin(), pendingSide_.end(), 0.0);
    std::fill(pendingHas_.begin(), pendingHas_.end(), false);
    pendingIndex_ = -1;
    pendingCount_ = 0;
}

void StemTimeline::push(const double* left, const double* right, int numFrames, std::int64_t projectStart) {
    if (pendingMid_.empty() || projectStart < 0) return;
    for (int i = 0; i < numFrames; ++i) {
        const std::int64_t pos = projectStart + i;
        const std::int64_t index = pos / kBlockFrames;
        if (index != pendingIndex_) {
            analysePending();
            pendingIndex_ = index;
        }
        const auto k = static_cast<std::size_t>(pos % kBlockFrames);
        const double l = std::isfinite(left[i]) ? left[i] : 0.0, r = std::isfinite(right[i]) ? right[i] : 0.0;
        pendingMid_[k] = 0.5 * (l + r);
        pendingSide_[k] = 0.5 * (l - r);
        if (!pendingHas_[k]) {
            pendingHas_[k] = true;
            ++pendingCount_;
        }
    }
}

void StemTimeline::flush() { analysePending(); }

void StemTimeline::analysePending() {
    if (pendingIndex_ < 0 || pendingCount_ == 0 || pendingIndex_ >= kMaxBlocks) {
        pendingIndex_ = -1;
        pendingCount_ = 0;
        std::fill(pendingHas_.begin(), pendingHas_.end(), false);
        return;
    }
    // Un bloc trop peu entendu (début ou fin d'une écoute) ne dit pas grand-chose.
    if (pendingCount_ >= kBlockFrames / 4) {
        for (int i = 0; i < kBlockFrames; ++i) {
            const auto k = static_cast<std::size_t>(i);
            const double w = pendingHas_[k] ? window_[k] : 0.0;
            spectrumMid_[k] = {pendingMid_[k] * w, 0.0};
            spectrumSide_[k] = {pendingSide_[k] * w, 0.0};
        }
        fft_.forward(spectrumMid_.data());
        fft_.forward(spectrumSide_.data());
        // Puissance par trame entendue : un bloc partiel compte comme un bloc plein.
        const double norm = 1.0 / (static_cast<double>(kBlockFrames) * pendingCount_);
        const auto index = static_cast<std::size_t>(pendingIndex_);
        if (blocks_.size() <= index) blocks_.resize(index + 1);
        Block& out = blocks_[index];
        for (int b = 0; b < kTimelineBands; ++b) {
            double p = 0.0;
            for (int k = binEdges_[static_cast<std::size_t>(b)]; k < binEdges_[static_cast<std::size_t>(b) + 1]; ++k)
                p += std::norm(spectrumMid_[static_cast<std::size_t>(k)]);
            out.mid[static_cast<std::size_t>(b)] += static_cast<float>(p * norm);
        }
        double side = 0.0, mid = 0.0;
        for (int k = 1; k < lowBin_; ++k) {
            side += std::norm(spectrumSide_[static_cast<std::size_t>(k)]);
            mid += std::norm(spectrumMid_[static_cast<std::size_t>(k)]);
        }
        out.sideLow += static_cast<float>(side * norm);
        out.midLow += static_cast<float>(mid * norm);
        if (out.passes < 65535) ++out.passes;
    }
    std::fill(pendingMid_.begin(), pendingMid_.end(), 0.0);
    std::fill(pendingSide_.begin(), pendingSide_.end(), 0.0);
    std::fill(pendingHas_.begin(), pendingHas_.end(), false);
    pendingIndex_ = -1;
    pendingCount_ = 0;
}

const StemTimeline::Block* StemTimeline::block(int k) const noexcept {
    if (k < 0 || k >= static_cast<int>(blocks_.size())) return nullptr;
    const Block& b = blocks_[static_cast<std::size_t>(k)];
    return b.passes > 0 ? &b : nullptr;
}

double StemTimeline::bandPower(int k, int band) const noexcept {
    const Block* b = block(k);
    return b ? static_cast<double>(b->mid[static_cast<std::size_t>(band)]) / b->passes : 0.0;
}

double StemTimeline::totalPower(int k) const noexcept {
    const Block* b = block(k);
    if (!b) return 0.0;
    double p = 0.0;
    for (const float x : b->mid) p += x;
    return p / b->passes;
}

int StemTimeline::heardBlocks() const noexcept {
    int n = 0;
    for (const auto& b : blocks_) n += b.passes > 0 ? 1 : 0;
    return n;
}

} // namespace omnis::stems
