// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/DynamicEq.cpp
// =============================================================================
#include "DynamicEq.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <numbers>

namespace omnis::dsp {

using params::DynChannel;
using params::DynControl;
using params::ParamId;

namespace {
constexpr double kHabitSeconds = 4.0; ///< mémoire de l'équilibre habituel
constexpr double kGatePower = 1.0e-7; ///< ~ −70 dB : en dessous, rien ne bouge
constexpr double kKneeDb = 2.0;
constexpr double kSlope = 1.0; ///< la bande revient à son seuil (le rapport bande / mix comprime déjà)
} // namespace

void DynamicEq::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    updateTimes();
    for (auto& b : bands_) {
        b.hz = b.set.hz;
        b.q = b.set.q;
        updateCoefficients(b);
    }
    reset();
}

void DynamicEq::reset() noexcept {
    for (auto& b : bands_) {
        b.mid = {};
        b.side = {};
        b.envBand = b.envAll = 0.0;
        b.habitKnown = false;
        b.habitDb = 0.0;
        b.gain = 1.0;
        b.gainStep = 0.0;
        b.running = b.set.on;
        b.hz = b.set.hz;
        b.q = b.set.q;
        updateCoefficients(b);
    }
    countdown_ = kControlPeriod;
}

void DynamicEq::updateTimes() noexcept {
    attackCoef_ = std::exp(-1.0 / (std::max(attackMs_, 0.05) * 1.0e-3 * sampleRate_));
    releaseCoef_ = std::exp(-1.0 / (std::max(releaseMs_, 1.0) * 1.0e-3 * sampleRate_));
    habitCoef_ = 1.0 - std::exp(-static_cast<double>(kControlPeriod) / (kHabitSeconds * sampleRate_));
}

void DynamicEq::updateCoefficients(Band& b) const noexcept {
    const double f = std::clamp(b.hz, 10.0, 0.45 * sampleRate_);
    const double g = std::tan(std::numbers::pi * f / sampleRate_);
    b.k = 1.0 / std::max(b.q, 0.05);
    b.a1 = 1.0 / (1.0 + g * (g + b.k));
    b.a2 = g * b.a1;
    b.a3 = g * b.a2;
}

void DynamicEq::setParameter(ParamId id, double plain) noexcept {
    if (!std::isfinite(plain)) return;
    if (id == ParamId::MatchDynAttack) {
        attackMs_ = plain;
        updateTimes();
        return;
    }
    if (id == ParamId::MatchDynRelease) {
        releaseMs_ = plain;
        updateTimes();
        return;
    }
    const auto raw = static_cast<std::uint32_t>(id), first = static_cast<std::uint32_t>(ParamId::MatchDynFirst);
    if (raw < first || raw >= first + 6u * kBands) return;
    Band& b = bands_[(raw - first) / 6u];
    switch (static_cast<DynControl>((raw - first) % 6u)) {
    case DynControl::On:
        if (plain >= 0.5 && !b.set.on && !b.running) {
            // Allumée : départ propre (filtre vide, habitude à réapprendre).
            b.mid = {};
            b.side = {};
            b.envBand = b.envAll = 0.0;
            b.habitKnown = false;
            b.gain = 1.0;
            b.gainStep = 0.0;
        }
        b.set.on = plain >= 0.5;
        if (b.set.on) b.running = true;
        break;
    case DynControl::Freq: b.set.hz = std::clamp(plain, 20.0, 20000.0); break;
    case DynControl::Q: b.set.q = std::clamp(plain, 0.3, 8.0); break;
    case DynControl::Range: b.set.rangeDb = std::clamp(plain, -18.0, 12.0); break;
    case DynControl::Threshold: b.set.thresholdDb = std::clamp(plain, 0.0, 18.0); break;
    case DynControl::Channel: b.set.channel = static_cast<DynChannel>(std::clamp(static_cast<int>(std::lround(plain)), 0, 2)); break;
    }
}

bool DynamicEq::active() const noexcept {
    for (const auto& b : bands_)
        if (b.running) return true;
    return false;
}

double DynamicEq::gainDb(int band) const noexcept {
    if (band < 0 || band >= kBands) return 0.0;
    const double g = bands_[static_cast<std::size_t>(band)].gain;
    return g > 1.0e-6 ? 20.0 * std::log10(g) : -120.0;
}

