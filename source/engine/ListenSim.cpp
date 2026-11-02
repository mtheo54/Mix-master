// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/ListenSim.cpp
// =============================================================================
#include "ListenSim.h"

#include "../params/Parameters.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace omnis {

namespace {

struct Band {
    ListenSim::Filter type;
    double freq, gainDb, q;
};
struct Recipe {
    std::vector<Band> bands;
    bool mono = false, sideLowCut = false;
    int spatial = 0;
    double targetLufs = 0.0;
};

using F = ListenSim::Filter;

/// Approximations d'OMNIS (pas des mesures) : l'écart de chaque écoute à une
/// écoute neutre, en quelques filtres larges.
Recipe recipeOf(int preset) {
    switch (preset) {
        case ListenSim::AirPods: // ouverts dans l'oreille : le sub s'échappe
            return {{{F::HighPass, 35, 0, 0.7}, {F::LowShelf, 90, -6, 0.7}, {F::Peak, 3000, 2, 1.0}, {F::HighShelf, 10000, -3, 0.7}}};
        case ListenSim::AirPodsPro:
            return {{{F::LowShelf, 105, 3, 0.7}, {F::Peak, 4500, -2, 1.4}, {F::HighShelf, 12000, -2, 0.7}}};
        case ListenSim::AirPodsMax:
            return {{{F::LowShelf, 105, 2, 0.7}, {F::Peak, 9000, -2.5, 1.2}}};
        case ListenSim::BeatsStudioPro:
            return {{{F::LowShelf, 110, 5, 0.7}, {F::Peak, 250, 1.5, 1.0}, {F::Peak, 6000, 1.5, 2.0}}};
        case ListenSim::SonyXm5:
            return {{{F::LowShelf, 120, 4, 0.7}, {F::Peak, 1500, -1.5, 1.0}, {F::Peak, 8000, -3, 1.5}}};
        case ListenSim::GalaxyBuds:
            return {{{F::LowShelf, 100, 3, 0.7}, {F::Peak, 8000, 2, 1.5}}};
        case ListenSim::JblTune:
            return {{{F::LowShelf, 110, 6, 0.7}, {F::Peak, 1000, -2.5, 0.8}, {F::Peak, 6000, 3.5, 2.0}, {F::LowPass, 14000, 0, 0.7}}};
        case ListenSim::Hd600:
            return {{{F::LowShelf, 50, -4, 0.7}, {F::Peak, 3000, 1, 1.0}}};
        case ListenSim::AtM50x:
            return {{{F::LowShelf, 90, 3, 0.7}, {F::Peak, 2000, -1.5, 1.2}, {F::Peak, 6000, 3, 2.0}}};
        case ListenSim::Phone: { // petit haut-parleur : rien sous ~350 Hz, bosse vers 2,5 kHz
            Recipe r{{{F::HighPass, 350, 0, 0.54}, {F::HighPass, 350, 0, 1.31}, {F::Peak, 2500, 4, 1.0}, {F::LowPass, 12000, 0, 0.7}}};
            r.mono = true;
            return r;
        }
        case ListenSim::TikTok: {
            Recipe r = recipeOf(ListenSim::Phone);
            r.mono = false; // la légère spatialisation garde un peu d'image
            r.spatial = 1;
            r.targetLufs = -14.0;
            return r;
        }
        case ListenSim::Car: // la cabine gonfle le grave
            return {{{F::Peak, 60, 5, 1.0}, {F::Peak, 250, -3, 1.0}, {F::HighShelf, 10000, -2, 0.7}}};
        case ListenSim::Club: {
            Recipe r{{{F::HighPass, 30, 0, 0.7}, {F::LowShelf, 70, 5, 0.7}, {F::Peak, 3000, 1, 1.0}, {F::LowPass, 16000, 0, 0.7}}};
            r.sideLowCut = true; // sono : le grave est mono
            return r;
        }
        case ListenSim::Spatial: {
            Recipe r;
            r.spatial = 2;
            return r;
        }
        case ListenSim::Spotify: return {{}, false, false, 0, -14.0};
        case ListenSim::Deezer: return {{}, false, false, 0, -15.0};
        case ListenSim::YouTube: return {{}, false, false, 0, -14.0};
        case ListenSim::AppleMusic: return {{}, false, false, 0, -16.0};
        default: return {};
    }
}

} // namespace

