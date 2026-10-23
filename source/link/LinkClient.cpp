// =============================================================================
//  Abrasion Labs // OMNIS
//  link/LinkClient.cpp
// =============================================================================
#include "LinkClient.h"

#include <chrono>

namespace omnis::link {

void LinkClient::start(Role role, StemKind kind, int group, double sampleRate) {
    stop();
    if (role == Role::None) return;
    role_ = role;
    sampleRate_ = sampleRate;
    setKind(kind);
    setGroup(group);
    quit_.store(false, std::memory_order_relaxed);
    linkedStems_.store(0, std::memory_order_relaxed);
    thread_ = std::thread([this] { run(); });
}

void LinkClient::stop() {
    if (thread_.joinable()) {
        quit_.store(true, std::memory_order_relaxed);
        thread_.join();
    }
    publish(nullptr);
    role_ = Role::None;
    linkedStems_.store(0, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(heardMutex_);
    heard_.clear();
}

std::vector<StemListener::Heard> LinkClient::heard() const {
    std::lock_guard<std::mutex> lock(heardMutex_);
    return heard_;
}

void LinkClient::publish(std::unique_ptr<Endpoint> next) {
    rtEndpoint_.store(next.get(), std::memory_order_seq_cst);
    // Le thread audio a pu prendre l'ancien juste avant : on attend qu'il
    // l'ait rendu (un bloc audio, quelques millisecondes au pire).
    while (rtBusy_.load(std::memory_order_seq_cst)) std::this_thread::sleep_for(std::chrono::microseconds(200));
    endpoint_ = std::move(next); // l'ancien quitte son emplacement ici
}

void LinkClient::run() {
    int joinedGroup = 0;
    auto namedKind = static_cast<StemKind>(kind_.load(std::memory_order_relaxed));
    auto lastTry = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    while (!quit_.load(std::memory_order_relaxed)) {
        const int group = group_.load(std::memory_order_relaxed);
        const auto kind = static_cast<StemKind>(kind_.load(std::memory_order_relaxed));
        const auto now = std::chrono::steady_clock::now();
        // Rejoindre (au départ, au changement de groupe, ou après un échec,
        // une fois par seconde : groupe plein, mémoire refusée).
        if ((!endpoint_ || group != joinedGroup) && now - lastTry >= std::chrono::seconds(1)) {
            lastTry = now;
            auto next = std::make_unique<Endpoint>();
            const char* name = role_ == Role::Master ? "Master" : stemKindName(kind);
            if (next->join(group, role_, kind, name, sampleRate_)) {
                publish(std::move(next));
                joinedGroup = group;
                namedKind = kind;
            } else if (group != joinedGroup) {
                publish(nullptr);
                joinedGroup = 0;
            }
        }
        if (endpoint_) {
            endpoint_->heartbeat();
            if (role_ == Role::Stem && kind != namedKind) {
                endpoint_->setKind(kind);
                endpoint_->setName(stemKindName(kind));
                namedKind = kind;
            } else if (role_ == Role::Master) {
                listener_.poll(*endpoint_);
                linkedStems_.store(static_cast<int>(listener_.heard().size()), std::memory_order_relaxed);
                std::lock_guard<std::mutex> lock(heardMutex_);
                heard_ = listener_.heard();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace omnis::link
