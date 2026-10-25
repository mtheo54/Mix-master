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

int ClipperStage::splitKernelLengthFor(double sampleRate) noexcept {
    // 8192 taps à 44.1/48 kHz : Linkwitz-Riley 24 dB/oct à 120 Hz reproduit à
    // ±0.7 dB jusqu'à -60 dB (prototype du module 2) ; proportionnel au-delà.
    if (sampleRate <= 50000.0) return 8192;
    if (sampleRate <= 100000.0) return 16384;
    if (sampleRate <= 200000.0) return 32768;
    return 65536;
}

ClipperStage::ClipperStage() {
    drive_.snapTo(dbToGain(defaultOf(ParamId::ClipDrive)));
    subDrive_.snapTo(dbToGain(defaultOf(ParamId::ClipSubDrive)));
    ceiling_.snapTo(dbToGain(defaultOf(ParamId::ClipCeiling)));
    knee_.snapTo(defaultOf(ParamId::ClipKnee) * 0.01);
    splitLog_.snapTo(std::log(defaultOf(ParamId::ClipSplit)));
    roomAmount_.snapTo(defaultOf(ParamId::ClipSubRoom) * 0.01);
    splitLog_.setSnapThreshold(1e-6);
    const int mode = static_cast<int>(defaultOf(ParamId::ClipMode));
    for (int m = 0; m < 3; ++m) modeWeight_[static_cast<std::size_t>(m)].snapTo(m == mode ? 1.0 : 0.0);
    requestedStages_ = static_cast<int>(defaultOf(ParamId::ClipOversampling));
}

ClipperStage::~ClipperStage() { bank_.stop(); }

void ClipperStage::prepare(const ProcessSpec& spec) {
    bank_.stop();
    sampleRate_ = spec.sampleRate;
    drive_.prepare(sampleRate_, smoothingOf(ParamId::ClipDrive));
    subDrive_.prepare(sampleRate_, smoothingOf(ParamId::ClipSubDrive));
    ceiling_.prepare(sampleRate_, smoothingOf(ParamId::ClipCeiling));
    knee_.prepare(sampleRate_, smoothingOf(ParamId::ClipKnee));
    splitLog_.prepare(sampleRate_, smoothingOf(ParamId::ClipSplit));
    roomAmount_.prepare(sampleRate_, smoothingOf(ParamId::ClipSubRoom));
    roomAmount_.snapTo(roomAmount_.target());
    for (auto& w : modeWeight_) w.prepare(sampleRate_, smoothingOf(ParamId::ClipMode));
    for (OnePoleSmoother* s : {&drive_, &subDrive_, &ceiling_, &knee_, &splitLog_, &modeWeight_[0], &modeWeight_[1], &modeWeight_[2]})
        s->snapTo(s->target());

    // Séparation de bandes à phase linéaire.
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

    // Suréchantillonnage des deux bandes ; latence = pire cas 16x.
    for (Band* band : {&low_, &high_}) {
        band->oversampler.prepare(spec.maxBlockSize);
        band->oversampler.setStages(requestedStages_);
        for (auto& buf : band->buffer) buf.assign(static_cast<std::size_t>(spec.maxBlockSize), 0.0);
    }
    const int osLatency =
        static_cast<int>(std::lround(dsp::Oversampler::phaseDelaySamples(dsp::Oversampler::kMaxStages, sampleRate_)));
    for (int s = 0; s <= dsp::Oversampler::kMaxStages; ++s)
        paddingForStages_[static_cast<std::size_t>(s)] =
            std::max(0, osLatency - static_cast<int>(std::lround(dsp::Oversampler::phaseDelaySamples(s, sampleRate_))));
    padding_.prepare(2, osLatency);
    padding_.setDelay(paddingForStages_[static_cast<std::size_t>(requestedStages_)]);
    roomLatency_ = std::max(1, static_cast<int>(std::lround(kRoomLookaheadMs * 1e-3 * sampleRate_)));
    room_.prepare(roomLatency_);
    roomRelease_ = 1.0 - std::exp(-1.0 / (kRoomReleaseMs * 1e-3 * sampleRate_));
    for (auto* d : {&roomDelayLow_, &roomDelayHigh_})
        for (auto& ch : *d) ch.assign(static_cast<std::size_t>(roomLatency_), 0.0);
    latency_ = splitLatency_ + osLatency + roomLatency_;

    const auto m = static_cast<std::size_t>(spec.maxBlockSize);
    for (auto* v : {&gainLow_, &gainHigh_, &gainOut_, &knees_, &wHard_, &wSoft_, &wDiode_}) v->assign(m, 0.0);

    // Bloqueur de DC du 1er ordre à 3 Hz pour la composante Diode, au taux
    // suréchantillonné : y = x − x₁ + R·y₁,  R = e^(−2π·fc / (fs·facteur)).
    dcCoefficient_ = std::exp(-2.0 * std::numbers::pi * 3.0 / (sampleRate_ * low_.oversampler.factor()));
    reset();
}

