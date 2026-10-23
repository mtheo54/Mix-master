// =============================================================================
//  Abrasion Labs // OMNIS
//  link/LinkClient.h — Une instance du plug-in dans son groupe.
//
//  Le thread du lien (non temps réel, toutes les 50 ms) :
//    - rejoint le groupe, et le change si la personne choisit un autre groupe ;
//    - bat le cœur de l'instance ;
//    - master : écoute les stems (link::StemListener) et compte les vivants ;
//    - stem : s'écoute lui-même (stems::ElementClassifier) et, avec le nom de la
//      piste donné par l'hôte, reconnaît son élément ; le publie dans son
//      emplacement (le master sait qui est qui). Il écoute le son SEC de la
//      piste (avant sa propre chaîne, pushListening) : ce que la chaîne fait
//      ne change pas ce qui est reconnu ;
//    - stem : publie l'élément et son caractère mesuré au thread audio
//      (rtListening, triple tampon) : la chaîne du stem s'y règle.
//  Le thread audio n'a qu'un accès sans attente à l'Endpoint courant
//  (RtAccess) : un changement de groupe remplace l'Endpoint sans jamais le
//  détruire sous les pieds du thread audio.
// =============================================================================
#pragma once

#include "LinkBus.h"

#include "core/SpscRingBuffer.h"
#include "core/TripleBuffer.h"
#include "stems/ElementClassifier.h"

#include <algorithm>
#include <array>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
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
    /// Stem : l'élément choisi par la personne (Auto : le reconnaître).
    void setKind(StemKind kind) noexcept { kind_.store(static_cast<std::uint32_t>(kind), std::memory_order_relaxed); }
    /// Non temps réel : nom de la piste dans l'hôte (VST3 IInfoListener).
    void setTrackName(std::string name);
    [[nodiscard]] std::string trackName() const;
    /// Stem : élément reconnu (Auto tant que rien n'est reconnu), sans attente.
    [[nodiscard]] StemKind identifiedElement() const noexcept {
        return static_cast<StemKind>(identified_.load(std::memory_order_relaxed));
    }
    /// Stem : dernière reconnaissance complète (copie).
    [[nodiscard]] stems::Identification identification() const;

    /// Ce que le thread audio d'un stem reçoit pour régler sa chaîne.
    struct Listening {
        stems::Element element = stems::Element::Auto;
        stems::Character character{};
        double secondsHeard = 0.0;
    };
    /// Temps réel, thread audio seulement (un seul lecteur) : dernière écoute publiée.
    const Listening& rtListening() noexcept {
        listening_.update();
        return listening_.readBuffer();
    }
    /// Temps réel : le son sec de la piste pendant la lecture (pour la
    /// reconnaissance). Sans attente ; perdu si le thread du lien est en retard.
    template <typename Sample>
    void pushListening(const Sample* left, const Sample* right, int numFrames) noexcept {
        if (!listenReady_.load(std::memory_order_acquire)) return;
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

    // Stem : reconnaissance de l'élément.
    stems::ElementClassifier classifier_;
    std::atomic<int> identified_{0};
    mutable std::mutex nameMutex_;
    std::string trackName_;
    stems::Identification identification_;
    TripleBuffer<Listening> listening_;
    SpscRingBuffer<float> dry_;               ///< son sec, entrelacé
    std::atomic<bool> listenReady_{false};
    mutable std::mutex heardMutex_;
    std::vector<StemListener::Heard> heard_;
};

} // namespace omnis::link
