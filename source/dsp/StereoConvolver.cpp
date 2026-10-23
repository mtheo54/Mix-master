// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/StereoConvolver.cpp
// =============================================================================
#include "StereoConvolver.h"

#include <algorithm>
#include <cassert>

namespace omnis::dsp {

namespace {

using Complex = std::complex<double>;

/// Sépare Z = FFT(a + i·b) en A et B (bins 0 … F/2), rangés [A réel | A imag | B réel | B imag].
void splitSpectra(const Complex* z, int fftSize, double* out) noexcept {
    const int half = fftSize / 2, bins = half + 1;
    double* ar = out;
    double* ai = ar + bins;
    double* br = ai + bins;
    double* bi = br + bins;
    for (int k = 0; k <= half; ++k) {
        const Complex zk = z[k];
        const Complex zn = std::conj(z[(fftSize - k) % fftSize]);
        const Complex a = 0.5 * (zk + zn);
        ar[k] = a.real();
        ai[k] = a.imag();
        // (zk − zn) / (2i) = −i·(zk − zn)/2
        const Complex d = zk - zn;
        br[k] = 0.5 * d.imag();
        bi[k] = -0.5 * d.real();
    }
}

/// Reconstruit Z = Y_A + i·Y_B sur F points à partir des demi-spectres.
void mergeSpectra(const double* acc, int fftSize, Complex* z) noexcept {
    const int half = fftSize / 2, bins = half + 1;
    const double* ar = acc;
    const double* ai = ar + bins;
    const double* br = ai + bins;
    const double* bi = br + bins;
    for (int k = 0; k <= half; ++k) z[k] = Complex{ar[k] + -bi[k], ai[k] + br[k]};
    for (int k = half + 1; k < fftSize; ++k) {
        const int m = fftSize - k;
        // conj(A) + i·conj(B)
        z[k] = Complex{ar[m] + bi[m], -ai[m] + br[m]};
    }
}

/// acc += x · h sur `bins` bins complexes rangés en tableaux réel / imaginaire.
void multiplyAdd(double* __restrict accRe, double* __restrict accIm, const double* __restrict xr, const double* __restrict xi,
                 const double* __restrict hr, const double* __restrict hi, int bins) noexcept {
    for (int k = 0; k < bins; ++k) {
        accRe[k] += xr[k] * hr[k] - xi[k] * hi[k];
        accIm[k] += xr[k] * hi[k] + xi[k] * hr[k];
    }
}

} // namespace

void StereoConvolver::prepare(int blockSize, int kernelLength) {
    assert(blockSize >= 2 && (blockSize & (blockSize - 1)) == 0);
    assert(kernelLength >= blockSize && kernelLength % blockSize == 0);
    blockSize_ = blockSize;
    fftSize_ = 2 * blockSize;
    partitions_ = kernelLength / blockSize;
    fft_.prepare(fftSize_);
    const auto bins = static_cast<std::size_t>(blockSize + 1);
    inputA_.assign(static_cast<std::size_t>(fftSize_), 0.0);
    inputB_.assign(static_cast<std::size_t>(fftSize_), 0.0);
    fdl_.assign(static_cast<std::size_t>(partitions_) * 4u * bins, 0.0);
    work_.assign(static_cast<std::size_t>(fftSize_), Complex{});
    acc_.assign(4u * bins, 0.0);
    outA_.assign(static_cast<std::size_t>(blockSize), 0.0);
    outB_.assign(outA_.size(), 0.0);
    out2A_.assign(outA_.size(), 0.0);
    out2B_.assign(outA_.size(), 0.0);
    head_ = 0;
}

void StereoConvolver::reset() noexcept {
    std::fill(inputA_.begin(), inputA_.end(), 0.0);
    std::fill(inputB_.begin(), inputB_.end(), 0.0);
    std::fill(fdl_.begin(), fdl_.end(), 0.0);
    std::fill(outA_.begin(), outA_.end(), 0.0);
    std::fill(outB_.begin(), outB_.end(), 0.0);
    std::fill(out2A_.begin(), out2A_.end(), 0.0);
    std::fill(out2B_.begin(), out2B_.end(), 0.0);
    head_ = 0;
}

void StereoConvolver::KernelBuilder::prepare(int blockSize, int kernelLength) {
    blockSize_ = blockSize;
    partitions_ = kernelLength / blockSize;
    fft_.prepare(2 * blockSize);
    work_.assign(static_cast<std::size_t>(2 * blockSize), Complex{});
}

void StereoConvolver::KernelBuilder::build(const double* timeA, const double* timeB, Kernel& out) noexcept {
    const int fftSize = 2 * blockSize_;
    const auto stride = 4u * static_cast<std::size_t>(blockSize_ + 1);
    for (int p = 0; p < partitions_; ++p) {
        for (int n = 0; n < fftSize; ++n) {
            const std::size_t t = static_cast<std::size_t>(p * blockSize_ + n);
            work_[static_cast<std::size_t>(n)] = n < blockSize_ ? Complex{timeA[t], timeB[t]} : Complex{};
        }
        fft_.forward(work_.data());
        splitSpectra(work_.data(), fftSize, out.data.data() + static_cast<std::size_t>(p) * stride);
    }
}

void StereoConvolver::accumulate(const Kernel& k, double* outA, double* outB) noexcept {
    const int bins = blockSize_ + 1;
    const auto b = static_cast<std::size_t>(bins);
    const std::size_t stride = 4u * b;
    std::fill(acc_.begin(), acc_.end(), 0.0);
    double* acc = acc_.data();
    for (int p = 0; p < partitions_; ++p) {
        // FDL[head − p] (spectre d'entrée de la trame k − p) × H[p]
        const int slot = (head_ - p + partitions_) % partitions_;
        const double* x = fdl_.data() + static_cast<std::size_t>(slot) * stride;
        const double* h = k.data.data() + static_cast<std::size_t>(p) * stride;
        multiplyAdd(acc, acc + b, x, x + b, h, h + b, bins);                                     // A
        multiplyAdd(acc + 2 * b, acc + 3 * b, x + 2 * b, x + 3 * b, h + 2 * b, h + 3 * b, bins); // B
    }
    mergeSpectra(acc, fftSize_, work_.data());
    fft_.inverse(work_.data());
    for (int n = 0; n < blockSize_; ++n) {
        outA[n] = work_[static_cast<std::size_t>(blockSize_ + n)].real();
        outB[n] = work_[static_cast<std::size_t>(blockSize_ + n)].imag();
    }
}

void StereoConvolver::processFrame(const Kernel& primary, const Kernel* secondary) noexcept {
    const auto stride = 4u * static_cast<std::size_t>(blockSize_ + 1);
    for (int n = 0; n < fftSize_; ++n)
        work_[static_cast<std::size_t>(n)] = Complex{inputA_[static_cast<std::size_t>(n)], inputB_[static_cast<std::size_t>(n)]};
    fft_.forward(work_.data());
    head_ = (head_ + 1) % partitions_;
    splitSpectra(work_.data(), fftSize_, fdl_.data() + static_cast<std::size_t>(head_) * stride);

    accumulate(primary, outA_.data(), outB_.data());
    if (secondary) accumulate(*secondary, out2A_.data(), out2B_.data());

    // La trame courante devient la trame précédente.
    std::copy(inputA_.begin() + blockSize_, inputA_.end(), inputA_.begin());
    std::copy(inputB_.begin() + blockSize_, inputB_.end(), inputB_.begin());
}

} // namespace omnis::dsp
