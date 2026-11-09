// =============================================================================
//  Abrasion Labs // OMNIS
//  link/LinkClient.h — Une instance PISTE ou BUS dans la région partagée.
//
//  Thread audio (process) : le son passe tel quel ; on en tire l'empreinte
//  (link/BandFrames, écrite dans l'emplacement, sans verrou) et une copie du
//  son pendant la lecture pour l'écoute (file sans attente).
//  Thread du lien (toutes les 50 ms) : rejoint la région (et réessaie), bat le
//  cœur, écoute (stems/ElementClassifier ; un bus aussi stems/DrumBus) et,
//  toutes les ~500 ms, publie le nom de la piste et ce qu'il a reconnu.
//  Le thread audio n'a qu'un accès sans attente à l'Endpoint (RtAccess) :
//  il n'est jamais détruit sous ses pieds.
// =============================================================================
#pragma once

#include "BandFrames.h"
#include "LinkBus.h"

#include "core/SpscRingBuffer.h"
#include "stems/DrumBus.h"
#include "stems/ElementClassifier.h"

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
    void start(Role role, double sampleRate);
    void stop();
    [[nodiscard]] bool running() const noexcept { return thread_.joinable(); }
    [[nodiscard]] Role role() const noexcept { return role_; }

    /// Non temps réel : nom de la piste dans l'hôte (VST3 IInfoListener).
    void setTrackName(std::string name);
    [[nodiscard]] std::string trackName() const;
    /// Dernière reconnaissance publiée (copie).
    [[nodiscard]] Identity identity() const;
    /// Emplacement pris dans la région (−1 : pas encore).
    [[nodiscard]] int slot() const noexcept { return slot_.load(std::memory_order_relaxed); }

    /// Temps réel, thread audio seulement. `projectStart` : position de la première
    /// trame dans le projet (kNoPosition sans lecture).
    template <typename Sample>
    void process(const Sample* left, const Sample* right, int numFrames, std::int64_t projectStart) noexcept {
        if (!ready_.load(std::memory_order_acquire)) return;
        {
            RtAccess access(*this);
            Endpoint* e = access.endpoint();
            frames_.process(left, right, numFrames, projectStart, [e](std::int64_t k, const std::array<float, kBands>& energy) {
                if (e) e->writeFrame(k, energy);
            });
        }
        if (projectStart == kNoPosition) return;
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

    /// Accès du thread audio, sans attente : Endpoint courant (nullptr hors région).
    class RtAccess {
    public:
        explicit RtAccess(LinkClient& c) noexcept : c_(c) {
            c_.rtBusy_.store(true, std::memory_order_seq_cst);
            e_ = c_.rtEndpoint_.load(std::memory_order_seq_cst);
        }
        ~RtAccess() { c_.rtBusy_.store(false, std::memory_order_release); }
        RtAccess(const RtAccess&) = delete;
        RtAccess& operator=(const RtAccess&) = delete;
        [[nodiscard]] Endpoint* endpoint() const noexcept { return e_; }

    private:
        LinkClient& c_;
        Endpoint* e_ = nullptr;
    };

private:
    void run();
    void publishEndpoint(std::unique_ptr<Endpoint> next);

    Role role_ = Role::None;
    double sampleRate_ = 48000.0;
    std::atomic<bool> quit_{false}, ready_{false};
    std::atomic<int> slot_{-1};
    std::thread thread_;

    std::unique_ptr<Endpoint> endpoint_; ///< possédé par le thread du lien
    std::atomic<Endpoint*> rtEndpoint_{nullptr};
    std::atomic<bool> rtBusy_{false};

    BandFrames frames_; ///< thread audio seulement
    SpscRingBuffer<float> dry_;
    stems::ElementClassifier classifier_;
    stems::DrumBusAnalyzer drumBus_;

    mutable std::mutex mutex_;
    std::string trackName_;
    Identity identity_;
};

} // namespace omnis::link
