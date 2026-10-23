// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/StereoConvolver.h — Convolution FFT partitionnée uniforme (UPOLS), 2 canaux.
//
//  Uniformly Partitioned Overlap-Save (Garcia 2002, Wefers 2015) :
//   - le noyau de N taps est découpé en P = N / B partitions de B taps, chacune
//     transformée sur F = 2B points (moitié haute à zéro) ;
//   - chaque trame de B échantillons d'entrée forme, avec la trame précédente,
//     un bloc de F points transformé une fois et rangé dans une ligne à retard
//     fréquentielle (FDL) de P spectres ;
//   - sortie = IFFT( Σ_p FDL[p] · H[p] ), dont on garde les B derniers points
//     (overlap-save : les B premiers sont pollués par le recouvrement circulaire).
//  Latence propre : B échantillons. Coût par échantillon : O(P + log F).
//
//  Astuce « deux réels pour une FFT complexe » : les canaux A (Mid) et B (Side)
//  sont empaquetés en z = a + i·b. Après FFT, la symétrie hermitienne sépare
//  les spectres :
//        A_k = (Z_k + conj(Z_{F−k})) / 2      B_k = (Z_k − conj(Z_{F−k})) / (2i)
//  Chaque canal est multiplié par SON noyau, puis on recombine
//  Y = Y_A + i·Y_B (spectres hermitiens) : une seule IFFT rend a' en partie
//  réelle et b' en partie imaginaire. Une FFT + une IFFT par trame pour 2 canaux.
//
//  Deux noyaux peuvent être appliqués à la même trame (même FDL) : c'est ce qui
//  permet le fondu enchaîné sans clic lors d'un changement de réglage.
//
//  Rangement des spectres : parties réelles et imaginaires dans des tableaux
//  séparés ([A réel | A imag | B réel | B imag] par partition), pour que la
//  somme des produits (le cœur du coût, P produits par bin) soit vectorisée
//  par le compilateur. Mêmes opérations dans le même ordre qu'avec des
//  nombres complexes entrelacés : résultat identique au bit près.
// =============================================================================
#pragma once

#include "Fft.h"

#include <complex>
#include <vector>

namespace omnis::dsp {

class StereoConvolver {
public:
    using Complex = std::complex<double>;

    /// Spectres des partitions d'un noyau stéréo : P × 4 × (B + 1) valeurs,
    /// [A réel | A imag | B réel | B imag] par partition.
    struct Kernel {
        std::vector<double> data;
        void allocate(int partitions, int blockSize) {
            data.assign(static_cast<std::size_t>(partitions) * 4u * static_cast<std::size_t>(blockSize + 1), 0.0);
        }
    };

    /// Non temps réel. `kernelLength` multiple de `blockSize` (puissance de 2).
    void prepare(int blockSize, int kernelLength);
    void reset() noexcept;

    [[nodiscard]] int blockSize() const noexcept { return blockSize_; }
    [[nodiscard]] int numPartitions() const noexcept { return partitions_; }
    [[nodiscard]] int kernelLength() const noexcept { return partitions_ * blockSize_; }

    /// Alloue un noyau aux bonnes dimensions (non temps réel).
    void allocateKernel(Kernel& k) const { k.allocate(partitions_, blockSize_); }

    /// Transforme des noyaux temporels en partitions. Possède sa propre FFT et
    /// son propre tampon : peut tourner sur un thread de calcul pendant que le
    /// thread audio utilise le convolueur.
    class KernelBuilder {
    public:
        void prepare(int blockSize, int kernelLength);
        /// Sans allocation. `timeA` / `timeB` : kernelLength taps chacun.
        void build(const double* timeA, const double* timeB, Kernel& out) noexcept;

    private:
        int blockSize_ = 0, partitions_ = 0;
        Fft fft_;
        std::vector<Complex> work_;
    };

    /// Écrit l'échantillon `index` (0 … B−1) de la trame d'entrée courante.
    void write(int index, double a, double b) noexcept {
        inputA_[static_cast<std::size_t>(blockSize_ + index)] = a;
        inputB_[static_cast<std::size_t>(blockSize_ + index)] = b;
    }

    /// Trame d'entrée complète : transforme et pousse dans la FDL, puis calcule
    /// la sortie avec `primary` (et `secondary` si non nul, pour un fondu).
    void processFrame(const Kernel& primary, const Kernel* secondary) noexcept;

    /// Sorties de la dernière trame calculée (B échantillons).
    [[nodiscard]] double outputA(int index) const noexcept { return outA_[static_cast<std::size_t>(index)]; }
    [[nodiscard]] double outputB(int index) const noexcept { return outB_[static_cast<std::size_t>(index)]; }
    [[nodiscard]] double secondaryA(int index) const noexcept { return out2A_[static_cast<std::size_t>(index)]; }
    [[nodiscard]] double secondaryB(int index) const noexcept { return out2B_[static_cast<std::size_t>(index)]; }

private:
    void accumulate(const Kernel& k, double* outA, double* outB) noexcept;

    int blockSize_ = 0;
    int fftSize_ = 0;
    int partitions_ = 0;
    int head_ = 0;
    Fft fft_;
    std::vector<double> inputA_, inputB_;   // 2B : trame précédente + courante
    std::vector<double> fdl_;               // P × 4 × (B+1), même rangement que Kernel
    std::vector<Complex> work_;             // F
    std::vector<double> acc_;               // 4 × (B+1)
    std::vector<double> outA_, outB_, out2A_, out2B_;
};

} // namespace omnis::dsp
