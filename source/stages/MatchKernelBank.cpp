// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/MatchKernelBank.cpp
// =============================================================================
#include "MatchKernelBank.h"

#include <algorithm>
#include <cmath>

namespace omnis {

double matchCurveDb(const MatchCurve& nodes, double frequency) noexcept {
    constexpr int last = params::kMatchBands - 1;
    // Position en unités de nœuds : x = 2·log2(f / 20 Hz).
    const double x = frequency > 0.0 ? 2.0 * std::log2(frequency / 20.0) : 0.0;
    if (x <= 0.0) return nodes[0];
    if (x >= last) return nodes[last];
    const int i = static_cast<int>(x);
    const double t = x - i;
    const auto at = [&](int k) { return nodes[static_cast<std::size_t>(std::clamp(k, 0, last))]; };
    const double p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
    // Catmull-Rom uniforme.
    return p1 + 0.5 * t * (p2 - p0 + t * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + t * (3.0 * (p1 - p2) + p3 - p0)));
}

double MatchKernelRequest::tiltAt(double f) const noexcept {
    constexpr int last = params::kMatchBands - 1;
    const double x = f > 0.0 ? std::clamp(2.0 * std::log2(f / 20.0), 0.0, static_cast<double>(last)) : 0.0;
    return tiltDb * (x - 0.5 * last) / (0.5 * last);
}

bool MatchKernelRequest::nearlyEquals(const MatchKernelRequest& o) const noexcept {
    // Comparaison des gains effectifs (dosés) : 0.005 dB est inaudible.
    if (std::fabs(tiltDb - o.tiltDb) > 0.005) return false;
    for (std::size_t k = 0; k < midDb.size(); ++k) {
        if (std::fabs(amount * midDb[k] - o.amount * o.midDb[k]) > 0.005) return false;
        if (std::fabs(amount * sideDb[k] - o.amount * o.sideDb[k]) > 0.005) return false;
    }
    return true;
}

double MatchKernelRequest::midMagnitude(double f) const noexcept {
    return std::pow(10.0, (amount * matchCurveDb(midDb, f) + tiltAt(f)) / 20.0);
}

double MatchKernelRequest::sideMagnitude(double f) const noexcept {
    return std::pow(10.0, (amount * matchCurveDb(sideDb, f) + tiltAt(f)) / 20.0);
}

} // namespace omnis
