// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/Halfband.h — Filtres demi-bande polyphase IIR (méthode de L. de Soras, HIIR).
//
//  Un filtre demi-bande elliptique s'écrit comme la moyenne de deux chaînes de
//  passe-tout du 1er ordre en z² :
//        H(z) = ½ · ( A0(z²) + z⁻¹ · A1(z²) ),    a_i(z) = (c_i + z⁻²) / (1 + c_i·z⁻²)
//  coefficients pairs dans A0, impairs dans A1. Propriétés : complémentarité en
//  puissance (|H|² + |H(−z)|² = 1, ondulation de bande passante ~1e-12 dB),
//  phase minimale (aucun pré-écho : préserve l'attaque des transitoires avant
//  un clipper), et coût minimal grâce à la forme polyphase : chaque passe-tout
//  tourne au taux le plus bas.
//
//  Conception (de Soras, d'après les fonctions elliptiques de Jacobi) :
//    paramètres  k = tan²((1 − 2·tw)·π/4),  e = ½·(1 − (1−k²)^¼)/(1 + (1−k²)^¼)
//                q = e·(1 + e⁴·(2 + e⁴·(15 + 150·e⁴)))   (nome elliptique)
//    ordre       N = ⌈ log(a²/16) / log(q) ⌉ impair,  a = 10^(−att/10) / (1 − 10^(−att/10))
//    coefficient i (c = i + 1) :
//       ω  = q^¼ · Σ_j (−1)^j q^(j(j+1)) sin((2j+1)·c·π/N)  /  (½ + Σ_{j≥1} (−1)^j q^(j²) cos(2j·c·π/N))
//       x  = sqrt((1 − ω²k)(1 − ω²/k)) / (1 + ω²),   c_i = (1 − x)/(1 + x)
//  `tw` est la demi-largeur de transition relative au taux SURÉCHANTILLONNÉ :
//  bande passante [0, 0.25 − tw], bande coupée [0.25 + tw, 0.5].
//  Vérifié par prototype : 120 dB / tw 0.02 -> 12 coefficients, -123.0 dB.
//
//  Polyphase (taux de base n) :
//    suréchantillonnage  y[2n] = A0·x[n],  y[2n+1] = A1·x[n]          (gain 2 compris)
//    sous-échantillonnage y[n] = ½·( A0·v[2n] + A1·v[2n−1] )
//  avec, au taux de base, a(z) = (c + z⁻¹)/(1 + c·z⁻¹) :  y = c·(x − y₁) + x₁.
// =============================================================================
#pragma once

#include <array>
#include <vector>

namespace omnis::dsp {

/// Coefficients d'un demi-bande elliptique (atténuation en dB, transition relative).
std::vector<double> designHalfband(double attenuationDb, double transition);

/// Chaîne de passe-tout du 1er ordre (au taux de base de l'étage polyphase).
class AllpassChain {
public:
    static constexpr int kMaxSections = 16;

    void setCoefficients(const double* coefs, int count) noexcept;
    void reset() noexcept {
        x1_.fill(0.0);
        y1_.fill(0.0);
    }
    double process(double x) noexcept {
        for (int i = 0; i < count_; ++i) {
            const double y = c_[static_cast<std::size_t>(i)] * (x - y1_[static_cast<std::size_t>(i)]) + x1_[static_cast<std::size_t>(i)];
            x1_[static_cast<std::size_t>(i)] = x;
            y1_[static_cast<std::size_t>(i)] = y;
            x = y;
        }
        return x;
    }

private:
    std::array<double, kMaxSections> c_{}, x1_{}, y1_{};
    int count_ = 0;
};

/// Étage 2x (montée + descente) pour un canal.
class Halfband2x {
public:
    void setCoefficients(const std::vector<double>& coefs) noexcept;
    void reset() noexcept;

    /// x -> (out0, out1) au double du taux.
    void upsample(double x, double& out0, double& out1) noexcept {
        out0 = up0_.process(x);
        out1 = up1_.process(x);
    }
    /// (v[2n], v[2n+1]) -> y[n].
    double downsample(double in0, double in1) noexcept {
        const double y = 0.5 * (down0_.process(in0) + down1_.process(previousOdd_));
        previousOdd_ = in1;
        return y;
    }

private:
    AllpassChain up0_, up1_, down0_, down1_;
    double previousOdd_ = 0.0;
};

} // namespace omnis::dsp