void ClipperStage::reset() noexcept {
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
        for (auto& c : band->clippers) c.reset();
        band->dcX1.fill(0.0);
        band->dcY1.fill(0.0);
    }
    padding_.reset();
    room_.reset();
    for (auto* d : {&roomDelayLow_, &roomDelayHigh_})
        for (auto& ch : *d) std::fill(ch.begin(), ch.end(), 0.0);
    roomDelayPos_ = 0;
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
        case ParamId::ClipSubDrive: set(subDrive_, dbToGain(plain)); break;
        case ParamId::ClipCeiling: set(ceiling_, dbToGain(plain)); break;
        case ParamId::ClipKnee: set(knee_, plain * 0.01); break;
        case ParamId::ClipSplit: set(splitLog_, std::log(plain)); break;
        case ParamId::ClipSubRoom: set(roomAmount_, plain * 0.01); break;
        case ParamId::ClipMode: {
            const int mode = static_cast<int>(plain);
            for (int m = 0; m < 3; ++m) set(modeWeight_[static_cast<std::size_t>(m)], m == mode ? 1.0 : 0.0);
            break;
        }
        case ParamId::ClipOversampling: requestedStages_ = std::clamp(static_cast<int>(plain), 0, dsp::Oversampler::kMaxStages); break;
        default: break;
    }
}

void ClipperStage::applyOversamplingChoice() noexcept {
    if (requestedStages_ == low_.oversampler.stages()) return;
    // Changement de qualité : courte discontinuité inévitable (réglage de
    // qualité, pas un paramètre d'automation).
    low_.oversampler.setStages(requestedStages_);
    high_.oversampler.setStages(requestedStages_);
    padding_.setDelay(paddingForStages_[static_cast<std::size_t>(requestedStages_)]);
    dcCoefficient_ = std::exp(-2.0 * std::numbers::pi * 3.0 / (sampleRate_ * low_.oversampler.factor()));
}

