// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/ProdListener.cpp
// =============================================================================
#include "ProdListener.h"

#include <chrono>
#include <cmath>

namespace omnis {

namespace {
constexpr double kSilencePower = 1.0e-6; // −60 dBFS RMS : plus bas, c'est du silence
}

void ProdListener::prepare(double sampleRate) {
    release();
    if (sampleRate != sampleRate_) {
        sampleRate_ = sampleRate;
        all_.prepare(sampleRate);
        auto_.prepare(sampleRate);
        allMusic_.prepare(sampleRate);
        autoMusic_.prepare(sampleRate);
        activeSamples_ = 0.0;
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = {};
    }
    ring_.allocate(static_cast<std::size_t>(2.0 * 8.0 * sampleRate));
    running_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { loop(); });
}

void ProdListener::release() {
    if (worker_.joinable()) {
        running_.store(false, std::memory_order_release);
        worker_.join();
    }
}

ProdListener::Snapshot ProdListener::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

bool ProdListener::takeAutoResult(analysis::SpectralSignature& out) {
    if (autoState() != AutoState::Done) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        out = autoResult_;
    }
    autoState_.store(static_cast<int>(AutoState::Idle), std::memory_order_release);
    return true;
}

void ProdListener::loop() {
    std::vector<float> chunk(2 * 1024);
    std::vector<double> l(1024), r(1024);
    auto lastSnapshot = std::chrono::steady_clock::now();
    bool changed = false;
    while (running_.load(std::memory_order_acquire)) {
        if (forget_.exchange(false)) {
            all_.reset();
            allMusic_.reset();
            activeSamples_ = 0.0;
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_ = {};
        }
        const std::size_t got = ring_.pop(chunk.data(), chunk.size()) / 2;
        if (got > 0) {
            double power = 0.0;
            for (std::size_t i = 0; i < got; ++i) {
                l[i] = chunk[2 * i];
                r[i] = chunk[2 * i + 1];
                power += 0.5 * (l[i] * l[i] + r[i] * r[i]);
            }
            const bool active = power / static_cast<double>(got) > kSilencePower;
            if (active) {
                all_.push(l.data(), r.data(), static_cast<int>(got));
                allMusic_.push(l.data(), r.data(), static_cast<int>(got));
                activeSamples_ += static_cast<double>(got);
                changed = true;
            }
            // AUTO : le passage que la personne joue maintenant.
            const auto state = autoState();
            if (state == AutoState::Waiting && active) {
                auto_.reset();
                autoMusic_.reset();
                autoActive_ = 0.0;
                autoState_.store(static_cast<int>(AutoState::Listening), std::memory_order_release);
            }
            if (autoState() == AutoState::Listening && active) {
                auto_.push(l.data(), r.data(), static_cast<int>(got));
                autoMusic_.push(l.data(), r.data(), static_cast<int>(got));
                autoActive_ += static_cast<double>(got);
                autoProgress_.store(std::min(1.0, autoActive_ / (kAutoSeconds * sampleRate_)), std::memory_order_relaxed);
                if (autoActive_ >= kAutoSeconds * sampleRate_) {
                    auto result = auto_.result();
                    result.music = autoMusic_.result();
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        autoResult_ = result;
                    }
                    autoState_.store(static_cast<int>(AutoState::Done), std::memory_order_release);
                }
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (changed && now - lastSnapshot >= std::chrono::seconds(2)) {
            lastSnapshot = now;
            changed = false;
            Snapshot s{all_.result(), activeSamples_ / sampleRate_};
            s.signature.music = allMusic_.result();
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_ = std::move(s);
        }
        if (got == 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace omnis
