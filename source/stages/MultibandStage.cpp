// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/MultibandStage.cpp
// =============================================================================
#include "MultibandStage.h"

#include "core/Decibels.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace omnis {

using params::MbControl;
using params::ParamId;

namespace {

double defaultOf(ParamId id) {
    const auto* spec = params::findParameter(id);
    return spec ? spec->defaultValue : 0.0;
}

double lowpass(double f, double fc) {
    const double w = f / fc, w2 = w * w;
    return 1.0 / (1.0 + w2 * w2);
}

double coefficient(double ms, double sampleRate) { return 1.0 - std::exp(-1.0 / (0.001 * ms * sampleRate)); }

} // namespace

bool MbKernelRequest::nearlyEquals(const MbKernelRequest& o) const noexcept {
    const auto close = [](double a, double b) { return std::fabs(a - b) <= 1e-4 * b; };
    return band == o.band && close(splitLow, o.splitLow) && close(splitMid, o.splitMid) && close(splitHigh, o.splitHigh);
}

double MbKernelRequest::magnitude(double f) const noexcept {
    const double l1 = lowpass(f, splitLow), l2 = lowpass(f, splitMid), l3 = lowpass(f, splitHigh);
    switch (band) {
        case 0: return l1;
        case 1: return std::max(0.0, l2 - l1);
        default: return std::max(0.0, l3 - l2);
    }
}

int MultibandStage::kernelLengthFor(double sampleRate) noexcept {
    // Comme la séparation du clipper : 8192 taps à 44.1 / 48 kHz (~5 Hz).
    if (sampleRate <= 50000.0) return 8192;
    if (sampleRate <= 100000.0) return 16384;
    if (sampleRate <= 200000.0) return 32768;
    return 65536;
}

MultibandStage::MultibandStage() {
    const ParamId splits[3] = {ParamId::MbSplitLow, ParamId::MbSplitMid, ParamId::MbSplitHigh};
    for (int s = 0; s < 3; ++s) {
        splitLog_[s].snapTo(std::log(defaultOf(splits[s])));
        splitLog_[s].setSnapThreshold(1.0e-6);
    }
    for (int b = 0; b < params::kMbBands; ++b) {
        auto& band = bands_[static_cast<std::size_t>(b)];
        band.thresholdDb = defaultOf(params::mbId(b, MbControl::Threshold));
        band.updateKneeStart();
        band.ratio = defaultOf(params::mbId(b, MbControl::Ratio));
        band.attackMs = defaultOf(params::mbId(b, MbControl::Attack));
        band.releaseMs = defaultOf(params::mbId(b, MbControl::Release));
        band.makeup.snapTo(dbToGain(defaultOf(params::mbId(b, MbControl::Makeup))));
    }
}

MultibandStage::~MultibandStage() {
    for (auto& bank : banks_) bank.stop();
}

MbKernelRequest MultibandStage::requestFor(int band) const noexcept {
    return MbKernelRequest{std::exp(splitLog_[0].current()), std::exp(splitLog_[1].current()), std::exp(splitLog_[2].current()), band};
}

void MultibandStage::updateTimeConstants(Band& b) noexcept {
    // L'anticipation fait déjà une rampe de 10 ms : au-delà, lissage en plus.
    const double extra = b.attackMs - kLookaheadMs;
    b.attackCoeff = extra > 0.0 ? coefficient(extra, sampleRate_) : 1.0;
    b.releaseCoeff = coefficient(b.releaseMs, sampleRate_);
}

