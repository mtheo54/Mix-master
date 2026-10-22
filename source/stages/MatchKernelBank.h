// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/MatchKernelBank.h — Noyaux FIR à phase linéaire du Match EQ.
//
//  La courbe est définie par kMatchBands nœuds (demi-octave, 20 Hz – 20 kHz),
//  en dB, pour le Mid et pour le Side. Entre deux nœuds : spline de
//  Catmull-Rom en log-fréquence (C1, reproduit exactement une pente
//  régulière). Hors de [20 Hz, 20.5 kHz] : valeur du nœud extrême.
//      |H_M(f)| = 10^(a · C_M(f) / 20)      |H_S(f)| = 10^(a · C_S(f) / 20)
//  avec a = Match Amount. Courbe nulle -> noyau = impulsion exacte au centre
//  (null-test bit-exact au retard près).
// =============================================================================
#pragma once

#include "dsp/FirKernelBank.h"
#include "dsp/LinearPhaseDesigner.h"
#include "params/Parameters.h"

#include <array>

namespace omnis {

using MatchCurve = std::array<double, params::kMatchBands>;

/// Gain (dB) de la courbe aux nœuds `nodes` à la fréquence f.
double matchCurveDb(const MatchCurve& nodes, double frequency) noexcept;

struct MatchKernelRequest {
    MatchCurve midDb{};
    MatchCurve sideDb{};
    double amount = 1.0; ///< 0..1

    [[nodiscard]] bool nearlyEquals(const MatchKernelRequest& o) const noexcept;
    [[nodiscard]] double midMagnitude(double frequency) const noexcept;
    [[nodiscard]] double sideMagnitude(double frequency) const noexcept;
};

/// Canal A = Mid, canal B = Side.
class MatchKernelDesigner {
public:
    void prepare(double sampleRate, int kernelLength, int blockSize) { designer_.prepare(sampleRate, kernelLength, blockSize); }
    void design(const MatchKernelRequest& r, dsp::StereoConvolver::Kernel& out) noexcept {
        designer_.design([&](double f) { return r.midMagnitude(f); }, [&](double f) { return r.sideMagnitude(f); }, out);
    }

private:
    dsp::LinearPhaseDesigner designer_;
};

using MatchKernelBank = dsp::FirKernelBank<MatchKernelRequest, MatchKernelDesigner>;

} // namespace omnis
