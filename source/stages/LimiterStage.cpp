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
    punch_.snapTo(defaultOf(ParamId::LimPunch) * 0.01);
    upwardDb_.snapTo(defaultOf(ParamId::LimUpward));
    unlinkTransient_.snapTo(defaultOf(ParamId::LimUnlinkTransient) * 0.01);
    unlinkSustain_.snapTo(defaultOf(ParamId::LimUnlinkSustain) * 0.01);
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
    for (auto* sm : {&punch_, &upwardDb_, &unlinkTransient_, &unlinkSustain_}) {
        sm->prepare(sampleRate_, 30.0);
        sm->snapTo(sm->target());
    }
    const auto coef = [&](double ms) { return 1.0 - std::exp(-1.0 / (ms * 1e-3 * sampleRate_)); };
    fastAttack_ = coef(0.3);
    fastRelease_ = coef(15.0);
    slowAttack_ = coef(20.0);
    slowRelease_ = coef(120.0);
    rmsAttack_ = coef(5.0);
    rmsRelease_ = coef(200.0);
    gainGlide_ = coef(1.0); // le gain de Punch ne bouge pas plus vite qu'en 1 ms (pas de grésillement)
    upGlide_ = coef(30.0);
    holdSamples_ = static_cast<int>(std::lround(0.025 * sampleRate_));
    releaseCoefficient_ = 1.0 - std::exp(-1.0 / (releaseMs_ * 1e-3 * sampleRate_));
    activityCoefficient_ = 1.0 - std::exp(-1.0 / (0.5 * sampleRate_));

    lookahead_ = std::max(1, static_cast<int>(std::lround(kLookaheadMs * 1e-3 * sampleRate_)));
    latency_ = lookahead_ + TruePeakMeter::kHalfLength;
    gainSmoother_.prepare(lookahead_);
    gainSmootherRight_.prepare(lookahead_);
    for (auto& d : delay_) d.assign(static_cast<std::size_t>(latency_), 0.0);
    reset();
}

void LimiterStage::reset() noexcept {
    for (auto& h : tpHistory_) h.fill(0.0);
    tpPos_ = 0;
    gainSmoother_.reset();
    gainSmootherRight_.reset();
    activity_ = 0.0;
    fastEnv_ = slowEnv_ = rmsEnv_ = 0.0;
    fastHold_ = slowHold_ = 0;
    punchGain_ = upwardGain_ = 1.0;
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
        case ParamId::LimPunch: immediate ? punch_.snapTo(plain * 0.01) : punch_.setTarget(plain * 0.01); break;
        case ParamId::LimUpward: immediate ? upwardDb_.snapTo(plain) : upwardDb_.setTarget(plain); break;
        case ParamId::LimUnlinkTransient:
            immediate ? unlinkTransient_.snapTo(plain * 0.01) : unlinkTransient_.setTarget(plain * 0.01);
            break;
        case ParamId::LimUnlinkSustain:
            immediate ? unlinkSustain_.snapTo(plain * 0.01) : unlinkSustain_.setTarget(plain * 0.01);
            break;
        case ParamId::LimRelease:
            releaseMs_ = plain;
            releaseCoefficient_ = 1.0 - std::exp(-1.0 / (releaseMs_ * 1e-3 * sampleRate_));
            break;
        default: break;
    }
}

void LimiterStage::detect(double l, double r, double& peakL, double& peakR) noexcept {
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
    peakL = pl;
    peakR = pr;
}

