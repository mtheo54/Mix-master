// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/Engine.cpp
// =============================================================================
#include "Engine.h"

#include "../core/Decibels.h"

#include <algorithm>
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

void Engine::appendStage(std::unique_ptr<Stage> stage, ParamId onParameter) {
    assert(!prepared_ && "appendStage() doit précéder prepare()");
    if (!stage) return;
    stages_.push_back(std::move(stage));
    slots_.emplace_back();
    slots_.back().onId = onParameter;
    slots_.back().on.snapTo(onParameter == ParamId::Bypass ? 1.0 : (defaultFor(onParameter) >= 0.5 ? 1.0 : 0.0));
    order_.push_back(static_cast<int>(stages_.size()) - 1);
    wantedOrder_ = order_;
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
    for (std::size_t k = 0; k < stages_.size(); ++k) {
        stages_[k]->prepare(spec_);
        Slot& slot = slots_[k];
        slot.latency = stages_[k]->latencySamples();
        latency_ += slot.latency;
        slot.dry.prepare(kNumChannels, slot.latency);
        slot.dry.setDelay(slot.latency);
        slot.on.prepare(sampleRate, 20.0);
        slot.on.snapTo(slot.on.target());
    }

    work_.allocate(kNumChannels, spec_.maxBlockSize);
    dry_.allocate(kNumChannels, spec_.maxBlockSize);
    stageIn_.allocate(kNumChannels, spec_.maxBlockSize);
    delta_.allocate(kNumChannels, spec_.maxBlockSize);
    dryDelay_.prepare(kNumChannels, latency_);
    dryDelay_.setDelay(latency_);
    deltaDelay_.prepare(kNumChannels, latency_);
    orderMix_.prepare(sampleRate, 30.0);
    orderMix_.snapTo(1.0);
    orderHold_ = 0;
    order_ = wantedOrder_;
    deltaMix_.prepare(sampleRate, 20.0);
    deltaMix_.snapTo(0.0);
    deltaActive_ = 0;
    inputLoudness_.prepare(sampleRate, kNumChannels);
    matchGain_.prepare(sampleRate, 1500.0);
    matchGain_.snapTo(1.0);
    matchMix_.prepare(sampleRate, 50.0);
    matchMix_.snapTo(gainMatch_ ? 1.0 : 0.0);

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
    for (auto& slot : slots_) slot.dry.reset();
    stageIn_.clear();
    delta_.clear();
    deltaDelay_.reset();
    inputLoudness_.reset();
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
        case ParamId::ChainOrder: {
            // L'ordre ne vaut que pour la chaîne complète (7 étages) ; appliqué au son sec.
            if (static_cast<int>(stages_.size()) != params::kModules) break;
            const auto order = params::chainOrder(static_cast<int>(plainValue));
            for (int i = 0; i < params::kModules; ++i) wantedOrder_[static_cast<std::size_t>(i)] = static_cast<int>(order[static_cast<std::size_t>(i)]);
            if (!prepared_ || immediate) order_ = wantedOrder_;
            break;
        }
        case ParamId::DeltaListen: deltaWanted_ = std::clamp(static_cast<int>(plainValue), 0, static_cast<int>(stages_.size())); break;
        case ParamId::GainMatch:
            gainMatch_ = plainValue >= 0.5;
            matchMix_.setTarget(gainMatch_ ? 1.0 : 0.0);
            break;
        default: {
            bool isSwitch = false;
            for (auto& slot : slots_)
                if (slot.onId != ParamId::Bypass && slot.onId == id) {
                    slot.on.setTarget(plainValue >= 0.5 ? 1.0 : 0.0);
                    if (immediate) slot.on.snapTo(slot.on.target());
                    isSwitch = true;
                }
            if (!isSwitch)
                for (auto& stage : stages_) stage->setParameter(id, plainValue, immediate);
            break;
        }
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
        case ParamId::GainMatch: matchMix_.snapTo(matchMix_.target()); break;
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

void Engine::updateReorder(int n) noexcept {
    if (orderHold_ > 0) {
        orderHold_ -= n;
        if (orderHold_ <= 0) orderMix_.setTarget(1.0);
        return;
    }
    if (order_ == wantedOrder_) return;
    orderMix_.setTarget(0.0);
    if (orderMix_.current() > 1.0e-4) return;
    // Au son sec : on permute, et on attend que la chaîne ait vidé son passé.
    order_ = wantedOrder_;
    orderMix_.snapTo(0.0);
    orderHold_ = latency_ + spec_.maxBlockSize;
    if (deltaActive_ != 0) deltaActive_ = -1; // à recaler sur le nouvel ordre
}

void Engine::updateDelta() noexcept {
    if (deltaActive_ == deltaWanted_) return;
    deltaMix_.setTarget(0.0);
    if (deltaMix_.current() > 1.0e-4 && deltaActive_ > 0) return;
    deltaActive_ = deltaWanted_;
    deltaMix_.snapTo(0.0);
    deltaDelay_.reset();
    if (deltaActive_ == 0) return;
    // Ce que l'étage change doit encore traverser les étages qui le suivent.
    int after = 0;
    bool seen = false;
    for (const int k : order_) {
        if (seen) after += slots_[static_cast<std::size_t>(k)].latency;
        if (k == deltaActive_ - 1) seen = true;
    }
    deltaDelay_.setDelay(after);
    deltaMix_.setTarget(1.0);
}

void Engine::processChunk(int n) noexcept {
    double* const* w = work_.channels();
    double* const* d = dry_.channels();
    double* const* in = stageIn_.channels();
    double* const* dl = delta_.channels();

    updateReorder(n);
    updateDelta();

    // 1. Chemin sec retardé de la latence totale de la chaîne (PDC interne).
    dryDelay_.process(d, kNumChannels, n);
    inputLoudness_.process(d, n);

    // 2. Chaîne de traitement, dans l'ordre du rack ; chaque étage éteint
    //    rend son entrée retardée de sa latence.
    applyGain(inputGain_, w, n);
    for (const int k : order_) {
        Slot& slot = slots_[static_cast<std::size_t>(k)];
        const bool fullyOn = !slot.on.isSmoothing() && slot.on.current() == 1.0;
        const bool needDry = !fullyOn || deltaActive_ == k + 1;
        if (needDry || slot.latency > 0) {
            for (int ch = 0; ch < kNumChannels; ++ch) std::copy_n(w[ch], n, in[ch]);
            slot.dry.process(in, kNumChannels, n); // tenu à jour même allumé : prêt pour un fondu
        }
        stages_[static_cast<std::size_t>(k)]->process(w, n);
        if (!fullyOn) {
            for (int ch = 0; ch < kNumChannels; ++ch) {
                OnePoleSmoother on = slot.on;
                double* OMNIS_RESTRICT y = w[ch];
                const double* OMNIS_RESTRICT x = in[ch];
                for (int i = 0; i < n; ++i) {
                    const double m = on.next();
                    y[i] = x[i] + m * (y[i] - x[i]);
                }
                if (ch == kNumChannels - 1) slot.on = on;
            }
        }
        if (deltaActive_ == k + 1)
            for (int ch = 0; ch < kNumChannels; ++ch)
                for (int i = 0; i < n; ++i) dl[ch][i] = w[ch][i] - in[ch][i];
    }
    applyGain(outputGain_, w, n);

    // 3. Mélange bypass : out = sec + mix·(traité − sec). Crossfade à gain
    //    constant en amplitude : les deux signaux sont alignés et corrélés.
    //    Une permutation du rack passe par le même chemin (orderMix_).
    if (mix_.isSmoothing() || mix_.current() != 1.0 || orderMix_.isSmoothing() || orderMix_.current() != 1.0) {
        for (int ch = 0; ch < kNumChannels; ++ch) {
            // Les lisseurs sont partagés entre canaux : on rejoue la même rampe.
            OnePoleSmoother mix = mix_, order = orderMix_;
            double* OMNIS_RESTRICT y = w[ch];
            const double* OMNIS_RESTRICT x = d[ch];
            for (int i = 0; i < n; ++i) {
                const double m = mix.next() * order.next();
                y[i] = x[i] + m * (y[i] - x[i]);
            }
            if (ch == kNumChannels - 1) {
                mix_ = mix;
                orderMix_ = order;
            }
        }
    }

    // 4. Mesures sur la sortie master (avant les outils d'écoute et le monitoring).
    loudness_.process(w, n);
    truePeak_.process(w, n);
    stereo_.process(w[0], w[1], n);

    // 5. Écoute Delta : ce que le module change, à la place de la sortie.
    if (deltaActive_ > 0 || deltaMix_.isSmoothing()) {
        if (deltaActive_ > 0) deltaDelay_.process(dl, kNumChannels, n);
        else
            for (int ch = 0; ch < kNumChannels; ++ch) std::fill_n(dl[ch], n, 0.0);
        for (int ch = 0; ch < kNumChannels; ++ch) {
            OnePoleSmoother dm = deltaMix_;
            for (int i = 0; i < n; ++i) {
                const double m = dm.next();
                w[ch][i] += m * (dl[ch][i] - w[ch][i]);
            }
            if (ch == kNumChannels - 1) deltaMix_ = dm;
        }
    }

    // 6. Gain Match : la sortie écoutée ramenée à la sonie de l'entrée.
    if (gainMatch_ || matchMix_.isSmoothing() || matchMix_.current() > 0.0) {
        const double inLufs = inputLoudness_.shortTermLufs(), outLufs = loudness_.shortTermLufs();
        if (std::isfinite(inLufs) && std::isfinite(outLufs) && inLufs > -70.0 && outLufs > -70.0)
            matchGain_.setTarget(dbToGain(std::clamp(inLufs - outLufs, -24.0, 24.0)));
        for (int ch = 0; ch < kNumChannels; ++ch) {
            OnePoleSmoother g = matchGain_, mm = matchMix_;
            for (int i = 0; i < n; ++i) {
                const double gain = g.next(), m = mm.next();
                w[ch][i] *= 1.0 + m * (gain - 1.0);
            }
            if (ch == kNumChannels - 1) {
                matchGain_ = g;
                matchMix_ = mm;
            }
        }
    }

    // 7. Monitoring Stéréo / Mid / Side.
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
