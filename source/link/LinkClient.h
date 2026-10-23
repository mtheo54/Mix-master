// =============================================================================
//  Abrasion Labs // OMNIS
//  link/LinkClient.h — Une instance du plug-in dans son groupe.
//
//  Le thread du lien (non temps réel, toutes les 50 ms) :
//    - rejoint le groupe, et le change si la personne choisit un autre groupe ;
//    - bat le cœur de l'instance ;
//    - master : écoute les stems (link::StemListener) et compte les vivants.
//  Le thread audio n'a qu'un accès sans attente à l'Endpoint courant
//  (RtAccess) : un changement de groupe remplace l'Endpoint sans jamais le
//  détruire sous les pieds du thread audio.
// =============================================================================
#pragma once

#include "LinkBus.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace omnis::link {

class LinkClient {
public:
    LinkClient() = default;
    ~LinkClient() { stop(); }
    LinkClient(const LinkClient&) = delete;
    LinkClient& operator=(const LinkClient&) = delete;

    /// Non temps réel (activation du plug-in). Rôle figé jusqu'au stop() : un
    /// stem n'a pas la latence d'un master, l'hôte doit réactiver l'instance.
    void start(Role role, StemKind kind, int group, double sampleRate);
    void stop();
    [[nodiscard]] bool running() const noexcept { return thread_.joinable(); }
    [[nodiscard]] Role role() const noexcept { return role_; }

    /// Temps réel (simples écritures atomiques), pris en compte par le thread.
    void setKind(StemKind kind) noexcept { kind_.store(static_cast<std::uint32_t>(kind), std::memory_order_relaxed); }
    void setGroup(int group) noexcept { group_.store(group, std::memory_order_relaxed); }

    /// Master : stems vivants du groupe (mis à jour toutes les 50 ms).
    [[nodiscard]] int linkedStems() const noexcept { return linkedStems_.load(std::memory_order_relaxed); }
    /// Master : ce que le master a entendu de chaque stem (copie).
    [[nodiscard]] std::vector<StemListener::Heard> heard() const;

    /// Accès du thread audio, sans attente : Endpoint courant (nullptr hors
    /// groupe) valable jusqu'à la destruction de l'objet RtAccess.
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
    void publish(std::unique_ptr<Endpoint> next); ///< remplace l'Endpoint vu par le thread audio

    Role role_ = Role::None;
    double sampleRate_ = 48000.0;
    std::atomic<std::uint32_t> kind_{0};
    std::atomic<int> group_{1};
    std::atomic<bool> quit_{false};
    std::atomic<int> linkedStems_{0};
    std::thread thread_;

    std::unique_ptr<Endpoint> endpoint_; ///< possédé par le thread du lien
    std::atomic<Endpoint*> rtEndpoint_{nullptr};
    std::atomic<bool> rtBusy_{false};

    StemListener listener_;
    mutable std::mutex heardMutex_;
    std::vector<StemListener::Heard> heard_;
};

} // namespace omnis::link
