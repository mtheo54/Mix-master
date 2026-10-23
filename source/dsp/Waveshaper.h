// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/Waveshaper.h — Courbes de clipping et anti-repliement par antidérivée.
//
//  Toutes les courbes sont normalisées : pente 1 autour de 0 (transparentes à
//  bas niveau) et |f(u)| <= 1 (le plafond est appliqué par mise à l'échelle).
//
//   HARD      f(u) = clamp(u, −1, 1)
//             F(u) = u²/2 (|u| <= 1),  |u| − ½ (sinon)
//   SOFT KNEE linéaire jusqu'à a = 1 − k, raccord parabolique C¹ sur [a, b = 1 + k],
//             plat au-delà :  f(u) = u − (u − a)² / (4k)  sur [a, b]
//             F(u) = u²/2 − (u − a)³/(12k) sur [a, b] ; F(b) + (u − b) au-delà
//             k = largeur du genou (0 = hard, 1 = très doux, commence à 0 dBFS − ∞)
//   DIODE     asymétrique, harmoniques paires (couleur « tube / diode ») :
//             u >= 0 : f = 1 − e^(−u)                F = u + e^(−u) − 1
//             u <  0 : f = −(1 − e^(r·u)) / r         F = −u/r + (e^(r·u) − 1)/r²
//             r = 1.25 -> alternance négative plafonnée à −0.8 (asymétrie 1.9 dB).
//             Crée un offset continu : l'étage le retire de cette seule composante
//             (un passe-haut sur les courbes symétriques ferait pencher les
//             plateaux des graves écrêtés et augmenterait les crêtes).
//
//  ADAA du 1er ordre (Parker, Zavalishin, Le Bihan, 2016) :
//        y[n] = (F(u[n]) − F(u[n−1])) / (u[n] − u[n−1])
//  c'est la moyenne exacte de f sur le segment [u[n−1], u[n]] : les
//  discontinuités de pente sont « intégrées », ce qui atténue le repliement de
//  ~20 dB à suréchantillonnage égal. Coût : demi-échantillon de retard au taux
//  suréchantillonné, et un passe-bas en cos(π·f/fs_os) (−0.14 dB à 20 kHz en 8x).
//  La sortie reste bornée par ±1 (moyenne de valeurs bornées).
// =============================================================================
#pragma once

#include <cmath>

namespace omnis::dsp {

enum class ClipShape : int { Hard = 0, SoftKnee = 1, Diode = 2 };

namespace shapes {

inline constexpr double kDiodeRatio = 1.25;

inline double hard(double u) noexcept { return u > 1.0 ? 1.0 : (u < -1.0 ? -1.0 : u); }
inline double hardIntegral(double u) noexcept {
    const double a = std::fabs(u);
    return a <= 1.0 ? 0.5 * u * u : a - 0.5;
}

inline double softKnee(double u, double k) noexcept {
    if (k < 1e-4) return hard(u);
    const double a = std::fabs(u);
    const double lo = 1.0 - k, hi = 1.0 + k;
    double y;
    if (a <= lo) y = a;
    else if (a < hi) y = a - (a - lo) * (a - lo) / (4.0 * k);
    else y = 1.0;
    return u < 0.0 ? -y : y;
}
inline double softKneeIntegral(double u, double k) noexcept {
    if (k < 1e-4) return hardIntegral(u);
    const double a = std::fabs(u); // f impaire -> F paire
    const double lo = 1.0 - k, hi = 1.0 + k;
    if (a <= lo) return 0.5 * a * a;
    if (a < hi) {
        const double d = a - lo;
        return 0.5 * a * a - d * d * d / (12.0 * k);
    }
    // F(hi) = hi²/2 − (2k)³/(12k) = hi²/2 − 2k²/3
    return 0.5 * hi * hi - 2.0 * k * k / 3.0 + (a - hi);
}

inline double diode(double u) noexcept {
    return u >= 0.0 ? 1.0 - std::exp(-u) : -(1.0 - std::exp(kDiodeRatio * u)) / kDiodeRatio;
}
inline double diodeIntegral(double u) noexcept {
    if (u >= 0.0) return u + std::exp(-u) - 1.0;
    return -u / kDiodeRatio + (std::exp(kDiodeRatio * u) - 1.0) / (kDiodeRatio * kDiodeRatio);
}

} // namespace shapes

/// Clipper ADAA à 3 courbes mélangeables (fondu entre modes sans clic).
class AdaaClipper {
public:
    void reset() noexcept {
        u1_ = 0.0;
        fH1_ = fS1_ = fD1_ = 0.0;
        k1_ = -1.0;
        diodeLive_ = false;
    }

    struct Output {
        double symmetric; ///< wHard·hard + wSoft·soft (sans offset continu)
        double diode;     ///< wDiode·diode (asymétrique : à débarrasser de son DC)
    };

    /// `wHard + wSoft + wDiode = 1`. `knee` en [0, 1].
    Output process(double u, double knee, double wHard, double wSoft, double wDiode) noexcept {
        const double fH = shapes::hardIntegral(u);
        const double fS = shapes::softKneeIntegral(u, knee);
        // La courbe Diode (deux exp) n'est calculée que si elle est entendue ; quand
        // elle revient, son intégrale précédente est recalculée : même résultat
        // que si elle avait toujours tourné.
        const bool diodeOn = wDiode != 0.0;
        if (diodeOn && !diodeLive_) fD1_ = shapes::diodeIntegral(u1_);
        diodeLive_ = diodeOn;
        const double fD = diodeOn ? shapes::diodeIntegral(u) : 0.0;
        // Si le genou a changé, l'intégrale précédente doit être recalculée avec
        // le nouveau genou (sinon la différence F(u) − F(u₁) serait incohérente).
        if (knee != k1_) {
            fS1_ = shapes::softKneeIntegral(u1_, knee);
            k1_ = knee;
        }
        const double du = u - u1_;
        double yH, yS, yD;
        if (std::fabs(du) > 1e-9) {
            const double inv = 1.0 / du;
            yH = (fH - fH1_) * inv;
            yS = (fS - fS1_) * inv;
            yD = diodeOn ? (fD - fD1_) * inv : 0.0;
        } else {
            // Segment dégénéré : valeur au milieu (limite exacte de la formule).
            const double m = 0.5 * (u + u1_);
            yH = shapes::hard(m);
            yS = shapes::softKnee(m, knee);
            yD = diodeOn ? shapes::diode(m) : 0.0;
        }
        u1_ = u;
        fH1_ = fH;
        fS1_ = fS;
        fD1_ = fD;
        return {wHard * yH + wSoft * yS, wDiode * yD};
    }

private:
    double u1_ = 0.0, fH1_ = 0.0, fS1_ = 0.0, fD1_ = 0.0, k1_ = -1.0;
    bool diodeLive_ = false;
};

} // namespace omnis::dsp
