// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/ClipperStage.cpp
// =============================================================================
#include "ClipperStage.h"

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

} // namespace

ClipperStage::ClipperStage() {
    drive_.snapTo(dbToGain(defaultOf(ParamId::ClipDrive)));
    ceiling_.snapTo(dbToGain(defaultOf(ParamId::ClipCeiling)));
    knee_.snapTo(defaultOf(ParamId::ClipKnee) * 0.01);
    const int mode = static_cast<int>(defaultOf(ParamId::ClipMode));
    for (int m = 0; m < 3; ++m) modeWeight_[static_cast<std::size_t>(m)].snapTo(m == mode ? 1.0 : 0.0);
    requestedStages_ = static_cast<int>(defaultOf(ParamId::ClipOversampling));
}

void ClipperStage::prepare(const ProcessSpec& spec) {
    sampleRate_ = spec.sampleRate;
    drive_.prepare(sampleRate_, smoothingOf(ParamId::ClipDrive));
    ceiling_.prepare(sampleRate_, smoothingOf(ParamId::ClipCeiling));
    knee_.prepare(sampleRate_, smoothingOf(ParamId::ClipKnee));
    for (auto& w : modeWeight_) w.prepare(sampleRate_, smoothingOf(ParamId::ClipMode));
    for (OnePoleSmoother* s : {&drive_, &ceiling_, &knee_, &modeWeight_[0], &modeWeight_[1], &modeWeight_[2]})
        s->snapTo(s->target());

    oversampler_.prepare(spec.maxBlockSize);
    // Latence = retard du pire cas (16x) ; chaque facteur est complété par un
    // retard entier pour que la somme reste constante.
    latency_ = static_cast<int>(std::lround(dsp::Oversampler::phaseDelaySamples(dsp::Oversampler::kMaxStages, sampleRate_)));
    for (int s = 0; s <= dsp::Oversampler::kMaxStages; ++s)
        paddingForStages_[static_cast<std::size_t>(s)] =
            std::max(0, latency_ - static_cast<int>(std::lround(dsp::Oversampler::phaseDelaySamples(s, sampleRate_))));
    padding_.prepare(2, latency_);

    const auto n = static_cast<std::size_t>(spec.maxBlockSize);
    for (auto* v : {&gainIn_, &gainOut_, &knees_, &wHard_, &wSoft_, &wDiode_}) v->assign(n, 0.0);

    oversampler_.setStages(requestedStages_);
    updateDcCoefficient();
    padding_.setDelay(paddingForStages_[static_cast<std::size_t>(requestedStages_)]);
    reset();
}

void ClipperStage::reset() noexcept {
    oversampler_.reset();
    for (auto& c : clippers_) c.reset();
    padding_.reset();
    dcX1_.fill(0.0);
    dcY1_.fill(0.0);
}

void ClipperStage::setParameter(ParamId id, double plain, bool immediate) noexcept {
    const auto set = [immediate](OnePoleSmoother& s, double v) {
        if (immediate)
            s.snapTo(v);
        else
            s.setTarget(v);
    };
    switch (id) {
        case ParamId::ClipDrive: set(drive_, dbToGain(plain)); break;
        case ParamId::ClipCeiling: set(ceiling_, dbToGain(plain)); break;
        case ParamId::ClipKnee: set(knee_, plain * 0.01); break;
        case ParamId::ClipMode: {
            const int mode = static_cast<int>(plain);
            for (int m = 0; m < 3; ++m) set(modeWeight_[static_cast<std::size_t>(m)], m == mode ? 1.0 : 0.0);
            break;
        }
        case ParamId::ClipOversampling: requestedStages_ = std::clamp(static_cast<int>(plain), 0, dsp::Oversampler::kMaxStages); break;
        default: break;
    }
}

void ClipperStage::updateDcCoefficient() noexcept {
    // Bloqueur de DC du 1er ordre à 3 Hz, au taux suréchantillonné :
    // y = x − x₁ + R·y₁,  R = e^(−2π·fc / (fs·facteur)).
    dcCoefficient_ = std::exp(-2.0 * std::numbers::pi * 3.0 / (sampleRate_ * oversampler_.factor()));
}

void ClipperStage::applyOversamplingChoice() noexcept {
    if (requestedStages_ == oversampler_.stages()) return;
    // Changement de qualité : les étages réactivés repartent à zéro et le retard
    // d'alignement change ; une courte discontinuité est inévitable (réglage de
    // qualité, pas un paramètre d'automation).
    oversampler_.setStages(requestedStages_);
    padding_.setDelay(paddingForStages_[static_cast<std::size_t>(requestedStages_)]);
    updateDcCoefficient();
}

void ClipperStage::process(double* const* channels, int numSamples) noexcept {
    applyOversamplingChoice();

    for (int i = 0; i < numSamples; ++i) {
        const double drive = drive_.next();
        const double ceiling = ceiling_.next();
        const auto k = static_cast<std::size_t>(i);
        gainIn_[k] = drive / ceiling;
        gainOut_[k] = ceiling;
        knees_[k] = knee_.next();
        wHard_[k] = modeWeight_[0].next();
        wSoft_[k] = modeWeight_[1].next();
        wDiode_[k] = modeWeight_[2].next();
    }

    double* const* up = oversampler_.upsample(channels, numSamples);
    const int stages = oversampler_.stages();
    const int total = numSamples << stages;
    const double r = dcCoefficient_;
    for (int ch = 0; ch < 2; ++ch) {
        double* x = up[ch];
        auto& clip = clippers_[static_cast<std::size_t>(ch)];
        double x1 = dcX1_[static_cast<std::size_t>(ch)], y1 = dcY1_[static_cast<std::size_t>(ch)];
        for (int j = 0; j < total; ++j) {
            const auto b = static_cast<std::size_t>(j >> stages);
            const auto out = clip.process(x[j] * gainIn_[b], knees_[b], wHard_[b], wSoft_[b], wDiode_[b]);
            // Seule la composante Diode (asymétrique) passe dans le bloqueur de DC.
            const double d = out.diode - x1 + r * y1;
            x1 = out.diode;
            y1 = d;
            x[j] = gainOut_[b] * (out.symmetric + d);
        }
        dcX1_[static_cast<std::size_t>(ch)] = x1;
        dcY1_[static_cast<std::size_t>(ch)] = y1;
    }
    oversampler_.downsample(channels, numSamples);
    padding_.process(channels, 2, numSamples);
}

} // namespace omnis
