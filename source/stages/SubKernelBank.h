// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/SubKernelBank.h — Noyaux FIR à phase linéaire du Sub Control.
//
//  Réponses en module cibles, à phase nulle (conçues par LinearPhaseDesigner) :
//      Mid  : H_M(f) = HP(f) · (1 + (g − 1)·LP_sub(f))
//      Side : H_S(f) = HP(f) · (1 − a·LP_mono(f))
//  avec  HP      = Butterworth 4 (low cut)  |H| = w⁴ / sqrt(1 + w⁸)
//        LP_mono = Linkwitz-Riley           |H| = 1/(1 + w⁴) (LR4) ou 1/(1 + w⁸) (LR8)
//        LP_sub  = LR4 à la fréquence Sub,  g = 10^(gain/20),  a = Mono Amount
//  1 − LP_LR est exactement le passe-haut LR complémentaire : Mid et Side
//  restent rigoureusement en phase (aucune rotation, aucun pré-écho M/S).
//  Les noyaux transitent par une FirKernelBank (calcul en tâche de fond en
//  temps réel, immédiat hors ligne).
// =============================================================================
#pragma once

#include "dsp/FirKernelBank.h"
#include "dsp/LinearPhaseDesigner.h"

namespace omnis {

struct SubKernelRequest {
    double lowCutHz = 16.0;
    double monoHz = 120.0;
    double monoAmount = 1.0; ///< 0..1
    double slopeMix = 0.0;   ///< 0 = LR4, 1 = LR8 (fondu continu)
    double subGainDb = 0.0;
    double subHz = 70.0;

    /// Égalité à une tolérance inaudible (évite des recalculs inutiles).
    [[nodiscard]] bool nearlyEquals(const SubKernelRequest& o) const noexcept;

    [[nodiscard]] double midMagnitude(double frequency) const noexcept;
    [[nodiscard]] double sideMagnitude(double frequency) const noexcept;
};

/// Canal A = Mid, canal B = Side.
class SubKernelDesigner {
public:
    void prepare(double sampleRate, int kernelLength, int blockSize) { designer_.prepare(sampleRate, kernelLength, blockSize); }
    void design(const SubKernelRequest& r, dsp::StereoConvolver::Kernel& out) noexcept {
        designer_.design([&](double f) { return r.midMagnitude(f); }, [&](double f) { return r.sideMagnitude(f); }, out);
    }

private:
    dsp::LinearPhaseDesigner designer_;
};

using SubKernelBank = dsp::FirKernelBank<SubKernelRequest, SubKernelDesigner>;

} // namespace omnis
