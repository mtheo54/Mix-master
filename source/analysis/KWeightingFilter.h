// =============================================================================
//  Abrasion Labs // OMNIS
//  analysis/KWeightingFilter.h — Pondération K (ITU-R BS.1770-4, §2.1).
//
//  Deux biquads en cascade :
//   1. « Pre-filter » : shelf haut ~ +4 dB au-dessus de ~1.5 kHz, modélise
//      l'effet acoustique de la tête (sphère rigide).
//   2. « RLB » : passe-haut du 2e ordre ~ 38 Hz (Revised Low-frequency B-curve).
//
//  La norme ne publie les coefficients qu'à 48 kHz. Pour toute autre fréquence
//  on recalcule par transformée bilinéaire à partir des prototypes analogiques
//  (paramètres f0 / G / Q identifiés sur les coefficients officiels — même
//  dérivation que libebur128) :
//
//    Stage 1 (shelf) : f0 = 1681.974450955533 Hz, G = 3.999843853973347 dB,
//                      Q = 0.7071752369554196
//        K  = tan(π f0 / fs),  Vh = 10^(G/20),  Vb = Vh^0.4996667741545416
//        a0 = 1 + K/Q + K²
//        b0 = (Vh + Vb·K/Q + K²)/a0   b1 = 2(K² - Vh)/a0   b2 = (Vh - Vb·K/Q + K²)/a0
//        a1 = 2(K² - 1)/a0            a2 = (1 - K/Q + K²)/a0
//
//    Stage 2 (HPF)   : f0 = 38.13547087602444 Hz, Q = 0.5003270373238773
//        b = [1, -2, 1],  a1 = 2(K² - 1)/a0,  a2 = (1 - K/Q + K²)/a0
//
//  À 48 kHz on retrouve les valeurs de la Table 1/2 de BS.1770 à 1e-10 près
//  (vérifié par test unitaire).
//
//  Implémentation : forme directe II transposée en double — bruit de calcul
//  négligeable et état minimal (2 variables par biquad et par canal).
// =============================================================================
#pragma once

#include <cmath>
#include <numbers>

namespace omnis::analysis {

struct BiquadCoefficients {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

struct BiquadState {
    double z1 = 0.0, z2 = 0.0;

    // Forme directe II transposée :
    //   y  = b0·x + z1
    //   z1 = b1·x - a1·y + z2
    //   z2 = b2·x - a2·y
    double process(const BiquadCoefficients& c, double x) noexcept {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0.0; }
};

inline BiquadCoefficients kWeightingShelf(double sampleRate) noexcept {
    constexpr double f0 = 1681.974450955533;
    constexpr double gainDb = 3.999843853973347;
    constexpr double q = 0.7071752369554196;
    const double k = std::tan(std::numbers::pi * f0 / sampleRate);
    const double vh = std::pow(10.0, gainDb / 20.0);
    const double vb = std::pow(vh, 0.4996667741545416);
    const double a0 = 1.0 + k / q + k * k;
    BiquadCoefficients c;
    c.b0 = (vh + vb * k / q + k * k) / a0;
    c.b1 = 2.0 * (k * k - vh) / a0;
    c.b2 = (vh - vb * k / q + k * k) / a0;
    c.a1 = 2.0 * (k * k - 1.0) / a0;
    c.a2 = (1.0 - k / q + k * k) / a0;
    return c;
}

inline BiquadCoefficients kWeightingHighPass(double sampleRate) noexcept {
    constexpr double f0 = 38.13547087602444;
    constexpr double q = 0.5003270373238773;
    const double k = std::tan(std::numbers::pi * f0 / sampleRate);
    const double a0 = 1.0 + k / q + k * k;
    BiquadCoefficients c;
    c.b0 = 1.0;
    c.b1 = -2.0;
    c.b2 = 1.0;
    c.a1 = 2.0 * (k * k - 1.0) / a0;
    c.a2 = (1.0 - k / q + k * k) / a0;
    return c;
}

/// Filtre K complet pour un canal.
class KWeightingFilter {
public:
    void prepare(double sampleRate) noexcept {
        shelf_ = kWeightingShelf(sampleRate);
        highPass_ = kWeightingHighPass(sampleRate);
        reset();
    }

    void reset() noexcept {
        s1_.reset();
        s2_.reset();
    }

    double process(double x) noexcept { return s2_.process(highPass_, s1_.process(shelf_, x)); }

private:
    BiquadCoefficients shelf_, highPass_;
    BiquadState s1_, s2_;
};

} // namespace omnis::analysis
