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
//
//  Tilt T (réglage à la main, non dosé par a) : une droite en octaves autour
//  du nœud central (640 Hz, milieu géométrique de 20 Hz – 20 kHz), −T dB à
//  20 Hz, +T dB à 20.5 kHz, constante au-delà. Mid et Side ensemble : « plus
//  brillant / plus sombre » d'un seul geste, sans toucher à l'image stéréo.
//
//  Caractère Analogique : la courbe moyennée sur ±1/3 d'octave (cloches plus
//  larges, plus douces) et un noyau à phase minimale (aucun pré-écho).
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
    double tiltDb = 0.0; ///< gain à 20 kHz (et opposé à 20 Hz)
    bool analog = false; ///< caractère Analogique : phase minimale, cloches lissées sur ±1/3 d'octave

    [[nodiscard]] double tiltAt(double frequency) const noexcept;
    [[nodiscard]] bool nearlyEquals(const MatchKernelRequest& o) const noexcept;
    [[nodiscard]] double midMagnitude(double frequency) const noexcept;
    [[nodiscard]] double sideMagnitude(double frequency) const noexcept;
};

/// Canal A = Mid, canal B = Side.
class MatchKernelDesigner {
public:
    void prepare(double sampleRate, int kernelLength, int blockSize, bool zeroDelay = false) { designer_.prepare(sampleRate, kernelLength, blockSize, zeroDelay); }
    void design(const MatchKernelRequest& r, dsp::StereoConvolver::Kernel& out) noexcept {
        if (r.analog)
            designer_.designMinimumPhase([&](double f) { return r.midMagnitude(f); }, [&](double f) { return r.sideMagnitude(f); }, out);
        else
            designer_.design([&](double f) { return r.midMagnitude(f); }, [&](double f) { return r.sideMagnitude(f); }, out);
    }

private:
    dsp::LinearPhaseDesigner designer_;
};

using MatchKernelBank = dsp::FirKernelBank<MatchKernelRequest, MatchKernelDesigner>;

} // namespace omnis
