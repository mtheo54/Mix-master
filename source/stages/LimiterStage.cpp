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
    // Les trois phases interpolées des deux canaux (six sommes indépendantes)
    // sont menées de front ; chaque somme garde son ordre (k = 0 … N−1) : même
    // résultat que phase par phase, canal par canal.
    constexpr int kN = TruePeakMeter::kTapsPerPhase;
    static_assert(TruePeakMeter::kOversampling == 4);
    auto& histL = tpHistory_[0];
    auto& histR = tpHistory_[1];
    const auto at = static_cast<std::size_t>(tpPos_);
    histL[at] = l;
    histL[at + kN] = l;
    histR[at] = r;
    histR[at + kN] = r;
    const int pos = (tpPos_ + 1) % kN;
    const double* wl = histL.data() + pos;
    const double* wr = histR.data() + pos;
    const double* c1 = tpCoefficients_[1].data();
    const double* c2 = tpCoefficients_[2].data();
    const double* c3 = tpCoefficients_[3].data();
    double l1 = 0.0, l2 = 0.0, l3 = 0.0, r1 = 0.0, r2 = 0.0, r3 = 0.0;
    for (int k = 0; k < kN; ++k) {
        const double xl = wl[kN - 1 - k], xr = wr[kN - 1 - k];
        l1 += c1[k] * xl;
        l2 += c2[k] * xl;
        l3 += c3[k] * xl;
        r1 += c1[k] * xr;
        r2 += c2[k] * xr;
        r3 += c3[k] * xr;
    }
    const double pl = std::max({std::fabs(wl[kN - 1 - TruePeakMeter::kHalfLength]), std::fabs(l1), std::fabs(l2), std::fabs(l3)});
    const double pr = std::max({std::fabs(wr[kN - 1 - TruePeakMeter::kHalfLength]), std::fabs(r1), std::fabs(r2), std::fabs(r3)});
    tpPos_ = pos;
    return std::max(pl, pr);
}

void LimiterStage::process(double* const* channels, int numSamples) noexcept {
    double* left = channels[0];
    double* right = channels[1];
    const double marginGain = dbToGain(-kTruePeakMarginDb);
    double lastGain = 1.0;

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
        lastGain = gain;
    }
    if (numSamples > 0) lastGainDb_ = gainToDb(lastGain); // afficheur : la dernière valeur du bloc suffit
}

} // namespace omnis
