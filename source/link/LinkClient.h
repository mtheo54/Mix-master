// =============================================================================
//  Abrasion Labs // OMNIS
//  link/LinkClient.h — Une instance BUS dans la région partagée.
//
//  Thread audio (process) : le son passe tel quel ; pendant la lecture, une
//  copie part vers le thread du lien (file sans attente : perdue plutôt
//  qu'attendre).
//  Thread du lien (toutes les 50 ms) : rejoint la région (et réessaie), bat le
//  cœur, écoute le bus (stems/BusListener, au plus ~1 s de son par tour) et,
//  toutes les secondes, publie le nom du bus et ce qu'il y entend.
// =============================================================================
#pragma once

#include "LinkBus.h"

#include "core/SpscRingBuffer.h"
#include "stems/BusListener.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace omnis::link {

class LinkClient {
public:
    LinkClient() = default;
    ~LinkClient() { stop(); }
    LinkClient(const LinkClient&) = delete;
    LinkClient& operator=(const LinkClient&) = delete;

    /// Non temps réel (activation, changement de rôle ; thread audio hors de process()).
    void start(double sampleRate);
    void stop();
    [[nodiscard]] bool running() const noexcept { return thread_.joinable(); }

    /// Non temps réel : nom du bus dans l'hôte (VST3 IInfoListener).
    void setTrackName(std::string name);
    [[nodiscard]] std::string trackName() const;
    /// Dernière écoute publiée (copie).
    [[nodiscard]] stems::BusContent content() const;
    /// Emplacement pris dans la région (−1 : pas encore).
    [[nodiscard]] int slot() const noexcept { return slot_.load(std::memory_order_relaxed); }

    /// Temps réel, thread audio seulement : le son du bus pendant la lecture.
    template <typename Sample>
    void process(const Sample* left, const Sample* right, int numFrames, bool playing) noexcept {
        if (!playing || !ready_.load(std::memory_order_acquire)) return;
        std::array<float, 512> chunk;
        for (int i = 0; i < numFrames;) {
            const int n = std::min(numFrames - i, static_cast<int>(chunk.size() / 2));
            for (int k = 0; k < n; ++k) {
                chunk[static_cast<std::size_t>(2 * k)] = static_cast<float>(left[i + k]);
                chunk[static_cast<std::size_t>(2 * k + 1)] = static_cast<float>(right[i + k]);
            }
            // Jamais une trame coupée en deux : tout le morceau, ou rien.
            if (dry_.freeSpace() >= static_cast<std::size_t>(2 * n)) dry_.push(chunk.data(), static_cast<std::size_t>(2 * n));
            i += n;
        }
    }

private:
    void run();

    double sampleRate_ = 48000.0;
    std::atomic<bool> quit_{false}, ready_{false};
    std::atomic<int> slot_{-1};
    std::thread thread_;
    SpscRingBuffer<float> dry_;
    stems::BusListener listener_; ///< thread du lien seulement

    mutable std::mutex mutex_;
    std::string trackName_;
    stems::BusContent content_;
};

} // namespace omnis::link