std::span<const std::string_view> ListenSim::labels() noexcept {
    return params::findParameter(params::ParamId::ListenSim)->choices;
}

ListenSim::Biquad ListenSim::design(Filter type, double fs, double f0, double gainDb, double q) noexcept {
    // Formules classiques des filtres biquad (« Audio EQ Cookbook »).
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * std::numbers::pi * std::clamp(f0, 1.0, 0.49 * fs) / fs;
    const double cw = std::cos(w0), sw = std::sin(w0), alpha = sw / (2.0 * q);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type) {
        case Filter::Peak:
            b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
            break;
        case Filter::LowShelf: {
            const double s = 2 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
            a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s;
            break;
        }
        case Filter::HighShelf: {
            const double s = 2 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
            a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s;
            break;
        }
        case Filter::HighPass:
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case Filter::LowPass:
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
    }
    Biquad b;
    b.b0 = b0 / a0; b.b1 = b1 / a0; b.b2 = b2 / a0; b.a1 = a1 / a0; b.a2 = a2 / a0;
    return b;
}

double ListenSim::responseDb(int preset, double fs, double freq) noexcept {
    const auto r = recipeOf(preset);
    const std::complex<double> z = std::polar(1.0, -2.0 * std::numbers::pi * freq / fs);
    double db = 0.0;
    for (const auto& band : r.bands) {
        const auto b = design(band.type, fs, band.freq, band.gainDb, band.q);
        const auto h = (b.b0 + b.b1 * z + b.b2 * z * z) / (1.0 + b.a1 * z + b.a2 * z * z);
        db += 20.0 * std::log10(std::max(1e-12, std::abs(h)));
    }
    return db;
}

void ListenSim::prepare(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    const auto len = static_cast<std::size_t>(std::ceil(0.032 * sampleRate)) + 2;
    for (auto& r : ring_) r.assign(len, 0.0);
    itd_ = std::max(1, static_cast<int>(std::lround(0.00026 * sampleRate))); // ~0,26 ms : enceinte à 30°
    const double ms[4] = {9.0, 13.0, 19.0, 27.0};                           // premières réflexions
    for (int k = 0; k < 4; ++k) taps_[static_cast<std::size_t>(k)] = static_cast<int>(std::lround(ms[k] * 0.001 * sampleRate));
    shadowCoeff_ = 1.0 - std::exp(-2.0 * std::numbers::pi * 1500.0 / sampleRate); // l'ombre de la tête coupe l'aigu
    fadeStep_ = 1.0 / std::max(1.0, 0.02 * sampleRate);
    active_ = wanted_;
    build(active_);
    reset();
}

void ListenSim::reset() noexcept {
    for (auto& f : filters_) f.z1 = f.z2 = {};
    sideHp_.z1 = sideHp_.z2 = {};
    for (auto& r : ring_) std::fill(r.begin(), r.end(), 0.0);
    ringPos_ = 0;
    shadow_ = {};
    fade_ = 1.0;
    switching_ = false;
}

void ListenSim::setPreset(int preset) noexcept {
    wanted_ = std::clamp(preset, 0, static_cast<int>(Count) - 1);
    if (wanted_ != active_) switching_ = true; // fondu vers le silence, puis la nouvelle écoute
}

void ListenSim::build(int preset) noexcept {
    const auto r = recipeOf(preset);
    filters_.clear();
    for (const auto& b : r.bands) filters_.push_back(design(b.type, sampleRate_, b.freq, b.gainDb, b.q));
    mono_ = r.mono;
    sideLowCut_ = r.sideLowCut;
    sideHp_ = design(Filter::HighPass, sampleRate_, 120.0, 0.0, 0.707);
    spatial_ = r.spatial;
    targetLufs_ = r.targetLufs;
}

