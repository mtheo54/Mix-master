// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/LinearPhaseDesigner.h — FIR à phase linéaire depuis une réponse en module.
//
//  Conception par échantillonnage fréquentiel :
//    1. module cible |H(f)| à phase nulle, échantillonné sur 2N points ;
//    2. IFFT -> réponse impulsionnelle réelle et paire ;
//    3. fenêtre de Kaiser (β = 6) sur N taps centrés -> FIR causal, retard N/2 ;
//    4. découpage en partitions pour le convolueur UPOLS.
//  Deux réponses (canaux A et B du convolueur stéréo) sont conçues en une seule
//  IFFT complexe : A en partie réelle, B en partie imaginaire (chacune étant
//  réelle et paire dans le domaine fréquentiel, chacune reste réelle en temps).
//
//  Précision mesurée (N = 16384, 44.1 kHz) : Linkwitz-Riley à ±0.7 dB de la
//  cible jusqu'à −60 dB, Butterworth 4 à 16 Hz : −3.3 dB à fc, −64 dB au DC.
//  design() n'alloue pas : utilisable depuis un thread de calcul.
// =============================================================================
#pragma once

#include "Fft.h"
#include "StereoConvolver.h"

#include <complex>
#include <vector>

namespace omnis::dsp {

class LinearPhaseDesigner {
public:
    static constexpr double kKaiserBeta = 6.0;

    /// Non temps réel.
    void prepare(double sampleRate, int kernelLength, int blockSize);

    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] int kernelLength() const noexcept { return length_; }

    /// `magA(f)` et `magB(f)` : modules cibles (≥ 0) à la fréquence f (Hz).
    template <typename MagA, typename MagB>
    void design(MagA&& magA, MagB&& magB, StereoConvolver::Kernel& out) noexcept {
        const int m = 2 * length_;
        for (int k = 0; k <= length_; ++k) {
            const double f = k * sampleRate_ / m;
            const std::complex<double> z{magA(f), magB(f)};
            spectrum_[static_cast<std::size_t>(k)] = z;
            if (k > 0 && k < length_) spectrum_[static_cast<std::size_t>(m - k)] = z;
        }
        finish(out);
    }

private:
    void finish(StereoConvolver::Kernel& out) noexcept;

    double sampleRate_ = 48000.0;
    int length_ = 0;
    Fft fft_;
    std::vector<std::complex<double>> spectrum_;
    std::vector<double> window_, timeA_, timeB_;
    StereoConvolver::KernelBuilder builder_;
};

} // namespace omnis::dsp
