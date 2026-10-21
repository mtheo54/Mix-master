// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/SubKernelBank.cpp
// =============================================================================
#include "SubKernelBank.h"

#include <cmath>

namespace omnis {

namespace {

double pow4(double x) {
    const double x2 = x * x;
    return x2 * x2;
}

} // namespace

// -----------------------------------------------------------------------------
//  SubKernelRequest
// -----------------------------------------------------------------------------
bool SubKernelRequest::nearlyEquals(const SubKernelRequest& o) const noexcept {
    const auto relClose = [](double a, double b) { return std::fabs(a - b) <= 1e-4 * std::fabs(b); };
    return relClose(lowCutHz, o.lowCutHz) && relClose(monoHz, o.monoHz) && relClose(subHz, o.subHz) &&
           std::fabs(monoAmount - o.monoAmount) < 1e-4 && std::fabs(slopeMix - o.slopeMix) < 1e-4 &&
           std::fabs(subGainDb - o.subGainDb) < 1e-3;
}

double SubKernelRequest::midMagnitude(double f) const noexcept {
    const double wc = f / lowCutHz;
    const double hp = pow4(wc) / std::sqrt(1.0 + pow4(wc) * pow4(wc));
    const double g = std::pow(10.0, subGainDb / 20.0);
    const double lpSub = 1.0 / (1.0 + pow4(f / subHz));
    return hp * (1.0 + (g - 1.0) * lpSub);
}

double SubKernelRequest::sideMagnitude(double f) const noexcept {
    const double wc = f / lowCutHz;
    const double hp = pow4(wc) / std::sqrt(1.0 + pow4(wc) * pow4(wc));
    const double w4 = pow4(f / monoHz);
    const double lp = (1.0 - slopeMix) / (1.0 + w4) + slopeMix / (1.0 + w4 * w4);
    return hp * (1.0 - monoAmount * lp);
}

} // namespace omnis
