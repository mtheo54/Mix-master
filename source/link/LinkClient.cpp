// =============================================================================
//  Abrasion Labs // OMNIS
//  link/LinkClient.cpp
// =============================================================================
#include "LinkClient.h"

#include <chrono>
#include <vector>

namespace omnis::link {

void LinkClient::start(Role role, double sampleRate) {
    stop();
    role_ = role;
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    quit_.store(false, std::memory_order_relaxed);
    classifier_.prepare(sampleRate_);
    drumBus_.prepare(sampleRate_);
    frames_.prepare(sampleRate_);
    // ~2,7 s de son à 48 kHz (lu toutes les 50 ms). Le thread audio est hors de process().
    dry_.allocate(std::size_t{1} << 18);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        identity_ = {};
    }
    ready_.store(role != Role::None, std::memory_order_release);
    if (role != Role::None) thread_ = std::thread([this] { run(); });
}

void LinkClient::stop() {
    ready_.store(false, std::memory_order_release);
    if (thread_.joinable()) {
        quit_.store(true, std::memory_order_relaxed);
        thread_.join();
    }
    publishEndpoint(nullptr);
    slot_.store(-1, std::memory_order_relaxed);
    role_ = Role::None;
}

void LinkClient::setTrackName(std::string name) {
    std::lock_guard<std::mutex> lock(mutex_);
    trackName_ = std::move(name);
}

std::string LinkClient::trackName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return trackName_;
}

Identity LinkClient::identity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return identity_;
}

void LinkClient::publishEndpoint(std::unique_ptr<Endpoint> next) {
    rtEndpoint_.store(next.get(), std::memory_order_seq_cst);
    // L'ancien n'est détruit qu'une fois le thread audio sorti de son accès.
    while (rtBusy_.load(std::memory_order_seq_cst)) std::this_thread::yield();
    endpoint_ = std::move(next);
}

void LinkClient::run() {
    using namespace std::chrono;
    auto lastPublish = steady_clock::now() - seconds(1), lastJoinTry = steady_clock::now() - seconds(2);
    std::vector<float> interleaved(4096);
    std::vector<double> l(2048), r(2048);
    std::string lastName;
    while (!quit_.load(std::memory_order_relaxed)) {
        const auto now = steady_clock::now();
        if (!endpoint_ && now - lastJoinTry >= seconds(1)) {
            lastJoinTry = now;
            auto e = std::make_unique<Endpoint>();
            if (e->join(role_, trackName(), sampleRate_)) {
                slot_.store(e->slotIndex(), std::memory_order_relaxed);
                publishEndpoint(std::move(e));
                lastPublish = now - seconds(1);
            }
        }
        // Le cœur d'abord : l'écoute qui suit peut être longue (machine chargée).
        if (endpoint_) endpoint_->heartbeat();
        // L'écoute : ce que le thread audio a copié depuis la dernière fois (au plus ~1 s par tour).
        for (std::size_t drained = 0; drained < static_cast<std::size_t>(2.0 * sampleRate_);) {
            const std::size_t got = dry_.pop(interleaved.data(), interleaved.size());
            drained += got;
            if (got < 2) break;
            const int n = static_cast<int>(got / 2);
            for (int i = 0; i < n; ++i) {
                l[static_cast<std::size_t>(i)] = interleaved[static_cast<std::size_t>(2 * i)];
                r[static_cast<std::size_t>(i)] = interleaved[static_cast<std::size_t>(2 * i + 1)];
            }
            classifier_.push(l.data(), r.data(), n);
            if (role_ == Role::Bus) drumBus_.push(l.data(), r.data(), n);
        }
        if (endpoint_) {
            endpoint_->heartbeat();
            const std::string name = trackName();
            if (now - lastPublish >= milliseconds(500) || name != lastName) {
                lastPublish = now;
                lastName = name;
                const stems::Identification id = classifier_.identify(stems::Element::Auto, name);
                Identity out;
                out.element = id.element;
                out.heard = id.heard;
                out.confidence = static_cast<float>(id.confidence);
                out.secondsHeard = static_cast<float>(id.secondsHeard);
                out.nameDisagrees = id.nameDisagrees;
                if (role_ == Role::Bus) {
                    const auto& d = drumBus_.content();
                    out.kick = d.kick.present;
                    out.snare = d.snare.present;
                    out.hat = d.hat.present;
                    out.openHat = d.openHat.present;
                }
                endpoint_->publish(name, out);
                std::lock_guard<std::mutex> lock(mutex_);
                identity_ = out;
            }
        }
        std::this_thread::sleep_for(milliseconds(50));
    }
    // L'emplacement est rendu par stop() (destruction de l'Endpoint), une fois le thread audio sorti.
}

} // namespace omnis::link
