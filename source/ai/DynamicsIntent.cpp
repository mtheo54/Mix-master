// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/DynamicsIntent.cpp
// =============================================================================
#include "DynamicsIntent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace omnis::ai {

namespace {

constexpr int kBands = params::kMbBands;
constexpr double kMinGapDb = 2.0;
constexpr double kMaxShare = 0.6;
constexpr double kMaxRatio = 4.0;
constexpr double kProtectedRatio = 2.0;
constexpr double kAttackMs[kBands] = {10.0, 20.0, 25.0, 20.0};
constexpr double kReleaseMs[kBands] = {200.0, 150.0, 120.0, 80.0};
const char* kNames[kBands] = {"Sub", "Bas", "Médium", "Aigu"};

/// Variation moyenne des nœuds d'une bande.
double bandSpread(const SpectralSignature& s, double lo, double hi) {
    double sum = 0.0;
    int n = 0;
    for (int k = 0; k < params::kMatchBands; ++k) {
        const double f = params::matchBandHz(k);
        if (f >= lo && f < hi) {
            sum += s.midSpreadDb[static_cast<std::size_t>(k)];
            ++n;
        }
    }
    return n ? sum / n : 0.0;
}

/// La bande du multibande touche-t-elle une région protégée du Match EQ ?
bool bandProtected(const MatchRequest& r, double lo, double hi) {
    for (int k = 0; k < params::kMatchBands; ++k) {
        const double f = params::matchBandHz(k);
        if (f >= lo && f < hi && r.protectedRegions[static_cast<std::size_t>(regionOfBand(k))]) return true;
    }
    return false;
}

std::string format(const char* fmt, double a = 0, double b = 0, double c = 0, double d = 0, double e = 0) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), fmt, a, b, c, d, e);
    return buf;
}

} // namespace

const char* mbBandName(int band) noexcept { return kNames[std::clamp(band, 0, kBands - 1)]; }

DynamicsIntent planDynamics(const SpectralSignature& prod, const SpectralSignature& ref, const analysis::BandLevels& levels,
                            const MatchRequest& request, const Descriptor& wishes) {
    DynamicsIntent intent;
    const double edges[kBands + 1] = {0.0, intent.splitsHz[0], intent.splitsHz[1], intent.splitsHz[2], 1.0e6};
    const double punch = wishes[Axis::Punch];
    const double density = wishes[Axis::Density];

    for (int b = 0; b < kBands; ++b) {
        auto& band = intent.bands[static_cast<std::size_t>(b)];
        const auto i = static_cast<std::size_t>(b);
        band.attackMs = kAttackMs[b] + (b > 0 ? std::max(0.0, 15.0 * punch) : 0.0);
        band.releaseMs = kReleaseMs[b];

        const double prodSpread = bandSpread(prod, edges[b], edges[b + 1]);
        const double refSpread = bandSpread(ref, edges[b], edges[b + 1]);
        const double gap = prodSpread - refSpread;
        // « dense » / « dynamique » dans la demande décalent la cible.
        double share = std::clamp(request.resemblance * gap / std::max(prodSpread, 1.0) + 0.15 * density, 0.0, kMaxShare);
        if (gap < kMinGapDb && density <= 0.0) share = 0.0;
        if (share <= 0.0) continue;

        // Seuil à la médiane ; la moitié haute de la variation réduite de la part voulue.
        const double upper = std::max(1.0, levels.p90[i] - levels.p50[i]);
        const double wanted = share * (levels.p90[i] - levels.p10[i]);
        const bool isProtected = bandProtected(request, edges[b], edges[b + 1]);
        const double maxRatio = isProtected ? kProtectedRatio : kMaxRatio;
        band.thresholdDb = std::clamp(levels.p50[i], -60.0, 0.0);
        band.ratio = std::clamp(upper / std::max(upper - wanted, upper / maxRatio), 1.0, maxRatio);
        intent.explanation.push_back(std::string(kNames[b]) +
                                     format(" : varie de %.0f dB (référence %.0f dB) -> seuil %.1f dBFS, ratio %.1f:1, attaque %.0f ms", prodSpread,
                                            refSpread, band.thresholdDb, band.ratio, band.attackMs) +
                                     (isProtected ? " (protégé : ratio limité)" : ""));
    }
    if (intent.explanation.empty()) intent.explanation.push_back("Tenue déjà proche de la référence : multibande neutre");
    return intent;
}

std::vector<std::pair<std::string, double>> dynamicsSettings(const DynamicsIntent& intent) {
    std::vector<std::pair<std::string, double>> s{
        {"mb_split_low", intent.splitsHz[0]}, {"mb_split_mid", intent.splitsHz[1]}, {"mb_split_high", intent.splitsHz[2]}};
    for (int b = 0; b < kBands; ++b) {
        const auto& band = intent.bands[static_cast<std::size_t>(b)];
        const auto key = [&](params::MbControl c) { return std::string(params::findParameter(params::mbId(b, c))->key); };
        s.emplace_back(key(params::MbControl::Threshold), band.thresholdDb);
        s.emplace_back(key(params::MbControl::Ratio), band.ratio);
        s.emplace_back(key(params::MbControl::Attack), band.attackMs);
        s.emplace_back(key(params::MbControl::Release), band.releaseMs);
        s.emplace_back(key(params::MbControl::Makeup), band.makeupDb);
    }
    return s;
}

} // namespace omnis::ai
