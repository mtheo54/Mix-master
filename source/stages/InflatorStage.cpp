// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/InflatorStage.cpp
// =============================================================================
#include "InflatorStage.h"

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

/// « Warmth » 100 % : courbe décentrée d'un quart de sa pleine échelle.
constexpr double kMaxBias = 0.25;

} // namespace

double InflatorCurve::exponentFor(double curve) noexcept { return 2.0 + 4.0 * (std::clamp(curve, -50.0, 50.0) + 50.0) / 100.0; }

double InflatorCurve::shape(double x, double m) noexcept {
    const double u = std::fabs(x);
    const double g = u >= 1.0 ? 1.0 : u + (u - std::pow(u, m)) / (m - 1.0);
    return x < 0.0 ? -g : g;
}

double InflatorCurve::slope(double x, double m) noexcept {
    const double u = std::fabs(x);
    if (u >= 1.0) return 0.0;
    return 1.0 + (1.0 - m * std::pow(u, m - 1.0)) / (m - 1.0);
}

int InflatorStage::splitKernelLengthFor(double sampleRate) noexcept {
    if (sampleRate <= 50000.0) return 4096;
    if (sampleRate <= 100000.0) return 8192;
    if (sampleRate <= 200000.0) return 16384;
    return 32768;
}

InflatorStage::InflatorStage() {
    lowEffect_.snapTo(defaultOf(ParamId::InflLowEffect) * 0.01);
    highEffect_.snapTo(defaultOf(ParamId::InflHighEffect) * 0.01);
    drive_.snapTo(dbToGain(defaultOf(ParamId::InflDrive)));
    exponent_.snapTo(InflatorCurve::exponentFor(defaultOf(ParamId::InflCurve)));
    bias_.snapTo(kMaxBias * defaultOf(ParamId::InflWarmth) * 0.01);
    output_.snapTo(dbToGain(defaultOf(ParamId::InflOutput)));
    splitLog_.snapTo(std::log(defaultOf(ParamId::InflSplit)));
    splitLog_.setSnapThreshold(1e-6);
    requestedStages_ = static_cast<int>(defaultOf(ParamId::InflOversampling));
}

InflatorStage::~InflatorStage() { bank_.stop(); }

