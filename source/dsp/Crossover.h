// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/Crossover.h — Crossovers Linkwitz-Riley (LR4 / LR8) et réseau N bandes.
//
//  Linkwitz-Riley d'ordre 2N = Butterworth d'ordre N au carré :
//      LP_LR = (LP_BW)²   HP_LR = (HP_BW)²
//  Propriétés : −6.02 dB pour chaque voie à fc, voies en phase à toutes les
//  fréquences, et surtout somme à module unitaire :
//      LR4 : LP + HP = (s² − √2·s + 1) / (s² + √2·s + 1)       (passe-tout 2e ordre, Q = 0.7071)
//      LR8 : LP + HP = B4(−s) / B4(s) = AP(Q=0.5412)·AP(Q=1.3066)
//  (car B(s)·B(−s) = 1 + s^2N pour un Butterworth). Toutes les sections sont
//  pré-distordues à la même fc : l'identité reste EXACTE après transformée
//  bilinéaire, aux erreurs d'arrondi près (vérifié à 1e-12 par les tests).
//
//  Réseau N bandes (fc croissantes f1 < … < fN−1), schéma en arbre :
//      x ─ XO(f1) ─ bas ──────────── AP(f2)·…·AP(fN−1) ─► bande 1
//               └ haut ─ XO(f2) ─ bas ── AP(f3)·…      ─► bande 2
//                              └ haut ─ …               ─► bande N
//  La somme des bandes vaut alors Π AP(fi) : module plat, phase identique à
//  celle d'un chemin non découpé passé dans les mêmes passe-tout. Ces passe-tout
//  sont exposés pour aligner tout signal parallèle (ex. canal Mid du Mono Maker).
// =============================================================================
#pragma once

#include "SvfFilter.h"

#include <array>
#include <cassert>
#include <cmath>

namespace omnis::dsp {

enum class CrossoverSlope : int { LR4 = 4, LR8 = 8 };

/// Passe-tout équivalent à la somme LP + HP d'un crossover Linkwitz-Riley.
template <int NumChannels>
class LinkwitzRileyAllpass {
public:
    void setup(double sampleRate, double frequency, CrossoverSlope slope) noexcept {
        numSections_ = slope == CrossoverSlope::LR4 ? 1 : 2;
        const int bwOrder = static_cast<int>(slope) / 2;
        for (int s = 0; s < numSections_; ++s)
            coeffs_[static_cast<std::size_t>(s)] =
                SvfCoefficients::make(SvfType::AllPass, sampleRate, frequency, butterworthQ(bwOrder, s));
    }
    void reset() noexcept {
        for (auto& ch : state_)
            for (auto& s : ch) s.reset();
    }
    double process(int channel, double x) noexcept {
        auto& st = state_[static_cast<std::size_t>(channel)];
        for (int s = 0; s < numSections_; ++s) x = st[static_cast<std::size_t>(s)].process(coeffs_[static_cast<std::size_t>(s)], x);
        return x;
    }

private:
    std::array<SvfCoefficients, 2> coeffs_{};
    std::array<std::array<SvfState, 2>, NumChannels> state_{};
    int numSections_ = 1;
};

/// Crossover 2 voies Linkwitz-Riley, multicanal.
template <int NumChannels>
class LinkwitzRileyCrossover {
public:
    void setup(double sampleRate, double frequency, CrossoverSlope slope) noexcept {
        const int bwOrder = static_cast<int>(slope) / 2; // 2 (LR4) ou 4 (LR8)
        const int bwSections = bwOrder / 2;
        numSections_ = 2 * bwSections; // Butterworth au carré
        for (int s = 0; s < numSections_; ++s) {
            const double q = butterworthQ(bwOrder, s % bwSections);
            low_[static_cast<std::size_t>(s)] = SvfCoefficients::make(SvfType::LowPass, sampleRate, frequency, q);
            high_[static_cast<std::size_t>(s)] = SvfCoefficients::make(SvfType::HighPass, sampleRate, frequency, q);
        }
    }

    void reset() noexcept {
        for (auto& ch : lowState_)
            for (auto& s : ch) s.reset();
        for (auto& ch : highState_)
            for (auto& s : ch) s.reset();
    }

    void process(int channel, double x, double& low, double& high) noexcept {
        auto& ls = lowState_[static_cast<std::size_t>(channel)];
        auto& hs = highState_[static_cast<std::size_t>(channel)];
        double l = x, h = x;
        for (int s = 0; s < numSections_; ++s) {
            l = ls[static_cast<std::size_t>(s)].process(low_[static_cast<std::size_t>(s)], l);
            h = hs[static_cast<std::size_t>(s)].process(high_[static_cast<std::size_t>(s)], h);
        }
        low = l;
        high = h;
    }

private:
    std::array<SvfCoefficients, 4> low_{}, high_{};
    std::array<std::array<SvfState, 4>, NumChannels> lowState_{}, highState_{};
    int numSections_ = 2;
};

/// Réseau de crossovers N bandes à somme passe-tout (N <= kMaxBands).
template <int NumChannels>
class CrossoverNetwork {
public:
    static constexpr int kMaxBands = 8;

    /// `frequencies` : numBands − 1 fréquences strictement croissantes.
    void setup(double sampleRate, const double* frequencies, int numBands, CrossoverSlope slope) noexcept {
        assert(numBands >= 1 && numBands <= kMaxBands);
        numBands_ = std::clamp(numBands, 1, kMaxBands);
        for (int i = 0; i < numBands_ - 1; ++i) {
            crossovers_[static_cast<std::size_t>(i)].setup(sampleRate, frequencies[i], slope);
            // La bande b passe par les passe-tout des crossovers b+1 … N−2.
            for (int b = 0; b < i; ++b)
                compensation_[static_cast<std::size_t>(b)][static_cast<std::size_t>(i)].setup(sampleRate, frequencies[i], slope);
        }
    }

    void reset() noexcept {
        for (auto& x : crossovers_) x.reset();
        for (auto& row : compensation_)
            for (auto& ap : row) ap.reset();
    }

    [[nodiscard]] int numBands() const noexcept { return numBands_; }

    /// Découpe un échantillon du canal `channel` en `numBands()` bandes.
    void process(int channel, double x, double* bands) noexcept {
        double rest = x;
        for (int i = 0; i < numBands_ - 1; ++i) {
            double low, high;
            crossovers_[static_cast<std::size_t>(i)].process(channel, rest, low, high);
            bands[i] = low;
            rest = high;
        }
        bands[numBands_ - 1] = rest;
        for (int b = 0; b < numBands_ - 2; ++b)
            for (int i = b + 1; i < numBands_ - 1; ++i)
                bands[b] = compensation_[static_cast<std::size_t>(b)][static_cast<std::size_t>(i)].process(channel, bands[b]);
    }

private:
    int numBands_ = 1;
    std::array<LinkwitzRileyCrossover<NumChannels>, kMaxBands - 1> crossovers_{};
    // compensation_[bande][crossover]
    std::array<std::array<LinkwitzRileyAllpass<NumChannels>, kMaxBands - 1>, kMaxBands - 1> compensation_{};
};

} // namespace omnis::dsp
