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
    identified_.store(0, std::memory_order_relaxed);
    classifier_.prepare(sampleRate);
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

void LinkClient::setTrackName(std::string name) {
    std::lock_guard<std::mutex> lock(nameMutex_);
    trackName_ = std::move(name);
}

std::string LinkClient::trackName() const {
    std::lock_guard<std::mutex> lock(nameMutex_);
    return trackName_;
}

stems::Identification LinkClient::identification() const {
    std::lock_guard<std::mutex> lock(nameMutex_);
    return identification_;
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
    auto lastTry = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    auto lastIdentify = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    // Ce que l'emplacement affiche (élément et nom) : republié quand ça change.
    StemKind shownKind = StemKind::Auto, lastManual = StemKind::Auto;
    std::string shownName;
    std::uint64_t readFrame = Endpoint::kNoFrame;
    std::vector<double> left, right;
    const auto publishedName = [&](StemKind element) {
        std::string name = trackName();
        return name.empty() ? std::string(stemKindName(element == StemKind::Auto ? StemKind::Other : element)) : name;
    };
    while (!quit_.load(std::memory_order_relaxed)) {
        const int group = group_.load(std::memory_order_relaxed);
        const auto kind = static_cast<StemKind>(kind_.load(std::memory_order_relaxed));
        const auto now = std::chrono::steady_clock::now();
        // Rejoindre (au départ, au changement de groupe, ou après un échec,
        // une fois par seconde : groupe plein, mémoire refusée).
        if ((!endpoint_ || group != joinedGroup) && now - lastTry >= std::chrono::seconds(1)) {
            lastTry = now;
            auto next = std::make_unique<Endpoint>();
            const StemKind element = role_ == Role::Stem ? identifiedElement() : StemKind::Other;
            const std::string name = role_ == Role::Master ? std::string("Master") : publishedName(element);
            if (next->join(group, role_, element, name, sampleRate_)) {
                publish(std::move(next));
                joinedGroup = group;
                shownKind = element;
                shownName = name;
                readFrame = Endpoint::kNoFrame;
            } else if (group != joinedGroup) {
                publish(nullptr);
                joinedGroup = 0;
            }
        }
        if (endpoint_) {
            endpoint_->heartbeat();
            if (role_ == Role::Stem) {
                // S'écouter (pendant la lecture), puis se reconnaître chaque seconde,
                // ou tout de suite quand la personne change l'élément.
                if (endpoint_->readOwnPlayback(readFrame, left, right) > 0)
                    classifier_.push(left.data(), right.data(), static_cast<int>(left.size()));
                if (now - lastIdentify >= std::chrono::seconds(1) || kind != lastManual) {
                    lastIdentify = now;
                    lastManual = kind;
                    stems::Identification id = classifier_.identify(kind, trackName());
                    identified_.store(static_cast<int>(id.element), std::memory_order_relaxed);
                    {
                        std::lock_guard<std::mutex> lock(nameMutex_);
                        identification_ = id;
                    }
                }
                const StemKind element = identifiedElement();
                const std::string name = publishedName(element);
                if (element != shownKind) {
                    endpoint_->setKind(element);
                    shownKind = element;
                }
                if (name != shownName) {
                    endpoint_->setName(name);
                    shownName = name;
                }
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