void InflatorStage::prepare(const ProcessSpec& spec) {
    bank_.stop();
    sampleRate_ = spec.sampleRate;
    const std::pair<OnePoleSmoother*, ParamId> smoothers[] = {
        {&lowEffect_, ParamId::InflLowEffect}, {&highEffect_, ParamId::InflHighEffect}, {&drive_, ParamId::InflDrive},
        {&exponent_, ParamId::InflCurve},      {&bias_, ParamId::InflWarmth},          {&output_, ParamId::InflOutput},
        {&splitLog_, ParamId::InflSplit}};
    for (const auto& [s, id] : smoothers) {
        s->prepare(sampleRate_, smoothingOf(id));
        s->snapTo(s->target());
    }

    // Séparation de bandes à phase linéaire (même filtre que le clipper).
    const int n = splitKernelLengthFor(sampleRate_);
    const int b = n / 32;
    splitter_.prepare(b, n);
    bank_.prepare(sampleRate_, n, b, realtime_, SplitKernelRequest{std::exp(splitLog_.current())});
    activeSlot_ = bank_.initialSlot();
    fadingSlot_ = nullptr;
    fadeRamp_.resize(static_cast<std::size_t>(b));
    for (int i = 0; i < b; ++i)
        fadeRamp_[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(std::numbers::pi * (i + 0.5) / b);
    splitLatency_ = n / 2 + b;
    for (auto& d : inputDelay_) d.assign(static_cast<std::size_t>(splitLatency_), 0.0);

    const auto m = static_cast<std::size_t>(spec.maxBlockSize);
    for (Band* band : {&low_, &high_}) {
        band->oversampler.prepare(spec.maxBlockSize);
        band->oversampler.setStages(requestedStages_);
        for (auto& buf : band->buffer) buf.assign(m, 0.0);
        for (auto& buf : band->harmonics) buf.assign(m, 0.0);
    }
    for (auto& d : dry_) d.assign(m, 0.0);
    for (int s = 0; s <= dsp::Oversampler::kMaxStages; ++s)
        osDelayForStages_[static_cast<std::size_t>(s)] =
            static_cast<int>(std::lround(dsp::Oversampler::phaseDelaySamples(s, sampleRate_)));
    osMaxDelay_ = osDelayForStages_[static_cast<std::size_t>(dsp::Oversampler::kMaxStages)];
    align_.prepare(6, osMaxDelay_);
    align_.setDelay(osDelayForStages_[static_cast<std::size_t>(requestedStages_)]);
    padding_.prepare(2, osMaxDelay_);
    padding_.setDelay(osMaxDelay_ - osDelayForStages_[static_cast<std::size_t>(requestedStages_)]);
    latency_ = splitLatency_ + osMaxDelay_;

    for (auto* v : {&eLow_, &eHigh_, &drives_, &exponents_, &biases_, &restShape_, &restSlope_, &outs_}) v->assign(m, 0.0);
    dcCoefficient_ = std::exp(-2.0 * std::numbers::pi * 5.0 / (sampleRate_ * low_.oversampler.factor()));
    reset();
}

void InflatorStage::reset() noexcept {
    splitter_.reset();
    framePos_ = 0;
    if (fadingSlot_) {
        SplitBank::release(fadingSlot_);
        fadingSlot_ = nullptr;
    }
    for (auto& d : inputDelay_) std::fill(d.begin(), d.end(), 0.0);
    inputDelayPos_ = 0;
    for (Band* band : {&low_, &high_}) {
        band->oversampler.reset();
        band->dcX1.fill(0.0);
        band->dcY1.fill(0.0);
    }
    align_.reset();
    padding_.reset();
}

void InflatorStage::setParameter(ParamId id, double plain, bool immediate) noexcept {
    const auto set = [immediate](OnePoleSmoother& s, double v) {
        if (immediate)
            s.snapTo(v);
        else
            s.setTarget(v);
    };
    switch (id) {
        case ParamId::InflLowEffect: set(lowEffect_, plain * 0.01); break;
        case ParamId::InflHighEffect: set(highEffect_, plain * 0.01); break;
        case ParamId::InflDrive: set(drive_, dbToGain(plain)); break;
        case ParamId::InflCurve: set(exponent_, InflatorCurve::exponentFor(plain)); break;
        case ParamId::InflWarmth: set(bias_, kMaxBias * plain * 0.01); break;
        case ParamId::InflOutput: set(output_, dbToGain(plain)); break;
        case ParamId::InflSplit: set(splitLog_, std::log(plain)); break;
        case ParamId::InflOversampling: requestedStages_ = std::clamp(static_cast<int>(plain), 0, dsp::Oversampler::kMaxStages); break;
        default: break;
    }
}

void InflatorStage::applyOversamplingChoice() noexcept {
    if (requestedStages_ == low_.oversampler.stages()) return;
    // Réglage de qualité : courte discontinuité acceptée (comme le clipper).
    low_.oversampler.setStages(requestedStages_);
    high_.oversampler.setStages(requestedStages_);
    align_.setDelay(osDelayForStages_[static_cast<std::size_t>(requestedStages_)]);
    padding_.setDelay(osMaxDelay_ - osDelayForStages_[static_cast<std::size_t>(requestedStages_)]);
    dcCoefficient_ = std::exp(-2.0 * std::numbers::pi * 5.0 / (sampleRate_ * low_.oversampler.factor()));
}

void InflatorStage::splitFrameBoundary() noexcept {
    if (fadingSlot_) {
        SplitBank::release(fadingSlot_);
        fadingSlot_ = nullptr;
    }
    bank_.request(SplitKernelRequest{std::exp(splitLog_.current())});
    if (SplitBank::Slot* ready = bank_.takeReady()) {
        SplitBank::markRetiring(activeSlot_);
        fadingSlot_ = activeSlot_;
        activeSlot_ = ready;
    }
    splitter_.processFrame(activeSlot_->kernel, fadingSlot_ ? &fadingSlot_->kernel : nullptr);
    framePos_ = 0;
}

void InflatorStage::harmonicsOf(Band& band, int numSamples) noexcept {
    const double* in[2] = {band.buffer[0].data(), band.buffer[1].data()};
    double* const* up = band.oversampler.upsample(in, numSamples);
    const int stages = band.oversampler.stages();
    const int total = numSamples << stages;
    const double r = dcCoefficient_;
    for (int ch = 0; ch < 2; ++ch) {
        double* x = up[ch];
        double x1 = band.dcX1[static_cast<std::size_t>(ch)], y1 = band.dcY1[static_cast<std::size_t>(ch)];
        for (int j = 0; j < total; ++j) {
            const auto b = static_cast<std::size_t>(j >> stages);
            const double d = drives_[b];
            // Ce que la courbe ajoute au-delà de sa pente au repos : les harmoniques.
            const double shaped = (InflatorCurve::shape(d * x[j] + biases_[b], exponents_[b]) - restShape_[b]) / d;
            const double nl = shaped - restSlope_[b] * x[j];
            // Le décentrage (chaleur) crée du continu : bloqueur de DC à 5 Hz.
            const double y = nl - x1 + r * y1;
            x1 = nl;
            y1 = y;
            x[j] = y;
        }
        band.dcX1[static_cast<std::size_t>(ch)] = x1;
        band.dcY1[static_cast<std::size_t>(ch)] = y1;
    }
    double* out[2] = {band.harmonics[0].data(), band.harmonics[1].data()};
    band.oversampler.downsample(out, numSamples);
}

void InflatorStage::process(double* const* channels, int numSamples) noexcept {
    applyOversamplingChoice();

    double lowPeak = 0.0, highPeak = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        const auto k = static_cast<std::size_t>(i);
        eLow_[k] = lowEffect_.next();
        eHigh_[k] = highEffect_.next();
        lowPeak = std::max(lowPeak, eLow_[k]);
        highPeak = std::max(highPeak, eHigh_[k]);
        drives_[k] = drive_.next();
        exponents_[k] = exponent_.next();
        biases_[k] = bias_.next();
        restShape_[k] = InflatorCurve::shape(biases_[k], exponents_[k]);
        restSlope_[k] = InflatorCurve::slope(biases_[k], exponents_[k]);
        outs_[k] = output_.next();
        splitLog_.next();

        // Séparation : bas = FIR LP, haut = entrée retardée − bas ; sec = entrée retardée.
        const double l = channels[0][i], r = channels[1][i];
        splitter_.write(framePos_, l, r);
        double lowL = splitter_.outputA(framePos_);
        double lowR = splitter_.outputB(framePos_);
        if (fadingSlot_) {
            const double t = fadeRamp_[static_cast<std::size_t>(framePos_)];
            lowL = splitter_.secondaryA(framePos_) + t * (lowL - splitter_.secondaryA(framePos_));
            lowR = splitter_.secondaryB(framePos_) + t * (lowR - splitter_.secondaryB(framePos_));
        }
        const auto dp = static_cast<std::size_t>(inputDelayPos_);
        const double dl = inputDelay_[0][dp], dr = inputDelay_[1][dp];
        inputDelay_[0][dp] = l;
        inputDelay_[1][dp] = r;
        if (++inputDelayPos_ == splitLatency_) inputDelayPos_ = 0;
        dry_[0][k] = dl;
        dry_[1][k] = dr;
        low_.buffer[0][k] = lowL;
        low_.buffer[1][k] = lowR;
        high_.buffer[0][k] = dl - lowL;
        high_.buffer[1][k] = dr - lowR;

        if (++framePos_ == splitter_.blockSize()) splitFrameBoundary();
    }

    // Une bande à 0 % tout le bloc : rien à calculer (cas par défaut).
    for (auto [band, peak] : {std::pair{&low_, lowPeak}, std::pair{&high_, highPeak}}) {
        if (peak > 0.0)
            harmonicsOf(*band, numSamples);
        else
            for (auto& h : band->harmonics) std::fill(h.begin(), h.begin() + numSamples, 0.0);
    }

    // Sec et bandes retardés comme les harmoniques (retard de l'oversampling).
    double* aligned[6] = {dry_[0].data(), dry_[1].data(), low_.buffer[0].data(), low_.buffer[1].data(), high_.buffer[0].data(),
                          high_.buffer[1].data()};
    align_.process(aligned, 6, numSamples);

    for (int i = 0; i < numSamples; ++i) {
        const auto k = static_cast<std::size_t>(i);
        const double density = restSlope_[k] - 1.0;
        for (int ch = 0; ch < 2; ++ch) {
            const auto c = static_cast<std::size_t>(ch);
            const double lowAdd = eLow_[k] * (density * low_.buffer[c][k] + low_.harmonics[c][k]);
            const double highAdd = eHigh_[k] * (density * high_.buffer[c][k] + high_.harmonics[c][k]);
            channels[ch][i] = outs_[k] * (dry_[c][k] + lowAdd + highAdd);
        }
    }
    padding_.process(channels, 2, numSamples);
}

} // namespace omnis
