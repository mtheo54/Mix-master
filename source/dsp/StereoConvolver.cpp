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

/// Sépare Z = FFT(a + i·b) en A et B (bins 0 … F/2).
void splitSpectra(const Complex* z, int fftSize, Complex* a, Complex* b) noexcept {
    const int half = fftSize / 2;
    for (int k = 0; k <= half; ++k) {
        const Complex zk = z[k];
        const Complex zn = std::conj(z[(fftSize - k) % fftSize]);
        a[k] = 0.5 * (zk + zn);
        // (zk − zn) / (2i) = −i·(zk − zn)/2
        const Complex d = zk - zn;
        b[k] = Complex{0.5 * d.imag(), -0.5 * d.real()};
    }
}

/// Reconstruit Z = Y_A + i·Y_B sur F points à partir des demi-spectres.
void mergeSpectra(const Complex* a, const Complex* b, int fftSize, Complex* z) noexcept {
    const int half = fftSize / 2;
    for (int k = 0; k <= half; ++k) z[k] = a[k] + Complex{-b[k].imag(), b[k].real()};
    for (int k = half + 1; k < fftSize; ++k) {
        const Complex ak = std::conj(a[fftSize - k]);
        const Complex bk = std::conj(b[fftSize - k]);
        z[k] = ak + Complex{-bk.imag(), bk.real()};
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
    fdlA_.assign(static_cast<std::size_t>(partitions_) * bins, Complex{});
    fdlB_.assign(fdlA_.size(), Complex{});
    work_.assign(static_cast<std::size_t>(fftSize_), Complex{});
    accA_.assign(bins, Complex{});
    accB_.assign(bins, Complex{});
    outA_.assign(static_cast<std::size_t>(blockSize), 0.0);
    outB_.assign(outA_.size(), 0.0);
    out2A_.assign(outA_.size(), 0.0);
    out2B_.assign(outA_.size(), 0.0);
    head_ = 0;
}

void StereoConvolver::reset() noexcept {
    std::fill(inputA_.begin(), inputA_.end(), 0.0);
    std::fill(inputB_.begin(), inputB_.end(), 0.0);
    std::fill(fdlA_.begin(), fdlA_.end(), Complex{});
    std::fill(fdlB_.begin(), fdlB_.end(), Complex{});
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
    const auto bins = static_cast<std::size_t>(blockSize_ + 1);
    for (int p = 0; p < partitions_; ++p) {
        for (int n = 0; n < fftSize; ++n) {
            const std::size_t t = static_cast<std::size_t>(p * blockSize_ + n);
            work_[static_cast<std::size_t>(n)] = n < blockSize_ ? Complex{timeA[t], timeB[t]} : Complex{};
        }
        fft_.forward(work_.data());
        splitSpectra(work_.data(), fftSize, out.a.data() + static_cast<std::size_t>(p) * bins,
                     out.b.data() + static_cast<std::size_t>(p) * bins);
    }
}

void StereoConvolver::accumulate(const Kernel& k, double* outA, double* outB) noexcept {
    const int bins = blockSize_ + 1;
    std::fill(accA_.begin(), accA_.end(), Complex{});
    std::fill(accB_.begin(), accB_.end(), Complex{});
    for (int p = 0; p < partitions_; ++p) {
        // FDL[head − p] (spectre d'entrée de la trame k − p) × H[p]
        const int slot = (head_ - p + partitions_) % partitions_;
        const Complex* xa = fdlA_.data() + static_cast<std::size_t>(slot) * static_cast<std::size_t>(bins);
        const Complex* xb = fdlB_.data() + static_cast<std::size_t>(slot) * static_cast<std::size_t>(bins);
        const Complex* ha = k.a.data() + static_cast<std::size_t>(p) * static_cast<std::size_t>(bins);
        const Complex* hb = k.b.data() + static_cast<std::size_t>(p) * static_cast<std::size_t>(bins);
        for (int i = 0; i < bins; ++i) {
            accA_[static_cast<std::size_t>(i)] += cmul(xa[i], ha[i]);
            accB_[static_cast<std::size_t>(i)] += cmul(xb[i], hb[i]);
        }
    }
    mergeSpectra(accA_.data(), accB_.data(), fftSize_, work_.data());
    fft_.inverse(work_.data());
    for (int n = 0; n < blockSize_; ++n) {
        outA[n] = work_[static_cast<std::size_t>(blockSize_ + n)].real();
        outB[n] = work_[static_cast<std::size_t>(blockSize_ + n)].imag();
    }
}

void StereoConvolver::processFrame(const Kernel& primary, const Kernel* secondary) noexcept {
    const auto bins = static_cast<std::size_t>(blockSize_ + 1);
    for (int n = 0; n < fftSize_; ++n)
        work_[static_cast<std::size_t>(n)] = Complex{inputA_[static_cast<std::size_t>(n)], inputB_[static_cast<std::size_t>(n)]};
    fft_.forward(work_.data());
    head_ = (head_ + 1) % partitions_;
    splitSpectra(work_.data(), fftSize_, fdlA_.data() + static_cast<std::size_t>(head_) * bins,
                 fdlB_.data() + static_cast<std::size_t>(head_) * bins);

    accumulate(primary, outA_.data(), outB_.data());
    if (secondary) accumulate(*secondary, out2A_.data(), out2B_.data());

    // La trame courante devient la trame précédente.
    std::copy(inputA_.begin() + blockSize_, inputA_.end(), inputA_.begin());
    std::copy(inputB_.begin() + blockSize_, inputB_.end(), inputB_.begin());
}

} // namespace omnis::dsp
