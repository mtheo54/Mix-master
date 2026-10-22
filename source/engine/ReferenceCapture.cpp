// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/ReferenceCapture.cpp
// =============================================================================
#include "ReferenceCapture.h"

#include <chrono>
#include <cmath>

namespace omnis {

namespace {

constexpr std::size_t kChunk = 4096;
/// Sous −70 dBFS RMS, un bloc de référence compte comme du silence.
constexpr double kSilencePower = 1.0e-7;

} // namespace

void ReferenceCapture::prepare(double sampleRate, double ringSeconds) {
    release();
    sampleRate_ = sampleRate;
    const auto capacity = static_cast<std::size_t>(ringSeconds * sampleRate);
    refRing_.allocate(capacity);
    prodRing_.allocate(capacity);
    refAcc_.prepare(sampleRate);
    prodAcc_.prepare(sampleRate);
    chunkRef_.resize(kChunk);
    chunkProd_.resize(kChunk);
    for (auto* v : {&l_, &r_, &pl_, &pr_}) v->resize(kChunk);
    pending_.store(0, std::memory_order_relaxed);
    running_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { workerLoop(); });
}

void ReferenceCapture::release() {
    if (worker_.joinable()) {
        running_.store(false, std::memory_order_release);
        worker_.join();
    }
}

bool ReferenceCapture::lastResult(Result& out) const {
    std::lock_guard<std::mutex> lock(resultMutex_);
    if (!hasResult_) return false;
    out = result_;
    return true;
}

std::size_t ReferenceCapture::drain(bool& sawSignal) {
    const std::size_t n = std::min(refRing_.pop(chunkRef_.data(), kChunk), kChunk);
    if (n == 0) return 0;
    // Les deux files sont remplies ensemble : la prod a au moins autant d'échantillons.
    const std::size_t m = prodRing_.pop(chunkProd_.data(), n);
    double power = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        l_[i] = chunkRef_[i].l;
        r_[i] = chunkRef_[i].r;
        power += 0.5 * (l_[i] * l_[i] + r_[i] * r_[i]);
        pl_[i] = i < m ? chunkProd_[i].l : 0.0;
        pr_[i] = i < m ? chunkProd_[i].r : 0.0;
    }
    refAcc_.push(l_.data(), r_.data(), static_cast<int>(n));
    prodAcc_.push(pl_.data(), pr_.data(), static_cast<int>(n));
    if (power / static_cast<double>(n) > kSilencePower) {
        activeSamples_ += static_cast<double>(n);
        sawSignal = true;
    }
    pending_.fetch_sub(n, std::memory_order_relaxed);
    return n;
}

void ReferenceCapture::workerLoop() {
    bool wasCapturing = false;
    while (running_.load(std::memory_order_acquire)) {
        const bool now = capturing_.load(std::memory_order_acquire);
        if (now && !wasCapturing) {
            refAcc_.reset();
            prodAcc_.reset();
            activeSamples_ = 0.0;
            refSeconds_.store(0.0, std::memory_order_relaxed);
            refLufs_.store(-70.0, std::memory_order_relaxed);
        }
        bool sawSignal = false;
        std::size_t total = 0;
        while (const std::size_t n = drain(sawSignal)) total += n;
        if (total > 0) {
            refSeconds_.store(activeSamples_ / sampleRate_, std::memory_order_relaxed);
            const double lufs = refAcc_.integratedLufs();
            if (std::isfinite(lufs)) refLufs_.store(lufs, std::memory_order_relaxed);
        }
        if (!now && wasCapturing) {
            // Fin de l'écoute : ce qui restait dans les files a été vidé ci-dessus.
            Result r;
            r.reference = refAcc_.result();
            r.prod = prodAcc_.result();
            r.referenceSeconds = activeSamples_ / sampleRate_;
            {
                std::lock_guard<std::mutex> lock(resultMutex_);
                result_ = r;
                hasResult_ = true;
            }
            if (handler_) handler_(r);
        }
        wasCapturing = now;
        if (total == 0) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace omnis