void LimiterStage::process(double* const* channels, int numSamples) noexcept {
    double* left = channels[0];
    double* right = channels[1];
    const double marginGain = dbToGain(-kTruePeakMarginDb);
    double lastGain = 1.0;
    constexpr double kMaxPunch = 2.8183829312644537; // +9 dB
    const double punchFloor = 1.0 / kMaxPunch;

    for (int i = 0; i < numSamples; ++i) {
        const double g = inputGain_.next();
        double l = left[i] * g;
        double r = right[i] * g;
        const double ceiling = ceiling_.next() * marginGain;
        const double punch = punch_.next(), upward = upwardDb_.next();
        const double unlinkT = unlinkTransient_.next(), unlinkS = unlinkSustain_.next();

        // Détecteur d'attaques (lié stéréo) : sert au Punch et au lien stéréo.
        const double level = std::max(std::fabs(l), std::fabs(r));
        // Maintien d'une période de grave (25 ms) avant de relâcher : sur une 808
        // tenue, les deux suiveurs ne suivent pas chaque alternance (pas de modulation).
        const auto follow = [&](double& env, int& hold, double attack, double release) {
            if (level > env) {
                env += attack * (level - env);
                hold = holdSamples_;
            } else if (hold > 0) {
                --hold;
            } else {
                env += release * (level - env);
            }
        };
        follow(fastEnv_, fastHold_, fastAttack_, fastRelease_);
        follow(slowEnv_, slowHold_, slowAttack_, slowRelease_);
        const double ratio = slowEnv_ > 1.0e-9 ? fastEnv_ / slowEnv_ : 1.0;
        // 0 : soutenu ; 1 : attaque d'au moins 6 dB au-dessus du corps.
        const double transient = std::clamp(gainToDb(std::max(ratio, 1.0)) / 6.0, 0.0, 1.0);

        if (punch > 0.0 || punchGain_ != 1.0) {
            const double want = punch > 0.0 ? std::clamp(std::pow(std::max(ratio, 1.0e-6), kPunchExponent * punch), punchFloor, kMaxPunch) : 1.0;
            punchGain_ += gainGlide_ * (want - punchGain_);
            if (punch == 0.0 && std::fabs(punchGain_ - 1.0) < 1.0e-9) punchGain_ = 1.0;
            l *= punchGain_;
            r *= punchGain_;
        }
        if (upward > 0.0 || upwardGain_ != 1.0) {
            const double power = 0.5 * (l * l + r * r);
            rmsEnv_ += (power > rmsEnv_ ? rmsAttack_ : rmsRelease_) * (power - rmsEnv_);
            const double rel = 10.0 * std::log10(std::max(rmsEnv_, 1.0e-20)) - gainToDb(ceiling);
            const double depth = std::clamp((-12.0 - rel) / 24.0, 0.0, 1.0) * std::clamp((rel + 70.0) / 10.0, 0.0, 1.0);
            const double want = upward > 0.0 ? dbToGain(upward * depth) : 1.0;
            upwardGain_ += upGlide_ * (want - upwardGain_);
            if (upward == 0.0 && std::fabs(upwardGain_ - 1.0) < 1.0e-9) upwardGain_ = 1.0;
            l *= upwardGain_;
            r *= upwardGain_;
        }

        // 1–3. Détection, gain requis (lié, puis par canal), relâchement.
        double peakL = 0.0, peakR = 0.0;
        detect(l, r, peakL, peakR);
        const double peak = std::max(peakL, peakR);
        const double required = peak > ceiling ? ceiling / peak : 1.0;
        double requiredL = required, requiredR = required;
        const double unlink = unlinkS + (unlinkT - unlinkS) * transient;
        if (unlink > 0.0) {
            const double rl = peakL > ceiling ? ceiling / peakL : 1.0;
            const double rr = peakR > ceiling ? ceiling / peakR : 1.0;
            requiredL = required * std::pow(rl / required, unlink);
            requiredR = required * std::pow(rr / required, unlink);
        }
        activity_ += activityCoefficient_ * ((required < 0.999 ? 1.0 : 0.0) - activity_);
        // a ≈ 1/(τ·fs) : diviser le coefficient revient à multiplier τ.
        const double release = releaseCoefficient_ / (1.0 + 4.0 * autoRelease_ * activity_);
        // 4–5. Minimum glissant + moyenne glissante sur le lookahead.
        const double gainL = gainSmoother_.process(requiredL, release);
        const double gainR = gainSmootherRight_.process(requiredR, release);

        // 6. Gain appliqué au signal retardé de L + D.
        const auto dp = static_cast<std::size_t>(delayPos_);
        const double dl = delay_[0][dp], dr = delay_[1][dp];
        delay_[0][dp] = l;
        delay_[1][dp] = r;
        if (++delayPos_ == latency_) delayPos_ = 0;
        left[i] = dl * gainL;
        right[i] = dr * gainR;
        lastGain = std::min(gainL, gainR);
    }
    if (numSamples > 0) lastGainDb_ = gainToDb(lastGain); // afficheur : la dernière valeur du bloc suffit
}

} // namespace omnis
