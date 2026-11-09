// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/CompressorStage.cpp
// =============================================================================
#include "CompressorStage.h"

#include "core/Decibels.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace omnis {

using params::ParamId;

namespace {

double defaultOf(ParamId id) {
    const auto* spec = params::findParameter(id);
    return spec ? spec->defaultValue : 0.0;
}

double smoothingOf(ParamId id) {
    const auto* spec = params::findParameter(id);
    return spec ? spec->smoothingMs : 0.0;
}

double coefficient(double ms, double sampleRate) noexcept { return ms <= 0.0 ? 0.0 : std::exp(-1.0 / (ms * 1.0e-3 * sampleRate)); }

/// Fenêtre RMS du détecteur (ms) ; 0 : détection crête (FET).
double rmsWindowOf(int character) noexcept {
    switch (character) {
        case 0: return 10.0; // Opto
        case 1: return 0.0;  // FET
        case 3: return 20.0; // Vari-mu
        default: return 5.0; // VCA
    }
}

constexpr double kSineCrest = 3.0103; // un sinus : crête = RMS + 3 dB (le seuil se lit en crête)

} // namespace

double CompressorCurve::kneeOf(int character) noexcept {
    switch (character) {
        case 0: return 10.0;
        case 1: return 3.0;
        case 3: return 18.0;
        default: return 6.0;
    }
}

double CompressorCurve::gainReductionDb(double x, double threshold, double ratio, double knee) noexcept {
    const double slope = 1.0 / std::max(1.0, ratio) - 1.0; // ≤ 0
    const double over = x - threshold;
    if (knee > 0.0 && std::fabs(over) <= 0.5 * knee) {
        const double t = over + 0.5 * knee;
        return slope * t * t / (2.0 * knee);
    }
    return over > 0.0 ? slope * over : 0.0;
}

CompressorStage::CompressorStage() {
    character_ = static_cast<int>(defaultOf(ParamId::CompCharacter));
    threshold_.snapTo(defaultOf(ParamId::CompThreshold));
    ratio_.snapTo(defaultOf(ParamId::CompRatio));
    makeup_.snapTo(defaultOf(ParamId::CompMakeup));
    mix_.snapTo(defaultOf(ParamId::CompMix) * 0.01);
    attackMs_ = defaultOf(ParamId::CompAttack);
    releaseMs_ = defaultOf(ParamId::CompRelease);
    scHz_ = defaultOf(ParamId::CompScHpf);
}

void CompressorStage::prepare(const ProcessSpec& spec) {
    sampleRate_ = spec.sampleRate;
    const std::pair<OnePoleSmoother*, ParamId> smoothers[] = {
        {&threshold_, ParamId::CompThreshold}, {&ratio_, ParamId::CompRatio}, {&makeup_, ParamId::CompMakeup}, {&mix_, ParamId::CompMix}};
    for (const auto& [s, id] : smoothers) {
        s->prepare(sampleRate_, smoothingOf(id));
        s->snapTo(s->target());
    }
    holdCoef_ = coefficient(1000.0, sampleRate_);
    heldDecayCoef_ = coefficient(3000.0, sampleRate_);
    peakCoef_ = coefficient(10.0, sampleRate_);
    updateTimes();
    scDirty_ = true;
    updateSidechainFilter();
    reset();
}

void CompressorStage::reset() noexcept {
    z1_.fill(0.0);
    z2_.fill(0.0);
    meanSquare_ = 0.0;
    peakHold_ = 0.0;
    envDb_ = 0.0;
    heldDb_ = 0.0;
    lastGrDb_ = 0.0;
}

void CompressorStage::updateTimes() noexcept {
    attackCoef_ = coefficient(attackMs_, sampleRate_);
    releaseCoef_ = coefficient(releaseMs_, sampleRate_);
    rmsCoef_ = coefficient(rmsWindowOf(character_), sampleRate_);
}

void CompressorStage::updateSidechainFilter() noexcept {
    if (!scDirty_) return;
    scDirty_ = false;
    // Passe-haut Butterworth d'ordre 2 (transformée bilinéaire, fréquence pré-déformée).
    const double f = std::clamp(scHz_, 10.0, 0.45 * sampleRate_);
    const double k = std::tan(std::numbers::pi * f / sampleRate_);
    const double q = std::numbers::sqrt2 / 2.0;
    const double norm = 1.0 / (1.0 + k / q + k * k);
    b0_ = norm;
    b1_ = -2.0 * norm;
    b2_ = norm;
    a1_ = 2.0 * (k * k - 1.0) * norm;
    a2_ = (1.0 - k / q + k * k) * norm;
}

