// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/DrumBus.cpp
// =============================================================================
#include "DrumBus.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace omnis::stems {

namespace {

constexpr double kOnsetRiseDb = 6.0, kFloorBelowPeakDb = 30.0, kRefractoryMs = 60.0, kOpenHatMs = 150.0;

double toDb(double p) { return 10.0 * std::log10(std::max(p, 1e-20)); }

/// Filtres RBJ : passe-bas, passe-haut, passe-bande (gain 0 dB au centre).
template <typename B>
void design(B& f, int kind, double hz, double q, double fs) {
    const double w = 2.0 * std::numbers::pi * hz / fs, c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    double b0, b1, b2;
    if (kind == 0) b0 = (1 - c) / 2, b1 = 1 - c, b2 = (1 - c) / 2;
    else if (kind == 1) b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2;
    else b0 = alpha, b1 = 0.0, b2 = -alpha;
    const double a0 = 1 + alpha;
    f.b0 = b0 / a0;
    f.b1 = b1 / a0;
    f.b2 = b2 / a0;
    f.a1 = -2 * c / a0;
    f.a2 = (1 - alpha) / a0;
}

} // namespace

std::vector<std::string> describeDrumBus(const DrumContent& c) {
    std::vector<std::string> out;
    const auto hits = [](const DrumContent::Item& i) { return " (" + std::to_string(i.hits) + " coups)"; };
    if (c.kick.present) out.push_back("Kick : mono sous 150 Hz" + hits(c.kick));
    if (c.snare.present) out.push_back("Snare / clap : +8 % de largeur" + hits(c.snare));
    if (c.openHat.present) out.push_back("Open hat : dureté vers 6,5 kHz calmée" + hits(c.openHat));
    if (c.hat.present) out.push_back("Hi-hat : dureté vers 7 kHz calmée, largeur gardée" + hits(c.hat));
    return out;
}

void DrumBusAnalyzer::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    frameSize_ = std::max(64, static_cast<int>(std::lround(0.00533 * sampleRate)));
    design(filters_[kLow], 0, 120.0, 0.7071, sampleRate);
    design(filters_[kBody], 2, 250.0, 1.0, sampleRate);
    design(filters_[kCrack], 2, 2500.0, 0.8, sampleRate);
    design(filters_[kHats], 1, 7000.0, 0.7071, sampleRate);
    reset();
}

void DrumBusAnalyzer::reset() {
    for (auto& f : filters_) f.z1 = f.z2 = 0.0;
    acc_.fill(0.0);
    peakDb_.fill(-200.0);
    holdUntil_.fill(0.0);
    for (auto& h : history_) h.fill(-200.0);
    historyPos_ = 0;
    framePos_ = 0;
    frame_ = 0;
    hatPending_ = false;
    eventFrames_ = -1;
    content_ = {};
}

void DrumBusAnalyzer::push(const double* left, const double* right, int numFrames) {
    for (int i = 0; i < numFrames; ++i) {
        const double x = 0.5 * (left[i] + right[i]);
        for (int b = 0; b < kBands; ++b) {
            const double y = filters_[static_cast<std::size_t>(b)].run(x);
            acc_[static_cast<std::size_t>(b)] += y * y;
        }
        if (++framePos_ == frameSize_) endFrame();
    }
    content_.seconds += numFrames / sampleRate_;
}