void DynamicEq::control(Band& b) noexcept {
    if (!b.running) return;
    // Fréquence et Q glissent (en log) vers le réglage : pas de saut de filtre.
    const double hzRatio = b.set.hz / b.hz, qRatio = b.set.q / b.q;
    if (std::fabs(hzRatio - 1.0) > 1.0e-4 || std::fabs(qRatio - 1.0) > 1.0e-4) {
        b.hz = std::fabs(hzRatio - 1.0) > 1.0e-4 ? b.hz * std::pow(hzRatio, 0.25) : b.set.hz;
        b.q = std::fabs(qRatio - 1.0) > 1.0e-4 ? b.q * std::pow(qRatio, 0.25) : b.set.q;
        updateCoefficients(b);
    }
    double target = 1.0;
    if (b.set.on && b.envAll > kGatePower) {
        const double ratioDb = 10.0 * std::log10((b.envBand + 1.0e-30) / b.envAll);
        if (!b.habitKnown) {
            b.habitDb = ratioDb;
            b.habitKnown = true;
        } else {
            // L'habitude monte deux fois moins vite qu'elle ne descend : un passage
            // trop chargé ne devient pas la norme en quelques secondes.
            const double c = ratioDb > b.habitDb ? 0.5 * habitCoef_ : habitCoef_;
            b.habitDb += c * (ratioDb - b.habitDb);
        }
        const double over = ratioDb - b.habitDb - b.set.thresholdDb;
        double d = 0.0;
        if (over >= 0.5 * kKneeDb) d = over;
        else if (over > -0.5 * kKneeDb) d = (over + 0.5 * kKneeDb) * (over + 0.5 * kKneeDb) / (2.0 * kKneeDb);
        const double range = b.set.rangeDb;
        const double db = range < 0.0 ? -std::min(-range, kSlope * d) : std::min(range, kSlope * d);
        target = std::pow(10.0, db / 20.0);
    }
    if (std::fabs(target - 1.0) < 1.0e-9 && std::fabs(b.gain - 1.0) < 1.0e-6) b.gain = target = 1.0; // rien à corriger : au bit près
    b.gainStep = (target - b.gain) / kControlPeriod;
    if (!b.set.on && std::fabs(b.gain - 1.0) < 1.0e-7) {
        // Éteinte et revenue à 0 dB : la bande ne calcule plus rien.
        b.gain = 1.0;
        b.gainStep = 0.0;
        b.running = false;
        b.mid = {};
        b.side = {};
        b.envBand = b.envAll = 0.0;
        b.habitKnown = false;
    }
}

void DynamicEq::process(double& mid, double& side) noexcept {
    for (auto& b : bands_) {
        if (!b.running) continue;
        const auto tick = [&b](Svf& s, double x) {
            const double v3 = x - s.ic2;
            const double v1 = b.a1 * s.ic1 + b.a2 * v3;
            const double v2 = s.ic2 + b.a2 * s.ic1 + b.a3 * v3;
            s.ic1 = 2.0 * v1 - s.ic1;
            s.ic2 = 2.0 * v2 - s.ic2;
            return b.k * v1; // passe-bande, 0 dB au centre
        };
        double bandPower = 0.0, allPower = 0.0, bpMid = 0.0, bpSide = 0.0;
        const bool doMid = b.set.channel != DynChannel::Side, doSide = b.set.channel != DynChannel::Mid;
        if (doMid) {
            bpMid = tick(b.mid, mid);
            bandPower += bpMid * bpMid;
            allPower += mid * mid;
        }
        if (doSide) {
            bpSide = tick(b.side, side);
            bandPower += bpSide * bpSide;
            allPower += side * side;
        }
        const auto follow = [this](double& env, double p) { env = p + (p > env ? attackCoef_ : releaseCoef_) * (env - p); };
        follow(b.envBand, bandPower);
        follow(b.envAll, allPower);
        if (b.envAll < 1.0e-30) b.envAll = 0.0; // pas de nombres dénormaux dans le silence
        if (b.envBand < 1.0e-30) b.envBand = 0.0;
        b.gain += b.gainStep;
        const double extra = b.gain - 1.0;
        if (extra != 0.0) {
            if (doMid) mid += extra * bpMid;
            if (doSide) side += extra * bpSide;
        }
    }
    if (--countdown_ <= 0) {
        countdown_ = kControlPeriod;
        for (auto& b : bands_) control(b);
    }
}

} // namespace omnis::dsp
