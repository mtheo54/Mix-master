// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/Engine.cpp
// =============================================================================
#include "Engine.h"

#include "../core/Decibels.h"

#include <cassert>
#include <cmath>

namespace omnis {

using params::ParamId;

namespace {

double smoothingMsFor(ParamId id) {
    const auto* spec = params::findParameter(id);
    return spec ? spec->smoothingMs : 0.0;
}

double defaultFor(ParamId id) {
    const auto* spec = params::findParameter(id);
    return spec ? spec->defaultValue : 0.0;
}

} // namespace

Engine::Engine() {
    inputGain_.snapTo(dbToGain(defaultFor(ParamId::InputGain)));
    outputGain_.snapTo(dbToGain(defaultFor(ParamId::OutputGain)));
    mix_.snapTo(defaultFor(ParamId::Bypass) >= 0.5 ? 0.0 : 1.0);
    setMonitorTargets(static_cast<int>(defaultFor(ParamId::Monitor)), true);
}

Engine::~Engine() = default;

void Engine::appendStage(std::unique_ptr<Stage> stage) {
    assert(!prepared_ && "appendStage() doit précéder prepare()");
    if (stage) stages_.push_back(std::move(stage));
}

void Engine::setRealtimeMode(bool realtime) noexcept {
    for (auto& stage : stages_) stage->setRealtime(realtime);
}

void Engine::prepare(double sampleRate, int maxBlockSize) {
    assert(sampleRate > 0.0 && maxBlockSize > 0);
    spec_.sampleRate = sampleRate;
    spec_.maxBlockSize = std::max(1, maxBlockSize);
    spec_.numChannels = kNumChannels;

    latency_ = 0;
    for (auto& stage : stages_) {
        stage->prepare(spec_);
        latency_ += stage->latencySamples();
    }

    work_.allocate(kNumChannels, spec_.maxBlockSize);
    dry_.allocate(kNumChannels, spec_.maxBlockSize);
    dryDelay_.prepare(kNumChannels, latency_);
    dryDelay_.setDelay(latency_);

    const auto prepareSmoother = [&](OnePoleSmoother& s, ParamId id) {
        s.prepare(sampleRate, smoothingMsFor(id));
        s.snapTo(s.target());
    };
    prepareSmoother(inputGain_, ParamId::InputGain);
    prepareSmoother(outputGain_, ParamId::OutputGain);
    prepareSmoother(mix_, ParamId::Bypass);
    for (OnePoleSmoother* m : {&mLL_, &mLR_, &mRL_, &mRR_}) prepareSmoother(*m, ParamId::Monitor);

    loudness_.prepare(sampleRate, kNumChannels);
    truePeak_.prepare(kNumChannels);
    stereo_.prepare(sampleRate);

    prepared_ = true;
    reset();
}

void Engine::reset() noexcept {
    work_.clear();
    dry_.clear();
    dryDelay_.reset();
    for (auto& stage : stages_) stage->reset();
    loudness_.reset();
    truePeak_.reset();
    stereo_.reset();
}

void Engine::recoverFromFault() noexcept {
    // Un étage a produit un résultat non fini : on repart d'un état propre
    // plutôt que de rester muet (ou de propager des NaN à l'hôte).
    ++faults_;
    reset();
}

void Engine::setMonitorTargets(int mode, bool immediate) noexcept {
    double ll = 1.0, lr = 0.0, rl = 0.0, rr = 1.0;
    switch (static_cast<params::MonitorMode>(mode)) {
        case params::MonitorMode::Mid: // L' = R' = (L + R) / 2
            ll = lr = rl = rr = 0.5;
            break;
        case params::MonitorMode::Side: // L' = R' = (L - R) / 2
            ll = rl = 0.5;
            lr = rr = -0.5;
            break;
        case params::MonitorMode::Stereo:
        default:
            break;
    }
    if (immediate) {
        mLL_.snapTo(ll);
        mLR_.snapTo(lr);
        mRL_.snapTo(rl);
        mRR_.snapTo(rr);
    } else {
        mLL_.setTarget(ll);
        mLR_.setTarget(lr);
        mRL_.setTarget(rl);
        mRR_.setTarget(rr);
    }
}

void Engine::setParameter(ParamId id, double plainValue) noexcept { applyParameter(id, plainValue, false); }

void Engine::applyParameter(ParamId id, double plainValue, bool immediate) noexcept {
    const auto* spec = params::findParameter(id);
    if (spec) plainValue = params::clampPlain(*spec, plainValue);
    switch (id) {
        case ParamId::Bypass: mix_.setTarget(plainValue >= 0.5 ? 0.0 : 1.0); break;
        case ParamId::InputGain: inputGain_.setTarget(dbToGain(plainValue)); break;
        case ParamId::OutputGain: outputGain_.setTarget(dbToGain(plainValue)); break;
        case ParamId::Monitor: setMonitorTargets(static_cast<int>(plainValue), false); break;
        default:
            for (auto& stage : stages_) stage->setParameter(id, plainValue, immediate);
            break;
    }
}

void Engine::setParameterImmediate(ParamId id, double plainValue) noexcept {
    applyParameter(id, plainValue, true);
    switch (id) {
        case ParamId::Bypass: mix_.snapTo(mix_.target()); break;
        case ParamId::InputGain: inputGain_.snapTo(inputGain_.target()); break;
        case ParamId::OutputGain: outputGain_.snapTo(outputGain_.target()); break;
        case ParamId::Monitor:
            for (OnePoleSmoother* m : {&mLL_, &mLR_, &mRL_, &mRR_}) m->snapTo(m->target());
            break;
        default: break;
    }
}

void Engine::applyGain(OnePoleSmoother& gain, double* const* channels, int numSamples) noexcept {
    double* OMNIS_RESTRICT left = channels[0];
    double* OMNIS_RESTRICT right = channels[1];
    if (!gain.isSmoothing()) {
        const double g = gain.current();
        if (g == 1.0) return; // chemin neutre : bit-exact
        for (int i = 0; i < numSamples; ++i) {
            left[i] *= g;
            right[i] *= g;
        }
        return;
    }
    // Gain lié stéréo : un seul lisseur pour L et R (pas de dérive d'image).
    for (int i = 0; i < numSamples; ++i) {
        const double g = gain.next();
        left[i] *= g;
        right[i] *= g;
    }
}

void Engine::applyMonitorMatrix(int numSamples) noexcept {
    const bool smoothing = mLL_.isSmoothing() || mLR_.isSmoothing() || mRL_.isSmoothing() || mRR_.isSmoothing();
    if (!smoothing && mLL_.current() == 1.0 && mLR_.current() == 0.0 && mRL_.current() == 0.0 &&
        mRR_.current() == 1.0)
        return; // stéréo : identité exacte

    double* OMNIS_RESTRICT left = work_.channel(0);
    double* OMNIS_RESTRICT right = work_.channel(1);
    for (int i = 0; i < numSamples; ++i) {
        const double l = left[i];
        const double r = right[i];
        left[i] = mLL_.next() * l + mLR_.next() * r;
        right[i] = mRL_.next() * l + mRR_.next() * r;
    }
}

void Engine::processChunk(int n) noexcept {
    double* const* w = work_.channels();
    double* const* d = dry_.channels();

    // 1. Chemin sec retardé de la latence totale de la chaîne (PDC interne).
    dryDelay_.process(d, kNumChannels, n);

    // 2. Chaîne de traitement.
    applyGain(inputGain_, w, n);
    for (auto& stage : stages_) stage->process(w, n);
    applyGain(outputGain_, w, n);

    // 3. Mélange bypass : out = sec + mix·(traité − sec). Crossfade à gain
    //    constant en amplitude : les deux signaux sont alignés et corrélés.
    if (mix_.isSmoothing() || mix_.current() != 1.0) {
        for (int ch = 0; ch < kNumChannels; ++ch) {
            // Le lisseur est partagé entre canaux : on rejoue la même rampe.
            OnePoleSmoother mix = mix_;
            double* OMNIS_RESTRICT y = w[ch];
            const double* OMNIS_RESTRICT x = d[ch];
            for (int i = 0; i < n; ++i) {
                const double m = mix.next();
                y[i] = x[i] + m * (y[i] - x[i]);
            }
            if (ch == kNumChannels - 1) mix_ = mix;
        }
    }

    // 4. Mesures sur la sortie master (avant monitoring).
    loudness_.process(w, n);
    truePeak_.process(w, n);
    stereo_.process(w[0], w[1], n);

    // 5. Monitoring Stéréo / Mid / Side.
    applyMonitorMatrix(n);
}

MeterReadout Engine::meters() const noexcept {
    MeterReadout m;
    m.momentaryLufs = loudness_.momentaryLufs();
    m.shortTermLufs = loudness_.shortTermLufs();
    m.integratedLufs = loudness_.integratedLufs();
    m.loudnessRangeLu = loudness_.loudnessRangeLu();
    m.truePeakDb = gainToDb(truePeak_.truePeakMax());
    m.samplePeakDb = gainToDb(truePeak_.samplePeakMax());
    m.correlation = stereo_.correlation();
    m.sideToMidDb = stereo_.sideToMidDb();
    return m;
}

void Engine::resetLoudnessHistory() noexcept {
    loudness_.resetIntegrated();
    truePeak_.resetPeaks();
}

} // namespace omnis
