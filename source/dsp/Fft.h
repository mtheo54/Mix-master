// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/Fft.h — FFT complexe radix-2 en double précision, sans dépendance.
//
//  Cooley-Tukey itératif à décimation temporelle :
//    1. permutation par inversion de bits (table précalculée) ;
//    2. log2(N) étages de papillons  u' = u + w·v,  v' = u − w·v,
//       avec w = e^(−2πi·k/N) lu dans une table précalculée (pas de sin/cos à
//       l'exécution, précision ~1 ulp par facteur).
//  Erreur relative typique ~ 1e-15·log2(N) : négligeable devant le bruit d'un
//  flottant 32 bits, ce qui permet des null-tests de convolution à 1e-12.
//
//  prepare() alloue (non temps réel) ; forward()/inverse() sont temps réel,
//  en place, sans allocation. inverse() applique la normalisation 1/N.
// =============================================================================
#pragma once

#include <complex>
#include <vector>

namespace omnis::dsp {

/// Produit complexe explicite. Sans -ffast-math, `std::complex * std::complex`
/// passe par __muldc3 (gestion NaN/inf de l'annexe G du C99), ~5x plus lent.
inline std::complex<double> cmul(const std::complex<double>& a, const std::complex<double>& b) noexcept {
    return {a.real() * b.real() - a.imag() * b.imag(), a.real() * b.imag() + a.imag() * b.real()};
}

class Fft {
public:
    using Complex = std::complex<double>;

    Fft() = default;
    explicit Fft(int size) { prepare(size); }

    /// `size` doit être une puissance de deux >= 2.
    void prepare(int size);

    [[nodiscard]] int size() const noexcept { return size_; }

    void forward(Complex* data) const noexcept { transform(data, false); }
    void inverse(Complex* data) const noexcept;

private:
    void transform(Complex* data, bool inverse) const noexcept;

    int size_ = 0;
    std::vector<Complex> twiddles_; ///< e^(−2πi·k/N), k < N/2
    std::vector<int> bitReverse_;
};

} // namespace omnis::dsp
