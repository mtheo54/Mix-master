// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/SvfFilter.h — Filtre d'état variable à topologie préservée (TPT SVF).
//
//  Référence : V. Zavalishin, « The Art of VA Filter Design » (TPT) ; forme
//  optimisée d'A. Simper (Cytomic, « SvfLinearTrapOptimised2 »).
//
//  Principe : les deux intégrateurs du SVF analogique sont discrétisés par la
//  règle trapézoïdale (équivalent bilinéaire avec pré-distorsion de fréquence)
//  en résolvant exactement la boucle sans retard (zero-delay feedback). Avantages
//  sur un biquad en forme directe :
//    - stabilité garantie même sous modulation rapide de fc et Q (l'état est
//      celui des intégrateurs, pas d'anciennes sorties : pas de « pops »),
//    - excellent comportement numérique aux basses fréquences (fc / fs ~ 1e-4,
//      cas du sub à 20 Hz sous 192 kHz) là où un biquad DF1/DF2 perd sa précision.
//
//  Coefficients (g = tan(π·fc/fs), k = 1/Q) :
//      a1 = 1 / (1 + g·(g + k)),  a2 = g·a1,  a3 = g·a2
//  Par échantillon :
//      v3 = x − ic2 ;  v1 = a1·ic1 + a2·v3 ;  v2 = ic2 + a2·ic1 + a3·v3
//      ic1 = 2·v1 − ic1 ;  ic2 = 2·v2 − ic2
//  v1 = sortie passe-bande (gain k⁻¹ au pic), v2 = passe-bas. Toutes les réponses
//  sont des combinaisons linéaires y = m0·x + m1·v1 + m2·v2 :
//
//      type        g                 k          m0     m1             m2
//      lowpass     tan(πf/fs)        1/Q        0      0              1
//      highpass    tan(πf/fs)        1/Q        1      −k             −1
//      bandpass    tan(πf/fs)        1/Q        0      k              0    (gain 0 dB au pic)
//      notch       tan(πf/fs)        1/Q        1      −k             0
//      allpass     tan(πf/fs)        1/Q        1      −2k            0
//      bell        tan(πf/fs)        1/(Q·A)    1      k·(A²−1)       0
//      lowShelf    tan(πf/fs)/√A     1/Q        1      k·(A−1)        A²−1
//      highShelf   tan(πf/fs)·√A     1/Q        A²     k·(1−A)·A      1−A²
//  avec A = 10^(gain_dB / 40).
// =============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace omnis::dsp {

enum class SvfType { LowPass, HighPass, BandPass, Notch, AllPass, Bell, LowShelf, HighShelf };

struct SvfCoefficients {
    double a1 = 1.0, a2 = 0.0, a3 = 0.0;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;

    static SvfCoefficients make(SvfType type, double sampleRate, double frequency, double q,
                                double gainDb = 0.0) noexcept {
        // Fréquence bornée sous Nyquist : tan() diverge à fs/2.
        const double f = std::clamp(frequency, 1.0e-3, 0.49 * sampleRate);
        const double qq = std::max(q, 1.0e-3);
        const double a = std::pow(10.0, gainDb / 40.0);
        double g = std::tan(std::numbers::pi * f / sampleRate);
        double k = 1.0 / qq;
        SvfCoefficients c;
        switch (type) {
            case SvfType::LowPass: c.m0 = 0.0; c.m1 = 0.0; c.m2 = 1.0; break;
            case SvfType::HighPass: c.m0 = 1.0; c.m1 = -k; c.m2 = -1.0; break;
            case SvfType::BandPass: c.m0 = 0.0; c.m1 = k; c.m2 = 0.0; break;
            case SvfType::Notch: c.m0 = 1.0; c.m1 = -k; c.m2 = 0.0; break;
            case SvfType::AllPass: c.m0 = 1.0; c.m1 = -2.0 * k; c.m2 = 0.0; break;
            case SvfType::Bell:
                k = 1.0 / (qq * a);
                c.m0 = 1.0; c.m1 = k * (a * a - 1.0); c.m2 = 0.0;
                break;
            case SvfType::LowShelf:
                g /= std::sqrt(a);
                c.m0 = 1.0; c.m1 = k * (a - 1.0); c.m2 = a * a - 1.0;
                break;
            case SvfType::HighShelf:
                g *= std::sqrt(a);
                c.m0 = a * a; c.m1 = k * (1.0 - a) * a; c.m2 = 1.0 - a * a;
                break;
        }
        c.a1 = 1.0 / (1.0 + g * (g + k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }
};

/// État d'un SVF pour un canal (2 doubles : les intégrateurs).
struct SvfState {
    double ic1 = 0.0, ic2 = 0.0;

    void reset() noexcept { ic1 = ic2 = 0.0; }

    double process(const SvfCoefficients& c, double x) noexcept {
        const double v3 = x - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return c.m0 * x + c.m1 * v1 + c.m2 * v2;
    }
};

/// SVF multicanal (coefficients partagés, un état par canal).
template <int NumChannels>
class SvfFilter {
public:
    void setCoefficients(const SvfCoefficients& c) noexcept { coeffs_ = c; }
    void set(SvfType type, double sampleRate, double frequency, double q, double gainDb = 0.0) noexcept {
        coeffs_ = SvfCoefficients::make(type, sampleRate, frequency, q, gainDb);
    }
    void reset() noexcept {
        for (auto& s : state_) s.reset();
    }
    double process(int channel, double x) noexcept { return state_[static_cast<std::size_t>(channel)].process(coeffs_, x); }
    const SvfCoefficients& coefficients() const noexcept { return coeffs_; }

private:
    SvfCoefficients coeffs_{};
    std::array<SvfState, NumChannels> state_{};
};

// -----------------------------------------------------------------------------
//  Butterworth d'ordre pair : cascade de sections du 2e ordre.
//  Les Q des sections d'un Butterworth d'ordre N valent
//      Q_i = 1 / (2·cos(π·(2i + 1) / (2N))),  i = 0 … N/2 − 1
//  ordre 2 : 0.7071 ; ordre 4 : 0.5412, 1.3066.
// -----------------------------------------------------------------------------
inline double butterworthQ(int order, int section) noexcept {
    return 1.0 / (2.0 * std::cos(std::numbers::pi * (2.0 * section + 1.0) / (2.0 * order)));
}

} // namespace omnis::dsp