void CompressorStage::setParameter(ParamId id, double plain, bool immediate) noexcept {
    const auto set = [immediate](OnePoleSmoother& s, double v) {
        if (immediate)
            s.snapTo(v);
        else
            s.setTarget(v);
    };
    switch (id) {
        case ParamId::CompCharacter:
            character_ = std::clamp(static_cast<int>(std::lround(plain)), 0, 3);
            rmsCoef_ = coefficient(rmsWindowOf(character_), sampleRate_);
            break;
        case ParamId::CompThreshold: set(threshold_, plain); break;
        case ParamId::CompRatio: set(ratio_, plain); break;
        case ParamId::CompAttack:
            attackMs_ = plain;
            attackCoef_ = coefficient(attackMs_, sampleRate_);
            break;
        case ParamId::CompRelease:
            releaseMs_ = plain;
            releaseCoef_ = coefficient(releaseMs_, sampleRate_);
            break;
        case ParamId::CompMakeup: set(makeup_, plain); break;
        case ParamId::CompMix: set(mix_, plain * 0.01); break;
        case ParamId::CompScHpf:
            scHz_ = plain;
            scDirty_ = true;
            break;
        default: break;
    }
}

void CompressorStage::process(double* const* channels, int numSamples) noexcept {
    updateSidechainFilter();
    const double knee = CompressorCurve::kneeOf(character_);
    const bool peak = character_ == 1;
    // Relâchement du bloc : Opto selon la durée de la compression, Vari-mu selon sa profondeur.
    double release = releaseCoef_;
    if (character_ == 0) {
        const double held = std::min(1.0, -heldDb_ / 6.0);
        release = coefficient(releaseMs_ * (0.3 + 1.7 * held), sampleRate_);
    } else if (character_ == 3) {
        release = coefficient(releaseMs_ * (1.0 + std::min(2.0, -envDb_ / 6.0)), sampleRate_);
    }
    double* l = channels[0];
    double* r = channels[1];
    double deepest = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        const double inL = l[i], inR = r[i];
        // Détecteur : passe-haut, puis crête ou RMS (lu en crête équivalente d'un sinus).
        const double hl = b0_ * inL + z1_[0];
        z1_[0] = b1_ * inL - a1_ * hl + z2_[0];
        z2_[0] = b2_ * inL - a2_ * hl;
        const double hr = b0_ * inR + z1_[1];
        z1_[1] = b1_ * inR - a1_ * hr + z2_[1];
        z2_[1] = b2_ * inR - a2_ * hr;
        double levelDb;
        if (peak) {
            // Crête maintenue, qui redescend en 10 ms : la réduction n'ondule pas entre deux crêtes.
            peakHold_ = std::max(std::max(std::fabs(hl), std::fabs(hr)), peakCoef_ * peakHold_);
            levelDb = 20.0 * std::log10(std::max(peakHold_, 1.0e-9));
        } else {
            meanSquare_ = rmsCoef_ * meanSquare_ + (1.0 - rmsCoef_) * 0.5 * (hl * hl + hr * hr);
            levelDb = 10.0 * std::log10(std::max(meanSquare_, 1.0e-18)) + kSineCrest;
        }
        const double target = CompressorCurve::gainReductionDb(levelDb, threshold_.next(), ratio_.next(), knee);
        const bool attacking = target < envDb_;
        envDb_ = target + (attacking ? attackCoef_ : release) * (envDb_ - target);
        // Opto : la mémoire se charge tant que la compression tient (~1 s), se vide dès qu'elle relâche (~3 s).
        heldDb_ = attacking || target <= envDb_ + 0.5 ? envDb_ + holdCoef_ * (heldDb_ - envDb_) : heldDecayCoef_ * heldDb_;
        deepest = std::min(deepest, envDb_);
        const double gain = dbToGain(envDb_ + makeup_.next());
        const double m = mix_.next();
        l[i] = inL + m * (inL * gain - inL);
        r[i] = inR + m * (inR * gain - inR);
    }
    lastGrDb_ = deepest;
}

} // namespace omnis
