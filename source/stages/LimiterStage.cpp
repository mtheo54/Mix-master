// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/LimiterStage.cpp
// =============================================================================
#include "LimiterStage.h"

#include "core/Decibels.h"

#include <algorithm>
#include <cmath>

namespace omnis {

using params::ParamId;
using analysis::TruePeakMeter;

namespace {

double defaultOf(ParamId id) {
    const auto* spec = params::findParameter(id);
    return spec ? spec->defaultValue : 0.0;
}

double smoothingOf(ParamId id) {
    const auto* spec = params::findParameter(id);
    return spec ? spec->smoothingMs : 0.0;
}

} // namespace

LimiterStage::LimiterStage() {
    inputGain_.snapTo(dbToGain(defaultOf(ParamId::LimGain)));
    ceiling_.snapTo(dbToGain(defaultOf(ParamId::LimCeiling)));
    releaseMs_ = defaultOf(ParamId::LimRelease);
    autoRelease_ = defaultOf(ParamId::LimAutoRelease) * 0.01;
    const TruePeakMeter reference;
    tpCoefficients_ = reference.coefficients();
}

void LimiterStage::prepare(const ProcessSpec& spec) {
    sampleRate_ = spec.sampleRate;
    inputGain_.prepare(sampleRate_, smoothingOf(ParamId::LimGain));
    ceiling_.prepare(sampleRate_, smoothingOf(ParamId::LimCeiling));
    inputGain_.snapTo(inputGain_.target());
    ceiling_.snapTo(ceiling_.target());
    releaseCoefficient_ = 1.0 - std::exp(-1.0 / (releaseMs_ * 1e-3 * sampleRate_));
    activityCoefficient_ = 1.0 - std::exp(-1.0 / (0.5 * sampleRate_));

    lookahead_ = std::max(1, static_cast<int>(std::lround(kLookaheadMs * 1e-3 * sampleRate_)));
    latency_ = lookahead_ + TruePeakMeter::kHalfLength;
    gainSmoother_.prepare(lookahead_);
    for (auto& d : delay_) d.assign(static_cast<std::size_t>(latency_), 0.0);
    reset();
}

void LimiterStage::reset() noexcept {
    for (auto& h : tpHistory_) h.fill(0.0);
    tpPos_ = 0;
    gainSmoother_.reset();
    activity_ = 0.0;
    for (auto& d : delay_) std::fill(d.begin(), d.end(), 0.0);
    delayPos_ = 0;
    lastGainDb_ = 0.0;
}

void LimiterStage::setParameter(ParamId id, double plain, bool immediate) noexcept {
    switch (id) {
        case ParamId::LimGain:
            immediate ? inputGain_.snapTo(dbToGain(plain)) : inputGain_.setTarget(dbToGain(plain));
            break;
        case ParamId::LimCeiling:
            immediate ? ceiling_.snapTo(dbToGain(plain)) : ceiling_.setTarget(dbToGain(plain));
            break;
        case ParamId::LimAutoRelease: autoRelease_ = plain * 0.01; break;
        case ParamId::LimRelease:
            releaseMs_ = plain;
            releaseCoefficient_ = 1.0 - std::exp(-1.0 / (releaseMs_ * 1e-3 * sampleRate_));
            break;
        default: break;
    }
}

double LimiterStage::detect(double l, double r) noexcept {
    // Historique miroir : fenêtre contiguë x[m−N+1 … m] à partir de tpPos_.
    constexpr int kN = TruePeakMeter::kTapsPerPhase;
    double peak = 0.0;
    const double in[2] = {l, r};
    int pos = tpPos_;
    for (int ch = 0; ch < 2; ++ch) {
        auto& hist = tpHistory_[static_cast<std::size_t>(ch)];
        hist[static_cast<std::size_t>(tpPos_)] = in[ch];
        hist[static_cast<std::size_t>(tpPos_ + kN)] = in[ch];
        pos = (tpPos_ + 1) % kN;
        const double* w = hist.data() + pos;
        double p = std::fabs(w[kN - 1 - TruePeakMeter::kHalfLength]); // échantillon m − D
        for (int phase = 1; phase < TruePeakMeter::kOversampling; ++phase) {
            const auto& c = tpCoefficients_[static_cast<std::size_t>(phase)];
            double acc = 0.0;
            for (int k = 0; k < kN; ++k) acc += c[static_cast<std::size_t>(k)] * w[kN - 1 - k];
            p = std::max(p, std::fabs(acc));
        }
        peak = std::max(peak, p);
    }
    tpPos_ = pos;
    return peak;
}

void LimiterStage::process(double* const* channels, int numSamples) noexcept {
    double* left = channels[0];
    double* right = channels[1];
    const double marginGain = dbToGain(-kTruePeakMarginDb);

    for (int i = 0; i < numSamples; ++i) {
        const double g = inputGain_.next();
        const double l = left[i] * g;
        const double r = right[i] * g;
        const double ceiling = ceiling_.next() * marginGain;

        // 1–3. Détection, gain requis, relâchement.
        const double peak = detect(l, r);
        const double required = peak > ceiling ? ceiling / peak : 1.0;
        activity_ += activityCoefficient_ * ((required < 0.999 ? 1.0 : 0.0) - activity_);
        // a ≈ 1/(τ·fs) : diviser le coefficient revient à multiplier τ.
        const double release = releaseCoefficient_ / (1.0 + 4.0 * autoRelease_ * activity_);
        // 4–5. Minimum glissant + moyenne glissante sur le lookahead.
        const double gain = gainSmoother_.process(required, release);

        // 6. Gain appliqué au signal retardé de L + D.
        const auto dp = static_cast<std::size_t>(delayPos_);
        const double dl = delay_[0][dp], dr = delay_[1][dp];
        delay_[0][dp] = l;
        delay_[1][dp] = r;
        if (++delayPos_ == latency_) delayPos_ = 0;
        left[i] = dl * gain;
        right[i] = dr * gain;
        lastGainDb_ = gainToDb(gain);
    }
}

} // namespace omnis