void DrumBusAnalyzer::endFrame() {
    framePos_ = 0;
    std::array<double, kBands> db{}, before{};
    std::array<bool, kBands> onset{};
    const double frameMs = 1000.0 * frameSize_ / sampleRate_;
    for (int b = 0; b < kBands; ++b) {
        const auto k = static_cast<std::size_t>(b);
        db[k] = toDb(acc_[k] / frameSize_);
        acc_[k] = 0.0;
        double mean = 0.0;
        for (double h : history_[k]) mean += std::pow(10.0, h / 10.0);
        before[k] = toDb(mean / history_[k].size());
        // Le plus fort entendu retombe lentement (60 dB en ~10 s).
        peakDb_[k] = std::max(db[k], peakDb_[k] - 6.0 * frameMs / 1000.0);
        onset[k] = db[k] - before[k] > kOnsetRiseDb && db[k] > peakDb_[k] - kFloorBelowPeakDb && frame_ >= static_cast<long>(holdUntil_[k]) && db[k] > -90.0;
        if (onset[k]) holdUntil_[k] = static_cast<double>(frame_) + kRefractoryMs / frameMs;
        history_[k][static_cast<std::size_t>(historyPos_)] = db[k];
    }
    historyPos_ = (historyPos_ + 1) % static_cast<int>(history_[0].size());

    // Retombée du dernier hat.
    if (hatPending_) {
        const double elapsedMs = (frame_ - hatOnsetFrame_) * frameMs;
        if (db[kHats] < hatOnsetDb_ - 15.0) {
            ++(elapsedMs > kOpenHatMs ? content_.openHat : content_.hat).hits;
            hatPending_ = false;
        } else if (elapsedMs > 400.0) {
            ++(elapsedMs > kOpenHatMs ? content_.openHat : content_.hat).hits;
            hatPending_ = false;
        }
    }
    // Un coup se juge sur ~15 ms (trois trames) : à son tout premier instant, le
    // grave d'un kick n'est pas encore monté et son claquement ressemble à une snare.
    const double loudest = *std::max_element(db.begin(), db.end());
    bool any = false;
    for (int b = 0; b < kBands; ++b) any = any || (onset[static_cast<std::size_t>(b)] && db[static_cast<std::size_t>(b)] > loudest - 30.0);
    if (eventFrames_ < 0 && any) {
        eventFrames_ = 0;
        eventMax_ = db;
        eventBase_ = before;
        eventFrame_ = frame_;
    } else if (eventFrames_ >= 0) {
        for (int b = 0; b < kBands; ++b) eventMax_[static_cast<std::size_t>(b)] = std::max(eventMax_[static_cast<std::size_t>(b)], db[static_cast<std::size_t>(b)]);
        if (++eventFrames_ >= 2) {
            // Ce qui est MONTÉ dans chaque zone (la queue d'un kick qui sonne encore ne compte pas).
            std::array<double, kBands> m{};
            for (int b = 0; b < kBands; ++b) {
                const auto k = static_cast<std::size_t>(b);
                m[k] = toDb(std::max(0.0, std::pow(10.0, eventMax_[k] / 10.0) - std::pow(10.0, eventBase_[k] / 10.0)));
            }
            const double top = *std::max_element(m.begin(), m.end());
            // Un kick s'entend : son grave monte à moins de 20 dB de la zone qui monte le
            // plus, et à moins de 24 dB du plus fort que le bus ait joué (toutes zones).
            const double busPeak = *std::max_element(peakDb_.begin(), peakDb_.end());
            if (m[kLow] > m[kCrack] + 6.0 && m[kLow] > m[kHats] && m[kLow] > top - 20.0 && m[kLow] > busPeak - 24.0) {
                ++content_.kick.hits;
            } else if (m[kBody] > m[kHats] - 12.0 && m[kLow] < m[kCrack] + 6.0) {
                ++content_.snare.hits; // du corps et du claquement : snare ou clap
            } else if (m[kBody] < m[kHats] - 12.0) {
                if (hatPending_) ++(((frame_ - hatOnsetFrame_) * frameMs > kOpenHatMs) ? content_.openHat : content_.hat).hits;
                hatPending_ = true;
                hatOnsetDb_ = m[kHats];
                hatOnsetFrame_ = eventFrame_;
            }
            eventFrames_ = -1;
        }
    }
    for (auto* item : {&content_.kick, &content_.snare, &content_.hat, &content_.openHat}) item->present = item->hits >= kMinHits;
    ++frame_;
}

} // namespace omnis::stems