void ClipperStage::splitFrameBoundary() noexcept {
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

void ClipperStage::clipBand(Band& band, const std::vector<double>& gainIn, int numSamples) noexcept {
    const double* in[2] = {band.buffer[0].data(), band.buffer[1].data()};
    double* const* up = band.oversampler.upsample(in, numSamples);
    const int stages = band.oversampler.stages();
    const int total = numSamples << stages;
    const double r = dcCoefficient_;
    for (int ch = 0; ch < 2; ++ch) {
        double* x = up[ch];
        auto& clip = band.clippers[static_cast<std::size_t>(ch)];
        double x1 = band.dcX1[static_cast<std::size_t>(ch)], y1 = band.dcY1[static_cast<std::size_t>(ch)];
        for (int j = 0; j < total; ++j) {
            const auto b = static_cast<std::size_t>(j >> stages);
            const auto out = clip.process(x[j] * gainIn[b], knees_[b], wHard_[b], wSoft_[b], wDiode_[b]);
            // Seule la composante Diode (asymétrique) passe dans le bloqueur de DC.
            const double d = out.diode - x1 + r * y1;
            x1 = out.diode;
            y1 = d;
            x[j] = gainOut_[b] * (out.symmetric + d);
        }
        band.dcX1[static_cast<std::size_t>(ch)] = x1;
        band.dcY1[static_cast<std::size_t>(ch)] = y1;
    }
    double* out[2] = {band.buffer[0].data(), band.buffer[1].data()};
    band.oversampler.downsample(out, numSamples);
}

void ClipperStage::process(double* const* channels, int numSamples) noexcept {
    applyOversamplingChoice();
    double drivenPeak = 0.0, lastCeiling = 1.0;

    for (int i = 0; i < numSamples; ++i) {
        const auto k = static_cast<std::size_t>(i);
        // Paramètres (un pas de lissage par échantillon).
        const double drive = drive_.next();
        const double ceiling = ceiling_.next();
        gainHigh_[k] = drive / ceiling;
        gainLow_[k] = drive * subDrive_.next() / ceiling;
        gainOut_[k] = ceiling;
        knees_[k] = knee_.next();
        wHard_[k] = modeWeight_[0].next();
        wSoft_[k] = modeWeight_[1].next();
        wDiode_[k] = modeWeight_[2].next();
        splitLog_.next();

        // Séparation : bas = FIR LP (sortie de la trame précédente), haut = entrée
        // retardée de la même latence − bas.
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
        drivenPeak = std::max(drivenPeak, drive * std::max(std::fabs(dl), std::fabs(dr)));
        lastCeiling = ceiling;
        inputDelay_[0][dp] = l;
        inputDelay_[1][dp] = r;
        if (++inputDelayPos_ == splitLatency_) inputDelayPos_ = 0;
        low_.buffer[0][k] = lowL;
        low_.buffer[1][k] = lowR;
        high_.buffer[0][k] = dl - lowL;
        high_.buffer[1][k] = dr - lowR;

        if (++framePos_ == splitter_.blockSize()) splitFrameBoundary();
    }

    clipBand(low_, gainLow_, numSamples);
    clipBand(high_, gainHigh_, numSamples);

    // Partage de marge : seule la bande sub cède la place quand la somme dépasse.
    for (int i = 0; i < numSamples; ++i) {
        const auto k = static_cast<std::size_t>(i);
        const double c = gainOut_[k];
        double required = 1.0;
        for (int ch = 0; ch < 2; ++ch) {
            const double lo = low_.buffer[static_cast<std::size_t>(ch)][k];
            const double hi = high_.buffer[static_cast<std::size_t>(ch)][k];
            if (std::fabs(lo + hi) <= c || std::fabs(lo) < 1e-12) continue;
            // Plus grand g <= 1 tel que |g·lo + hi| <= c (borne haute de l'intervalle admissible).
            const double g = lo > 0.0 ? (c - hi) / lo : (-c - hi) / lo;
            required = std::min(required, std::clamp(g, 0.0, 1.0));
        }
        // Profondeur bornée : à 100 %, la 808 ne cède jamais plus de 6 dB. Sans
        // borne, elle cédait en permanence et le sub perdait 14 dB (mesuré).
        const double amount = roomAmount_.next();
        if (amount != roomFloorAmount_) { // pow une fois par changement, pas par échantillon
            roomFloorAmount_ = amount;
            roomFloor_ = dbToGain(-kRoomMaxDepthDb * amount);
        }
        const double floor = roomFloor_;
        const double gain = room_.process(std::max(required, floor), roomRelease_);
        const auto dp = static_cast<std::size_t>(roomDelayPos_);
        for (int ch = 0; ch < 2; ++ch) {
            const auto c2 = static_cast<std::size_t>(ch);
            const double lo = roomDelayLow_[c2][dp], hi = roomDelayHigh_[c2][dp];
            roomDelayLow_[c2][dp] = low_.buffer[c2][k];
            roomDelayHigh_[c2][dp] = high_.buffer[c2][k];
            channels[ch][i] = gain * lo + hi;
        }
        if (++roomDelayPos_ == roomLatency_) roomDelayPos_ = 0;
    }
    padding_.process(channels, 2, numSamples);
    // Afficheur : ce que l'écrêtage a retiré aux crêtes du bloc (approché).
    double outPeak = 0.0;
    for (int i = 0; i < numSamples; ++i) outPeak = std::max({outPeak, std::fabs(channels[0][i]), std::fabs(channels[1][i])});
    lastReductionDb_ = drivenPeak > lastCeiling && outPeak > 0.0 ? std::min(0.0, gainToDb(outPeak / drivenPeak)) : 0.0;
}

} // namespace omnis
