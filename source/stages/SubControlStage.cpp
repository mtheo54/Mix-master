// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/SubControlStage.cpp
// =============================================================================
#include "SubControlStage.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace omnis {

using params::ParamId;
using dsp::SvfCoefficients;
using dsp::SvfType;

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

int SubControlStage::kernelLengthFor(double sampleRate) noexcept {
    // 16384 taps à 44.1/48 kHz (résolution ~2.7 Hz, nécessaire au low cut à
    // 16 Hz), puis proportionnel : la résolution en Hz reste la même.
    if (sampleRate <= 50000.0) return 16384;
    if (sampleRate <= 100000.0) return 32768;
    if (sampleRate <= 200000.0) return 65536;
    return 131072;
}

int SubControlStage::blockSizeFor(double sampleRate) noexcept { return kernelLengthFor(sampleRate) / 32; }

SubControlStage::SubControlStage() {
    lowCutLog_.snapTo(std::log(defaultOf(ParamId::LowCutFreq)));
    monoFreqLog_.snapTo(std::log(defaultOf(ParamId::MonoFreq)));
    subFreqLog_.snapTo(std::log(defaultOf(ParamId::SubFreq)));
    monoAmount_.snapTo(defaultOf(ParamId::MonoAmount) * 0.01);
    slopeMix_.snapTo(defaultOf(ParamId::MonoSlope));
    subGainDb_.snapTo(defaultOf(ParamId::SubGain));
    synthAmount_.snapTo(defaultOf(ParamId::SubSynth) * 0.01);
    analogMix_.snapTo(defaultOf(ParamId::SubPhaseMode));
    for (OnePoleSmoother* s : {&lowCutLog_, &monoFreqLog_, &subFreqLog_}) s->setSnapThreshold(1.0e-6);
    subGainDb_.setSnapThreshold(1.0e-5);
}

SubControlStage::~SubControlStage() { bank_.stop(); }