void ListenSim::process(double* const* ch, int n, double outputLufs) noexcept {
    if (active_ == Off && !switching_ && fade_ >= 1.0) {
        normDb_ = 0.0;
        return;
    }
    // Normalisation des plateformes : elles baissent un master plus fort que leur
    // cible (on ne remonte pas un master plus faible : chaque plateforme fait
    // différemment). Gain lissé, mis à jour une fois par bloc.
    double wantDb = 0.0;
    if (targetLufs_ < 0.0 && std::isfinite(outputLufs) && outputLufs > -70.0) wantDb = std::min(0.0, targetLufs_ - outputLufs);
    normDb_ += 0.05 * (wantDb - normDb_);
    const double norm = std::pow(10.0, normDb_ / 20.0);
    const int len = static_cast<int>(ring_[0].size());
    const auto at = [&](int c, int delay) {
        int p = ringPos_ - delay;
        if (p < 0) p += len;
        return ring_[static_cast<std::size_t>(c)][static_cast<std::size_t>(p)];
    };
    for (int i = 0; i < n; ++i) {
        // Changement d'écoute : 20 ms vers le silence, bascule, 20 ms de retour.
        if (switching_) {
            fade_ -= fadeStep_;
            if (fade_ <= 0.0) {
                fade_ = 0.0;
                active_ = wanted_;
                build(active_);
                reset();
                fade_ = 0.0;
                switching_ = false;
            }
        } else if (fade_ < 1.0) {
            fade_ = std::min(1.0, fade_ + fadeStep_);
        }
        double l = ch[0][i], r = ch[1][i];
        if (active_ == Off) { // écoute neutre (fondu au changement seulement)
            ch[0][i] = l * fade_;
            ch[1][i] = r * fade_;
            continue;
        }
        if (mono_) l = r = 0.5 * (l + r);
        if (sideLowCut_) {
            const double m = 0.5 * (l + r), s = sideHp_.run(0.5 * (l - r), 0);
            l = m + s;
            r = m - s;
        }
        for (auto& f : filters_) {
            l = f.run(l, 0);
            r = f.run(r, 1);
        }
        ring_[0][static_cast<std::size_t>(ringPos_)] = l;
        ring_[1][static_cast<std::size_t>(ringPos_)] = r;
        if (spatial_ == 1) {
            // Légère spatialisation (TikTok) : image un peu élargie, pièce discrète.
            const double m = 0.5 * (l + r), s = 0.5 * (l - r) * 1.25;
            const double room = 0.063 * (at(1, taps_[0]) + at(0, taps_[1]));
            l = m + s + room;
            r = m - s + room;
        } else if (spatial_ == 2) {
            // Binaural : chaque oreille entend l'enceinte opposée en retard et
            // assombrie par la tête, plus les premières réflexions de la pièce.
            shadow_[0] += shadowCoeff_ * (at(1, itd_) - shadow_[0]);
            shadow_[1] += shadowCoeff_ * (at(0, itd_) - shadow_[1]);
            const double reflL = 0.16 * at(0, taps_[0]) + 0.12 * at(1, taps_[1]) + 0.09 * at(0, taps_[2]) + 0.06 * at(1, taps_[3]);
            const double reflR = 0.16 * at(1, taps_[0]) + 0.12 * at(0, taps_[1]) + 0.09 * at(1, taps_[2]) + 0.06 * at(0, taps_[3]);
            l = (l + 0.6 * shadow_[0] + reflL) / 1.45;
            r = (r + 0.6 * shadow_[1] + reflR) / 1.45;
        }
        if (++ringPos_ == len) ringPos_ = 0;
        ch[0][i] = l * norm * fade_;
        ch[1][i] = r * norm * fade_;
    }
}

} // namespace omnis
