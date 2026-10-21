// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/SubKernelBank.cpp
// =============================================================================
#include "SubKernelBank.h"

#include <chrono>
#include <cmath>

namespace omnis {

namespace {

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    const double h = 0.5 * x;
    for (int k = 1; k < 64; ++k) {
        term *= h / k;
        const double t2 = term * term;
        sum += t2;
        if (t2 < sum * 1e-17) break;
    }
    return sum;
}

double pow4(double x) {
    const double x2 = x * x;
    return x2 * x2;
}

} // namespace

// -----------------------------------------------------------------------------
//  SubKernelRequest
// -----------------------------------------------------------------------------
bool SubKernelRequest::nearlyEquals(const SubKernelRequest& o) const noexcept {
    const auto relClose = [](double a, double b) { return std::fabs(a - b) <= 1e-4 * std::fabs(b); };
    return relClose(lowCutHz, o.lowCutHz) && relClose(monoHz, o.monoHz) && relClose(subHz, o.subHz) &&
           std::fabs(monoAmount - o.monoAmount) < 1e-4 && std::fabs(slopeMix - o.slopeMix) < 1e-4 &&
           std::fabs(subGainDb - o.subGainDb) < 1e-3;
}

double SubKernelRequest::midMagnitude(double f) const noexcept {
    const double wc = f / lowCutHz;
    const double hp = pow4(wc) / std::sqrt(1.0 + pow4(wc) * pow4(wc));
    const double g = std::pow(10.0, subGainDb / 20.0);
    const double lpSub = 1.0 / (1.0 + pow4(f / subHz));
    return hp * (1.0 + (g - 1.0) * lpSub);
}

double SubKernelRequest::sideMagnitude(double f) const noexcept {
    const double wc = f / lowCutHz;
    const double hp = pow4(wc) / std::sqrt(1.0 + pow4(wc) * pow4(wc));
    const double w4 = pow4(f / monoHz);
    const double lp = (1.0 - slopeMix) / (1.0 + w4) + slopeMix / (1.0 + w4 * w4);
    return hp * (1.0 - monoAmount * lp);
}

// -----------------------------------------------------------------------------
//  SubKernelDesigner
// -----------------------------------------------------------------------------
void SubKernelDesigner::prepare(double sampleRate, int kernelLength, int blockSize) {
    sampleRate_ = sampleRate;
    length_ = kernelLength;
    fft_.prepare(2 * kernelLength);
    spectrum_.assign(static_cast<std::size_t>(2 * kernelLength), {});
    window_.resize(static_cast<std::size_t>(kernelLength));
    timeMid_.assign(window_.size(), 0.0);
    timeSide_.assign(window_.size(), 0.0);
    const double i0Beta = besselI0(kKaiserBeta);
    const double half = 0.5 * kernelLength;
    for (int n = 0; n < kernelLength; ++n) {
        const double r = (n - half) / half;
        window_[static_cast<std::size_t>(n)] = besselI0(kKaiserBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0Beta;
    }
    builder_.prepare(blockSize, kernelLength);
}

void SubKernelDesigner::design(const SubKernelRequest& r, dsp::StereoConvolver::Kernel& out) noexcept {
    const int m = 2 * length_;
    // Spectres réels pairs : Z_k = H_M(f_k) + i·H_S(f_k), et Z_{M−k} = Z_k.
    for (int k = 0; k <= length_; ++k) {
        const double f = k * sampleRate_ / m;
        const std::complex<double> z{r.midMagnitude(f), r.sideMagnitude(f)};
        spectrum_[static_cast<std::size_t>(k)] = z;
        if (k > 0 && k < length_) spectrum_[static_cast<std::size_t>(m - k)] = z;
    }
    fft_.inverse(spectrum_.data());
    // Réponse à phase nulle centrée sur N/2, fenêtrée -> FIR causal à phase linéaire.
    const int half = length_ / 2;
    for (int n = 0; n < length_; ++n) {
        const int idx = ((n - half) % m + m) % m;
        const double w = window_[static_cast<std::size_t>(n)];
        timeMid_[static_cast<std::size_t>(n)] = spectrum_[static_cast<std::size_t>(idx)].real() * w;
        timeSide_[static_cast<std::size_t>(n)] = spectrum_[static_cast<std::size_t>(idx)].imag() * w;
    }
    builder_.build(timeMid_.data(), timeSide_.data(), out);
}

// -----------------------------------------------------------------------------
//  SubKernelBank
// -----------------------------------------------------------------------------
void SubKernelBank::prepare(double sampleRate, int kernelLength, int blockSize, bool background,
                            const SubKernelRequest& initial) {
    stop();
    designer_.prepare(sampleRate, kernelLength, blockSize);
    for (auto& slot : slots_) {
        slot.kernel.allocate(kernelLength / blockSize, blockSize);
        slot.state.store(Free, std::memory_order_relaxed);
    }
    designer_.design(initial, slots_[0].kernel);
    slots_[0].request = initial;
    slots_[0].state.store(Active, std::memory_order_release);
    lastRequest_ = initial;
    requests_.writeBuffer() = initial;
    requests_.publish();
    requests_.update();

    background_ = background;
    if (background_) {
        running_.store(true, std::memory_order_release);
        worker_ = std::thread([this] { workerLoop(); });
    }
}

void SubKernelBank::stop() {
    if (worker_.joinable()) {
        running_.store(false, std::memory_order_release);
        worker_.join();
    }
}

SubKernelBank::Slot* SubKernelBank::acquireForWriting() noexcept {
    // Priorité aux emplacements libres, sinon on écrase un noyau Ready périmé
    // (le thread audio ne l'a pas encore pris : le CAS échoue s'il le prend).
    for (int wanted : {static_cast<int>(Free), static_cast<int>(Ready)}) {
        for (auto& slot : slots_) {
            int expected = wanted;
            if (slot.state.compare_exchange_strong(expected, Writing, std::memory_order_acquire)) return &slot;
        }
    }
    return nullptr;
}

void SubKernelBank::request(const SubKernelRequest& r) noexcept {
    if (r.nearlyEquals(lastRequest_)) return;
    lastRequest_ = r;
    if (background_) {
        requests_.writeBuffer() = r;
        requests_.publish();
        return;
    }
    // Synchrone : calcul immédiat (rendu hors ligne, autorisé à prendre du temps).
    if (Slot* slot = acquireForWriting()) {
        designer_.design(r, slot->kernel);
        slot->request = r;
        slot->state.store(Ready, std::memory_order_release);
    }
}

SubKernelBank::Slot* SubKernelBank::takeReady() noexcept {
    for (auto& slot : slots_) {
        int expected = Ready;
        if (slot.state.compare_exchange_strong(expected, Active, std::memory_order_acquire)) return &slot;
    }
    return nullptr;
}

void SubKernelBank::workerLoop() {
    bool pending = false;
    SubKernelRequest current{};
    while (running_.load(std::memory_order_acquire)) {
        if (requests_.update()) {
            current = requests_.readBuffer();
            pending = true;
        }
        if (pending) {
            if (Slot* slot = acquireForWriting()) {
                designer_.design(current, slot->kernel);
                slot->request = current;
                slot->state.store(Ready, std::memory_order_release);
                pending = false;
                continue; // regarde tout de suite si une demande plus récente est arrivée
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

} // namespace omnis
