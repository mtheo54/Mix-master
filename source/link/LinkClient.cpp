// =============================================================================
//  Abrasion Labs // OMNIS
//  link/LinkClient.cpp
// =============================================================================
#include "LinkClient.h"

#include <chrono>
#include <vector>

namespace omnis::link {

void LinkClient::start(double sampleRate) {
    stop();
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    quit_.store(false, std::memory_order_relaxed);
    listener_.prepare(sampleRate_);
    // ~11 s de son à 48 kHz (lu toutes les 50 ms, bien plus vite que le temps réel) : rien ne se
    // perd même si la machine est chargée un instant. Le thread audio est hors de process().
    dry_.allocate(std::size_t{1} << 20);
    for (auto& s : heardSecond_) s.store(0, std::memory_order_relaxed);
    lastSecond_ = -1;
    enteredClean_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        content_ = {};
    }
    ready_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { run(); });
}

void LinkClient::stop() {
    ready_.store(false, std::memory_order_release);
    if (thread_.joinable()) {
        quit_.store(true, std::memory_order_relaxed);
        thread_.join();
    }
    slot_.store(-1, std::memory_order_relaxed);
}

void LinkClient::setTrackName(std::string name) {
    std::lock_guard<std::mutex> lock(mutex_);
    trackName_ = std::move(name);
}

std::string LinkClient::trackName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return trackName_;
}

stems::BusContent LinkClient::content() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return content_;
}

void LinkClient::run() {
    using namespace std::chrono;
    Endpoint endpoint; // possédé par ce thread seulement : le thread audio n'y touche jamais
    auto lastPublish = steady_clock::now() - seconds(2), lastJoinTry = steady_clock::now() - seconds(2);
    std::uint32_t relistenSeen = 0;
    bool relistenKnown = false;
    std::vector<float> interleaved(4096);
    std::vector<double> l(2048), r(2048);
    std::string lastName;
    while (!quit_.load(std::memory_order_relaxed)) {
        const auto now = steady_clock::now();
        if (!endpoint.joined() && now - lastJoinTry >= seconds(1)) {
            lastJoinTry = now;
            if (endpoint.join(trackName())) {
                slot_.store(endpoint.slotIndex(), std::memory_order_relaxed);
                lastPublish = now - seconds(2);
            }
        }
        // Le cœur d'abord : l'écoute qui suit peut être longue (machine chargée).
        endpoint.heartbeat();
        // Réécouter, demandé par n'importe quelle instance : le thread audio oublie ce qu'il a envoyé,
        // puis on vide la file et on repart de zéro.
        if (endpoint.joined()) {
            const std::uint32_t count = endpoint.relistenCount();
            if (relistenKnown && count != relistenSeen) relisten();
            relistenSeen = count;
            relistenKnown = true;
        }
        // Ici même si la lecture est arrêtée (le thread audio ne tourne pas forcément) : les secondes
        // entendues sont effacées, la file vidée, l'écoute repart de zéro ; le thread audio oubliera
        // la seconde en cours à son prochain bloc.
        if (relistenWanted_.exchange(false, std::memory_order_acq_rel)) {
            for (auto& s : heardSecond_) s.store(0, std::memory_order_relaxed);
            relistenRequested_.store(true, std::memory_order_release);
            while (dry_.pop(interleaved.data(), interleaved.size()) > 0) {
            }
            listener_.reset();
            lastPublish = now - seconds(2);
        }
        // L'écoute : ce que le thread audio a copié depuis la dernière fois (au plus ~4 s par tour).
        for (std::size_t drained = 0; drained < static_cast<std::size_t>(8.0 * sampleRate_);) {
            const std::size_t got = dry_.pop(interleaved.data(), interleaved.size());
            if (got < 2) break;
            drained += got;
            const int n = static_cast<int>(got / 2);
            for (int i = 0; i < n; ++i) {
                l[static_cast<std::size_t>(i)] = interleaved[static_cast<std::size_t>(2 * i)];
                r[static_cast<std::size_t>(i)] = interleaved[static_cast<std::size_t>(2 * i + 1)];
            }
            listener_.push(l.data(), r.data(), n);
        }
        endpoint.heartbeat();
        const std::string name = trackName();
        if (endpoint.joined() && (now - lastPublish >= seconds(1) || name != lastName)) {
            lastPublish = now;
            lastName = name;
            stems::BusContent heard = listener_.content();
            // Ce qui a été écouté du morceau : les secondes entendues, et jusqu'où.
            int count = 0, last = -1;
            for (int s = 0; s < kSongSeconds; ++s)
                if (heardSecond_[static_cast<std::size_t>(s)].load(std::memory_order_relaxed)) {
                    ++count;
                    last = s;
                }
            heard.songSeconds = count;
            heard.songEnd = last + 1;
            endpoint.publish(name, heard);
            std::lock_guard<std::mutex> lock(mutex_);
            content_ = heard;
        }
        std::this_thread::sleep_for(milliseconds(50));
    }
}

} // namespace omnis::link