void SubControlStage::prepare(const ProcessSpec& spec) {
    bank_.stop();
    sampleRate_ = spec.sampleRate;
    const auto prep = [&](OnePoleSmoother& s, ParamId id) {
        s.prepare(sampleRate_, smoothingOf(id));
        s.snapTo(s.target());
    };
    prep(lowCutLog_, ParamId::LowCutFreq);
    prep(monoFreqLog_, ParamId::MonoFreq);
    prep(subFreqLog_, ParamId::SubFreq);
    prep(monoAmount_, ParamId::MonoAmount);
    prep(slopeMix_, ParamId::MonoSlope);
    prep(subGainDb_, ParamId::SubGain);
    prep(synthAmount_, ParamId::SubSynth);
    prep(analogMix_, ParamId::SubPhaseMode);

    // Chemin linéaire.
    const int n = kernelLengthFor(sampleRate_);
    const int b = blockSizeFor(sampleRate_);
    convolver_.prepare(b, n);
    bank_.prepare(sampleRate_, n, b, realtime_, currentRequest());
    activeSlot_ = bank_.initialSlot();
    fadingSlot_ = nullptr;
    fadeRamp_.resize(static_cast<std::size_t>(b));
    for (int i = 0; i < b; ++i)
        fadeRamp_[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(std::numbers::pi * (i + 0.5) / b);
    latency_ = n / 2 + b;

    // Chemin analogique, retardé de la même latence.
    analogDelayMid_.assign(static_cast<std::size_t>(latency_), 0.0);
    analogDelaySide_.assign(static_cast<std::size_t>(latency_), 0.0);

    // Synthé sous-harmonique (filtres fixes).
    const double q2 = dsp::butterworthQ(2, 0);
    synthInHp_ = SvfCoefficients::make(SvfType::HighPass, sampleRate_, 45.0, q2);
    synthInLp_ = SvfCoefficients::make(SvfType::LowPass, sampleRate_, 100.0, q2);
    synthOutLp1_ = SvfCoefficients::make(SvfType::LowPass, sampleRate_, 55.0, dsp::butterworthQ(4, 0));
    synthOutLp2_ = SvfCoefficients::make(SvfType::LowPass, sampleRate_, 55.0, dsp::butterworthQ(4, 1));
    synthOutHp_ = SvfCoefficients::make(SvfType::HighPass, sampleRate_, 20.0, q2);
    envAttack_ = 1.0 - std::exp(-1.0 / (0.002 * sampleRate_)); // suiveur crête : 2 ms
    envRelease_ = 1.0 - std::exp(-1.0 / (0.060 * sampleRate_)); //                  60 ms

    updateAnalogCoefficients();
    reset();
}

void SubControlStage::reset() noexcept {
    convolver_.reset();
    framePos_ = 0;
    if (fadingSlot_) {
        SubKernelBank::release(fadingSlot_);
        fadingSlot_ = nullptr;
    }
    for (auto& ch : lowCutState_)
        for (auto& s : ch) s.reset();
    sideXo4_.reset();
    sideXo8_.reset();
    midAp4_.reset();
    midAp8_.reset();
    subShelfState_.reset();
    std::fill(analogDelayMid_.begin(), analogDelayMid_.end(), 0.0);
    std::fill(analogDelaySide_.begin(), analogDelaySide_.end(), 0.0);
    analogDelayPos_ = 0;
    for (dsp::SvfState* s : {&synthInHpS_, &synthInLpS_, &synthOutLp1S_, &synthOutLp2S_, &synthOutHpS_}) s->reset();
    envelope_ = 0.0;
    armed_ = false;
    polarity_ = false;
    clock_ = 0;
}

void SubControlStage::setParameter(ParamId id, double plain, bool immediate) noexcept {
    OnePoleSmoother* target = nullptr;
    double value = plain;
    switch (id) {
        case ParamId::LowCutFreq: target = &lowCutLog_; value = std::log(plain); break;
        case ParamId::MonoFreq: target = &monoFreqLog_; value = std::log(plain); break;
        case ParamId::SubFreq: target = &subFreqLog_; value = std::log(plain); break;
        case ParamId::MonoAmount: target = &monoAmount_; value = plain * 0.01; break;
        case ParamId::MonoSlope: target = &slopeMix_; value = plain >= 0.5 ? 1.0 : 0.0; break;
        case ParamId::SubGain: target = &subGainDb_; break;
        case ParamId::SubSynth: target = &synthAmount_; value = plain * 0.01; break;
        case ParamId::SubPhaseMode: target = &analogMix_; value = plain >= 0.5 ? 1.0 : 0.0; break;
        default: return;
    }
    if (immediate)
        target->snapTo(value);
    else
        target->setTarget(value);
    analogDirty_ = true;
}

SubKernelRequest SubControlStage::currentRequest() const noexcept {
    SubKernelRequest r;
    r.lowCutHz = std::exp(lowCutLog_.current());
    r.monoHz = std::exp(monoFreqLog_.current());
    r.monoAmount = monoAmount_.current();
    r.slopeMix = slopeMix_.current();
    r.subGainDb = subGainDb_.current();
    r.subHz = std::exp(subFreqLog_.current());
    return r;
}

bool SubControlStage::anySmoothing() const noexcept {
    return lowCutLog_.isSmoothing() || monoFreqLog_.isSmoothing() || subFreqLog_.isSmoothing() ||
           subGainDb_.isSmoothing();
}

void SubControlStage::updateAnalogCoefficients() noexcept {
    const double lowCut = std::exp(lowCutLog_.current());
    for (int s = 0; s < 2; ++s)
        lowCut_[static_cast<std::size_t>(s)] =
            SvfCoefficients::make(SvfType::HighPass, sampleRate_, lowCut, dsp::butterworthQ(4, s));
    const double monoFreq = std::exp(monoFreqLog_.current());
    sideXo4_.setup(sampleRate_, monoFreq, dsp::CrossoverSlope::LR4);
    sideXo8_.setup(sampleRate_, monoFreq, dsp::CrossoverSlope::LR8);
    midAp4_.setup(sampleRate_, monoFreq, dsp::CrossoverSlope::LR4);
    midAp8_.setup(sampleRate_, monoFreq, dsp::CrossoverSlope::LR8);
    subShelf_ = SvfCoefficients::make(SvfType::LowShelf, sampleRate_, std::exp(subFreqLog_.current()),
                                      dsp::butterworthQ(2, 0), subGainDb_.current());
    analogDirty_ = false;
}

void SubControlStage::frameBoundary() noexcept {
    // Le fondu de la trame précédente est terminé : l'ancien noyau est libéré.
    if (fadingSlot_) {
        SubKernelBank::release(fadingSlot_);
        fadingSlot_ = nullptr;
    }
    bank_.request(currentRequest());
    if (SubKernelBank::Slot* ready = bank_.takeReady()) {
        SubKernelBank::markRetiring(activeSlot_);
        fadingSlot_ = activeSlot_;
        activeSlot_ = ready;
    }
    convolver_.processFrame(activeSlot_->kernel, fadingSlot_ ? &fadingSlot_->kernel : nullptr);
    framePos_ = 0;
}

double SubControlStage::processSynth(double mid) noexcept {
    const double band = synthInLpS_.process(synthInLp_, synthInHpS_.process(synthInHp_, mid));
    const double a = std::fabs(band);
    envelope_ += (a > envelope_ ? envAttack_ : envRelease_) * (a - envelope_);
    // Hystérésis à ±10 % de l'enveloppe : une alternance complète (− puis +)
    // = un cycle ; changer de signe à chaque cycle donne une carrée à f/2.
    const double threshold = 0.1 * envelope_;
    if (band < -threshold) armed_ = true;
    if (armed_ && band > threshold) {
        armed_ = false;
        polarity_ = !polarity_;
    }
    double sub = synthOutLp1S_.process(synthOutLp1_, (polarity_ ? 1.0 : -1.0) * envelope_);
    sub = synthOutLp2S_.process(synthOutLp2_, sub);
    return synthOutHpS_.process(synthOutHp_, sub);
}

void SubControlStage::process(double* const* channels, int numSamples) noexcept {
    double* left = channels[0];
    double* right = channels[1];
    // π/4 compense la fondamentale d'un carré ±1 (4/π) ; ×1.429 (+3.1 dB)
    // compense filtres de bande, sortie et ondulation du suiveur (mesuré à 80 Hz).
    constexpr double kSynthGain = std::numbers::pi / 4.0 * 1.429;
    const int latency = latency_;

    for (int i = 0; i < numSamples; ++i) {
        // Lisseurs : un pas par échantillon, quel que soit le découpage en blocs.
        lowCutLog_.next();
        monoFreqLog_.next();
        subFreqLog_.next();
        subGainDb_.next();
        const double amount = monoAmount_.next();
        const double slope = slopeMix_.next();
        const double synth = synthAmount_.next();
        const double analog = analogMix_.next();
        if ((clock_++ % kControlInterval) == 0 && (analogDirty_ || anySmoothing())) updateAnalogCoefficients();

        const double l = left[i], r = right[i];

        // ---- LINEAR : entrée M/S dans le convolueur, sortie de la trame précédente.
        convolver_.write(framePos_, 0.5 * (l + r), 0.5 * (l - r));
        double midLin = convolver_.outputA(framePos_);
        double sideLin = convolver_.outputB(framePos_);
        if (fadingSlot_) {
            const double t = fadeRamp_[static_cast<std::size_t>(framePos_)];
            const double oldMid = convolver_.secondaryA(framePos_);
            const double oldSide = convolver_.secondaryB(framePos_);
            midLin = oldMid + t * (midLin - oldMid);
            sideLin = oldSide + t * (sideLin - oldSide);
        }

        // ---- ANALOG : low cut L/R -> M/S -> Mono Maker IIR -> plateau, puis retard.
        double la = l, ra = r;
        for (int s = 0; s < 2; ++s) {
            la = lowCutState_[0][static_cast<std::size_t>(s)].process(lowCut_[static_cast<std::size_t>(s)], la);
            ra = lowCutState_[1][static_cast<std::size_t>(s)].process(lowCut_[static_cast<std::size_t>(s)], ra);
        }
        const double midA = 0.5 * (la + ra);
        const double sideA = 0.5 * (la - ra);
        double sLo4, sHi4, sLo8, sHi8;
        sideXo4_.process(0, sideA, sLo4, sHi4);
        sideXo8_.process(0, sideA, sLo8, sHi8);
        const double keep = 1.0 - amount;
        const double s4 = sHi4 + keep * sLo4;
        const double s8 = sHi8 + keep * sLo8;
        const double m4 = midAp4_.process(0, midA);
        const double m8 = midAp8_.process(0, midA);
        const double midAnalogNow = subShelfState_.process(subShelf_, m4 + slope * (m8 - m4));
        const double sideAnalogNow = s4 + slope * (s8 - s4);
        const auto dp = static_cast<std::size_t>(analogDelayPos_);
        const double midAnalog = analogDelayMid_[dp];
        const double sideAnalog = analogDelaySide_[dp];
        analogDelayMid_[dp] = midAnalogNow;
        analogDelaySide_[dp] = sideAnalogNow;
        if (++analogDelayPos_ == latency) analogDelayPos_ = 0;

        // ---- Fondu Linear / Analog, synthé, retour L/R.
        double mid = midLin + analog * (midAnalog - midLin);
        const double side = sideLin + analog * (sideAnalog - sideLin);
        mid += synth * kSynthGain * processSynth(mid);
        left[i] = mid + side;
        right[i] = mid - side;

        if (++framePos_ == convolver_.blockSize()) frameBoundary();
    }
}

} // namespace omnis