void MultibandStage::prepare(const ProcessSpec& spec) {
    for (auto& bank : banks_) bank.stop();
    sampleRate_ = spec.sampleRate;
    const ParamId splits[3] = {ParamId::MbSplitLow, ParamId::MbSplitMid, ParamId::MbSplitHigh};
    for (int s = 0; s < 3; ++s) {
        splitLog_[s].prepare(sampleRate_, params::findParameter(splits[s])->smoothingMs);
        splitLog_[s].snapTo(splitLog_[s].target());
    }
    const int n = kernelLengthFor(sampleRate_);
    const int blk = n / 32;
    for (int k = 0; k < kFiltered; ++k) {
        convolvers_[static_cast<std::size_t>(k)].prepare(blk, n);
        banks_[static_cast<std::size_t>(k)].prepare(sampleRate_, n, blk, realtime_, requestFor(k));
        active_[static_cast<std::size_t>(k)] = banks_[static_cast<std::size_t>(k)].initialSlot();
        fading_[static_cast<std::size_t>(k)] = nullptr;
    }
    fadeRamp_.resize(static_cast<std::size_t>(blk));
    for (int i = 0; i < blk; ++i)
        fadeRamp_[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(std::numbers::pi * (i + 0.5) / blk);
    splitLatency_ = n / 2 + blk;
    lookahead_ = static_cast<int>(std::lround(kLookaheadMs * 0.001 * sampleRate_));
    // Audio retardé de l'anticipation + la fenêtre la plus longue : le gain d'un
    // échantillon s est réglé sur la fenêtre [s, s + W] (vers l'avant). Au début
    // d'un coup, elle est déjà entièrement dans la note (une fenêtre centrée y
    // était à moitié dans le silence : sous-estimation, le coup passait).
    const auto windowOf = [&](int b) { return static_cast<int>(std::lround(kRmsWindowMs[b] * 0.001 * sampleRate_)); };
    int longest = 0;
    for (int b = 0; b < params::kMbBands; ++b) longest = std::max(longest, windowOf(b));
    audioDelay_ = lookahead_ + longest;
    latency_ = splitLatency_ + audioDelay_;
    for (auto& d : inputDelay_) d.assign(static_cast<std::size_t>(splitLatency_), 0.0);
    for (int b = 0; b < params::kMbBands; ++b) {
        auto& band = bands_[static_cast<std::size_t>(b)];
        band.makeup.prepare(sampleRate_, params::findParameter(params::mbId(b, MbControl::Makeup))->smoothingMs);
        band.makeup.snapTo(band.makeup.target());
        updateTimeConstants(band);
        band.powerRing.assign(static_cast<std::size_t>(windowOf(b)), 0.0);
        band.powerDelay.assign(static_cast<std::size_t>(longest - windowOf(b)), 0.0);
        band.lookahead.prepare(lookahead_);
        for (auto& d : band.delay) d.assign(static_cast<std::size_t>(audioDelay_), 0.0);
    }
    reset();
}

void MultibandStage::reset() noexcept {
    for (int k = 0; k < kFiltered; ++k) {
        convolvers_[static_cast<std::size_t>(k)].reset();
        if (fading_[static_cast<std::size_t>(k)]) {
            Bank::release(fading_[static_cast<std::size_t>(k)]);
            fading_[static_cast<std::size_t>(k)] = nullptr;
        }
    }
    framePos_ = inputDelayPos_ = lookPos_ = 0;
    for (auto& d : inputDelay_) std::fill(d.begin(), d.end(), 0.0);
    for (auto& b : bands_) {
        std::fill(b.powerRing.begin(), b.powerRing.end(), 0.0);
        std::fill(b.powerDelay.begin(), b.powerDelay.end(), 0.0);
        b.ringPos = b.delayPos = 0;
        b.powerSum = 0.0;
        b.lookahead.reset();
        b.gain = 1.0;
        for (auto& d : b.delay) std::fill(d.begin(), d.end(), 0.0);
    }
}

void MultibandStage::setParameter(ParamId id, double plain, bool immediate) noexcept {
    const auto raw = static_cast<std::uint32_t>(id);
    if (id == ParamId::MbSplitLow || id == ParamId::MbSplitMid || id == ParamId::MbSplitHigh) {
        auto& s = splitLog_[raw - static_cast<std::uint32_t>(ParamId::MbSplitLow)];
        if (immediate) s.snapTo(std::log(plain));
        else s.setTarget(std::log(plain));
        return;
    }
    const auto first = static_cast<std::uint32_t>(ParamId::MbBandFirst);
    if (raw < first || raw >= first + 10u * params::kMbBands) return;
    const auto b = static_cast<std::size_t>((raw - first) / 10u);
    const auto control = static_cast<MbControl>((raw - first) % 10u);
    auto& band = bands_[b];
    switch (control) {
        case MbControl::Threshold:
            band.thresholdDb = plain;
            band.updateKneeStart();
            break;
        case MbControl::Ratio: band.ratio = std::max(1.0, plain); break;
        case MbControl::Attack: band.attackMs = plain; updateTimeConstants(band); break;
        case MbControl::Release: band.releaseMs = plain; updateTimeConstants(band); break;
        case MbControl::Makeup:
            if (immediate) band.makeup.snapTo(dbToGain(plain));
            else band.makeup.setTarget(dbToGain(plain));
            break;
        default: break;
    }
}

double MultibandStage::computeGainDb(const Band& b, double levelDb) const noexcept {
    if (b.ratio <= 1.0) return 0.0;
    const double over = levelDb - b.thresholdDb;
    const double slope = 1.0 / b.ratio - 1.0; // < 0
    if (over <= -0.5 * kKneeDb) return 0.0;
    if (over >= 0.5 * kKneeDb) return slope * over;
    const double x = over + 0.5 * kKneeDb;
    return slope * x * x / (2.0 * kKneeDb);
}

void MultibandStage::frameBoundary() noexcept {
    for (int k = 0; k < kFiltered; ++k) {
        const auto i = static_cast<std::size_t>(k);
        if (fading_[i]) {
            Bank::release(fading_[i]);
            fading_[i] = nullptr;
        }
        banks_[i].request(requestFor(k));
        if (Bank::Slot* ready = banks_[i].takeReady()) {
            Bank::markRetiring(active_[i]);
            fading_[i] = active_[i];
            active_[i] = ready;
        }
        convolvers_[i].processFrame(active_[i]->kernel, fading_[i] ? &fading_[i]->kernel : nullptr);
    }
    framePos_ = 0;
}

void MultibandStage::process(double* const* channels, int numSamples) noexcept {
    double* left = channels[0];
    double* right = channels[1];
    for (int i = 0; i < numSamples; ++i) {
        for (auto& s : splitLog_) s.next();
        const double l = left[i], r = right[i];

        // ---- Séparation : trois bandes filtrées + complément du retard.
        double bandL[params::kMbBands], bandR[params::kMbBands];
        double sumL = 0.0, sumR = 0.0;
        for (int k = 0; k < kFiltered; ++k) {
            auto& c = convolvers_[static_cast<std::size_t>(k)];
            c.write(framePos_, l, r);
            double a = c.outputA(framePos_), b = c.outputB(framePos_);
            if (fading_[static_cast<std::size_t>(k)]) {
                const double t = fadeRamp_[static_cast<std::size_t>(framePos_)];
                a = c.secondaryA(framePos_) + t * (a - c.secondaryA(framePos_));
                b = c.secondaryB(framePos_) + t * (b - c.secondaryB(framePos_));
            }
            bandL[k] = a;
            bandR[k] = b;
            sumL += a;
            sumR += b;
        }
        const auto dp = static_cast<std::size_t>(inputDelayPos_);
        bandL[3] = inputDelay_[0][dp] - sumL;
        bandR[3] = inputDelay_[1][dp] - sumR;
        inputDelay_[0][dp] = l;
        inputDelay_[1][dp] = r;
        if (++inputDelayPos_ == splitLatency_) inputDelayPos_ = 0;

        // ---- Dynamique par bande : le détecteur voit l'audio en avance.
        double outL = 0.0, outR = 0.0;
        const auto lp = static_cast<std::size_t>(lookPos_);
        for (int b = 0; b < params::kMbBands; ++b) {
            auto& band = bands_[static_cast<std::size_t>(b)];
            double power = 0.5 * (bandL[b] * bandL[b] + bandR[b] * bandR[b]);
            if (!band.powerDelay.empty()) {
                std::swap(power, band.powerDelay[static_cast<std::size_t>(band.delayPos)]);
                if (++band.delayPos == static_cast<int>(band.powerDelay.size())) band.delayPos = 0;
            }
            const auto rp = static_cast<std::size_t>(band.ringPos);
            band.powerSum += power - band.powerRing[rp];
            band.powerRing[rp] = power;
            if (++band.ringPos == static_cast<int>(band.powerRing.size())) {
                band.ringPos = 0;
                double exact = 0.0; // recalcul exact à chaque tour (pas de dérive)
                for (double v : band.powerRing) exact += v;
                band.powerSum = exact;
            }
            // Sous le début du genou (ou ratio 1:1) le gain est 0 dB : ni log ni pow.
            const double meanPower = std::max(0.0, band.powerSum) / static_cast<double>(band.powerRing.size());
            const double required = band.ratio <= 1.0 || meanPower < band.kneeStartPower
                                        ? 1.0
                                        : dbToGain(computeGainDb(band, powerToDb(meanPower, -200.0)));
            const double ramped = band.lookahead.process(required, band.releaseCoeff);
            band.gain = ramped < band.gain ? band.gain + band.attackCoeff * (ramped - band.gain) : ramped;
            const double g = band.gain * band.makeup.next();
            const double dl = band.delay[0][lp], dr = band.delay[1][lp];
            band.delay[0][lp] = bandL[b];
            band.delay[1][lp] = bandR[b];
            outL += g * dl;
            outR += g * dr;
        }
        if (++lookPos_ == audioDelay_) lookPos_ = 0;
        left[i] = outL;
        right[i] = outR;

        if (++framePos_ == convolvers_[0].blockSize()) frameBoundary();
    }
}

} // namespace omnis
