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

bool ProdListener::takeAutoResult(analysis::SpectralSignature& out, std::vector<float>* excerpt) {
    if (autoState() != AutoState::Done) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        out = autoResult_;
        if (excerpt) {
            *excerpt = std::move(autoExcerpt_);
            autoExcerpt_.clear();
        }
    }
    autoState_.store(static_cast<int>(AutoState::Idle), std::memory_order_release);
    return true;
}

void ProdListener::loop() {
    std::vector<float> chunk(2 * 1024);
    std::vector<double> l(1024), r(1024);
    auto lastSnapshot = std::chrono::steady_clock::now();
    auto lastAudio = lastSnapshot, lastData = lastSnapshot; // dernier son entendu, dernier bloc reçu (lecture)
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
            lastData = std::chrono::steady_clock::now();
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
                autoSeconds_.store(0.0, std::memory_order_relaxed);
                windowPos_ = windowFilled_ = secondIndex_ = inSecond_ = 0;
                secondAccum_ = 0.0;
                bestEnergy_ = -1.0;
                bestReady_ = false;
                // Le passage le plus fort : deux tampons de kExcerptSeconds, le temps de cette
                // écoute seulement (thread d'analyse, pas le thread audio).
                window_.assign(static_cast<std::size_t>(2.0 * kExcerptSeconds * sampleRate_), 0.0f);
                best_.assign(window_.size(), 0.0f);
                secondEnergy_.assign(static_cast<std::size_t>(kExcerptSeconds), 0.0);
                autoState_.store(static_cast<int>(AutoState::Listening), std::memory_order_release);
            }
            if (autoState() == AutoState::Listening && active) {
                auto_.push(l.data(), r.data(), static_cast<int>(got));
                autoMusic_.push(l.data(), r.data(), static_cast<int>(got));
                keepLoudest(l.data(), r.data(), got);
                autoActive_ += static_cast<double>(got);
                autoSeconds_.store(autoActive_ / sampleRate_, std::memory_order_relaxed);
                autoProgress_.store(std::min(1.0, autoActive_ / (kAutoMinSeconds * sampleRate_)), std::memory_order_relaxed);
                lastAudio = std::chrono::steady_clock::now();
            }
        }
        // AUTO : fin de l'écoute (lecture arrêtée, second clic ou morceau très long).
        if (autoState() == AutoState::Listening) {
            const double heard = autoActive_ / sampleRate_;
            // Arrêt : plus aucun bloc (le processeur n'envoie que pendant la lecture), ou la
            // lecture tourne sur du silence depuis kEndSilenceSeconds (fin du morceau).
            const auto t = std::chrono::steady_clock::now();
            const bool stopped = std::chrono::duration<double>(t - lastData).count() >= kStopGapSeconds ||
                                 std::chrono::duration<double>(t - lastAudio).count() >= kEndSilenceSeconds;
            const bool finish = finishAuto_.load(std::memory_order_acquire);
            if (heard >= kAutoMaxSeconds || (heard >= kAutoMinSeconds && (stopped || finish)) || (finish && heard >= 4.0)) finishListening();
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

void ProdListener::keepLoudest(const double* l, const double* r, std::size_t n) {
    const std::size_t frames = window_.size() / 2, perSecond = static_cast<std::size_t>(sampleRate_);
    if (frames == 0 || perSecond == 0 || secondEnergy_.empty()) return;
    for (std::size_t i = 0; i < n; ++i) {
        window_[2 * windowPos_] = static_cast<float>(l[i]);
        window_[2 * windowPos_ + 1] = static_cast<float>(r[i]);
        windowPos_ = (windowPos_ + 1) % frames;
        windowFilled_ = std::min(frames, windowFilled_ + 1);
        secondAccum_ += l[i] * l[i] + r[i] * r[i];
        if (++inSecond_ < perSecond) continue;
        // Une seconde de plus : énergie de la fenêtre, et copie si c'est la plus forte.
        secondEnergy_[secondIndex_] = secondAccum_;
        secondIndex_ = (secondIndex_ + 1) % secondEnergy_.size();
        secondAccum_ = 0.0;
        inSecond_ = 0;
        double sum = 0.0;
        for (double e : secondEnergy_) sum += e;
        if (windowFilled_ == frames && sum > bestEnergy_) {
            bestEnergy_ = sum;
            // Linéarisé : du plus ancien au plus récent.
            for (std::size_t k = 0; k < frames; ++k) {
                const std::size_t src = (windowPos_ + k) % frames;
                best_[2 * k] = window_[2 * src];
                best_[2 * k + 1] = window_[2 * src + 1];
            }
            bestReady_ = true;
        }
    }
}

void ProdListener::finishListening() {
    auto result = auto_.result();
    result.music = autoMusic_.result();
    std::vector<float> excerpt;
    if (bestReady_) {
        excerpt = std::move(best_);
    } else if (!window_.empty()) {
        // Moins de kExcerptSeconds entendues : tout ce qui est dans la fenêtre, dans l'ordre.
        const std::size_t frames = window_.size() / 2;
        excerpt.resize(2 * windowFilled_);
        const std::size_t start = (windowPos_ + frames - windowFilled_) % frames;
        for (std::size_t k = 0; k < windowFilled_; ++k) {
            const std::size_t src = (start + k) % frames;
            excerpt[2 * k] = window_[2 * src];
            excerpt[2 * k + 1] = window_[2 * src + 1];
        }
    }
    std::vector<float>().swap(window_); // rendus tout de suite
    std::vector<float>().swap(best_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        autoResult_ = result;
        autoExcerpt_ = std::move(excerpt);
    }
    autoState_.store(static_cast<int>(AutoState::Done), std::memory_order_release);
}

} // namespace omnis
