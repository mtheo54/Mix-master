// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/ImagerStage.cpp
// =============================================================================
#include "ImagerStage.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace omnis {

using params::ParamId;

namespace {

constexpr double kTransitionOctaves = 1.0 / 3.0; ///< demi-largeur de chaque transition

double defaultOf(ParamId id) {
    const auto* spec = params::findParameter(id);
    return spec ? spec->defaultValue : 0.0;
}

ParamId widthId(int band) noexcept {
    return static_cast<ParamId>(static_cast<std::uint32_t>(ParamId::ImgWidthFirst) + static_cast<std::uint32_t>(band));
}

} // namespace

bool ImagerKernelRequest::nearlyEquals(const ImagerKernelRequest& o) const noexcept {
    for (std::size_t k = 0; k < splitHz.size(); ++k)
        if (std::fabs(splitHz[k] - o.splitHz[k]) > 1e-4 * o.splitHz[k]) return false;
    for (std::size_t k = 0; k < width.size(); ++k)
        if (std::fabs(width[k] - o.width[k]) > 1e-4) return false;
    return true;
}

double ImagerKernelRequest::sideMagnitude(double f) const noexcept {
    // Largeur de la bande la plus basse, puis chaque transition ajoute l'écart
    // avec la bande suivante (cosinus surélevé en log-fréquence).
    double w = width[0];
    if (f <= 0.0) return w;
    for (std::size_t k = 0; k < splitHz.size(); ++k) {
        const double t = std::clamp((std::log2(f / splitHz[k]) + kTransitionOctaves) / (2.0 * kTransitionOctaves), 0.0, 1.0);
        w += (width[k + 1] - width[k]) * (0.5 - 0.5 * std::cos(std::numbers::pi * t));
    }
    return std::max(0.0, w);
}

int ImagerStage::kernelLengthFor(double sampleRate) noexcept {
    if (sampleRate <= 50000.0) return 4096;
    if (sampleRate <= 100000.0) return 8192;
    if (sampleRate <= 200000.0) return 16384;
    return 32768;
}

ImagerStage::ImagerStage() {
    request_.splitHz = {defaultOf(ParamId::ImgSplitLow), defaultOf(ParamId::ImgSplitMid), defaultOf(ParamId::ImgSplitHigh)};
    for (int b = 0; b < kImagerBands; ++b) request_.width[static_cast<std::size_t>(b)] = defaultOf(widthId(b)) * 0.01;
}

ImagerStage::~ImagerStage() { bank_.stop(); }

ImagerKernelRequest ImagerStage::orderedRequest() const noexcept {
    // Fréquences saisies dans le désordre : rangées, au moins 2/3 d'octave
    // d'écart (deux transitions ne se chevauchent pas).
    ImagerKernelRequest r = request_;
    std::sort(r.splitHz.begin(), r.splitHz.end());
    const double gap = std::exp2(2.0 * kTransitionOctaves);
    for (std::size_t k = 1; k < r.splitHz.size(); ++k) r.splitHz[k] = std::max(r.splitHz[k], r.splitHz[k - 1] * gap);
    return r;
}

void ImagerStage::prepare(const ProcessSpec& spec) {
    bank_.stop();
    sampleRate_ = spec.sampleRate;
    const int n = kernelLengthFor(sampleRate_);
    const int b = n / 32;
    convolver_.prepare(b, n);
    bank_.prepare(sampleRate_, n, b, realtime_, orderedRequest());
    activeSlot_ = bank_.initialSlot();
    fadingSlot_ = nullptr;
    fadeRamp_.resize(static_cast<std::size_t>(b));
    for (int i = 0; i < b; ++i)
        fadeRamp_[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(std::numbers::pi * (i + 0.5) / b);
    latency_ = n / 2 + b;
    reset();
}

void ImagerStage::reset() noexcept {
    convolver_.reset();
    framePos_ = 0;
    if (fadingSlot_) {
        Bank::release(fadingSlot_);
        fadingSlot_ = nullptr;
    }
}

void ImagerStage::setParameter(ParamId id, double plain, bool /*immediate*/) noexcept {
    // Pas de lisseur : le changement de noyau est fondu sur une trame.
    const auto raw = static_cast<std::uint32_t>(id);
    const auto first = static_cast<std::uint32_t>(ParamId::ImgWidthFirst);
    if (id == ParamId::ImgSplitLow)
        request_.splitHz[0] = plain;
    else if (id == ParamId::ImgSplitMid)
        request_.splitHz[1] = plain;
    else if (id == ParamId::ImgSplitHigh)
        request_.splitHz[2] = plain;
    else if (raw >= first && raw < first + kImagerBands)
        request_.width[raw - first] = plain * 0.01;
}

void ImagerStage::frameBoundary() noexcept {
    if (fadingSlot_) {
        Bank::release(fadingSlot_);
        fadingSlot_ = nullptr;
    }
    bank_.request(orderedRequest());
    if (Bank::Slot* ready = bank_.takeReady()) {
        Bank::markRetiring(activeSlot_);
        fadingSlot_ = activeSlot_;
        activeSlot_ = ready;
    }
    convolver_.processFrame(activeSlot_->kernel, fadingSlot_ ? &fadingSlot_->kernel : nullptr);
    framePos_ = 0;
}

void ImagerStage::process(double* const* channels, int numSamples) noexcept {
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
