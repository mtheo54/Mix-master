// =============================================================================
//  Abrasion Labs // OMNIS
//  link/LinkBus.h — Les instances d'un projet se voient (pistes, bus, master).
//
//  Une instance OMNIS en rôle PISTE (sur une piste du mixer : kick, lead,
//  reverse cymbal…) ou BUS (sur un bus : DRUMS, MÉLO, EFX…) ne touche pas au
//  son : elle publie dans une région de mémoire partagée (link/SharedRegion.h)
//      - qui elle est : le nom de sa piste dans le DAW, l'élément qu'elle
//        a reconnu à l'écoute (stems/ElementClassifier) ;
//      - une « empreinte » de ce qu'elle entend : l'énergie de six bandes par
//        trame de kFrameHop échantillons, repérée par sa position dans le
//        projet (toutes les instances découpent le temps de la même façon).
//  Un bus reçoit la somme de ses pistes : en comparant les empreintes, on
//  retrouve quelles pistes y entrent (stems/Composition). Le DAW ne le dit pas
//  aux plug-ins (VST3 ne donne que le nom de la piste où l'on est).
//
//  Temps réel : le thread audio écrit ses trames sans verrou ni appel système
//  (atomiques sans verrou, chaque trame marquée de son numéro après les
//  valeurs). Rejoindre, battre le cœur, se nommer : thread à part. Un
//  emplacement dont le cœur ne bat plus depuis kStaleMs est repris.
//  Disposition fixe, versionnée dans le nom de la région : deux versions
//  d'OMNIS ne lisent jamais la même mémoire.
// =============================================================================
#pragma once

#include "SharedRegion.h"

#include "core/Platform.h"
#include "stems/Element.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace omnis::link {

inline constexpr std::uint32_t kLayoutVersion = 3; ///< 3 : pistes / bus, empreintes par bandes (plus d'audio ni d'ordres)
inline constexpr int kSlots = 64;
inline constexpr int kNameLength = 48;
inline constexpr int kBands = 6;          ///< 20–60, 60–150, 150–400, 400–2000, 2000–6000, 6000–20000 Hz
inline constexpr int kFrameHop = 2048;    ///< échantillons par trame (~43 ms à 48 kHz)
inline constexpr int kFrameRing = 1024;   ///< trames gardées (~44 s à 48 kHz)
inline constexpr std::uint64_t kStaleMs = 3000;
inline constexpr std::int64_t kNoPosition = std::numeric_limits<std::int64_t>::min();

enum class Role : std::uint32_t { None = 0, Track = 1, Bus = 2 };

static_assert(std::atomic<float>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free &&
                  std::atomic<std::int64_t>::is_always_lock_free && std::atomic<std::uint32_t>::is_always_lock_free,
              "la mémoire partagée exige des atomiques sans verrou");

/// Ce qu'une instance a reconnu d'elle-même (thread du lien).
struct Identity {
    stems::Element element = stems::Element::Auto; ///< résultat (écoute + nom)
    stems::Element heard = stems::Element::Auto;   ///< l'écoute seule
    float confidence = 0.0f;
    float secondsHeard = 0.0f;
    bool nameDisagrees = false;
    /// Bus : ce que l'écoute du bus entend dedans (stems/DrumBus) : kick, snare / clap, hi-hat, open hat.
    bool kick = false, snare = false, hat = false, openHat = false;
};

/// Ce qu'on voit d'une instance.
struct InstanceView {
    int slot = -1;
    std::uint64_t owner = 0;
    Role role = Role::None;
    std::string name;
    double sampleRate = 0.0;
    Identity identity;
};

/// Une trame d'empreinte : numéro (position dans le projet / kFrameHop) et énergie par bande.
struct BandFrame {
    std::int64_t index = 0;
    std::array<float, kBands> energy{};
};

// ---- Disposition de la mémoire partagée (une mémoire neuve est à zéro : tout libre)
struct alignas(kCacheLineSize) Slot {
    std::atomic<std::uint64_t> owner; ///< jeton de l'instance, 0 = libre
    std::atomic<std::uint64_t> heartbeatMs;
    std::atomic<std::uint32_t> role;
    std::atomic<std::uint32_t> sampleRate;
    std::atomic<std::uint32_t> infoSeq; ///< impair : écriture du nom / de l'identité en cours
    std::atomic<char> name[kNameLength];
    std::atomic<std::uint32_t> element, heard, flags;
    std::atomic<float> confidence, secondsHeard;

    /// Trame k : tag = k + 1 (0 = vide), écrit APRÈS les énergies.
    alignas(kCacheLineSize) std::atomic<std::int64_t> frameTag[kFrameRing];
    std::atomic<float> frameEnergy[kFrameRing][kBands];
};

struct Header {
    std::atomic<std::uint32_t> magic;
    std::atomic<std::uint32_t> version;
    Slot slots[kSlots];
};

/// Nom de la région. OMNIS_LINK_NAMESPACE le préfixe (tests : ne jamais croiser une vraie session).
std::string regionName();
std::uint64_t nowMs() noexcept;
bool isLive(const Slot& slot, std::uint64_t now) noexcept;

// -----------------------------------------------------------------------------
//  Une instance (piste ou bus) dans la région
// -----------------------------------------------------------------------------
class Endpoint {
public:
    Endpoint() = default;
    ~Endpoint() { leave(); }
    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;

    /// Non temps réel : ouvre la région et prend un emplacement.
    bool join(Role role, std::string_view name, double sampleRate);
    void leave() noexcept;
    [[nodiscard]] bool joined() const noexcept { return slot_ != nullptr; }
    [[nodiscard]] int slotIndex() const noexcept { return slotIndex_; }
    [[nodiscard]] std::uint64_t token() const noexcept { return token_; }

    /// Non temps réel, au moins toutes les secondes.
    void heartbeat() noexcept;
    void publish(std::string_view name, const Identity& identity) noexcept;

    /// Temps réel (thread audio) : une trame d'empreinte complète.
    void writeFrame(std::int64_t index, const std::array<float, kBands>& energy) noexcept {
        Slot* s = slot_;
        if (!s || index < 0) return;
        const auto r = static_cast<std::size_t>(index % kFrameRing);
        s->frameTag[r].store(0, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        for (int b = 0; b < kBands; ++b) s->frameEnergy[r][b].store(energy[static_cast<std::size_t>(b)], std::memory_order_relaxed);
        s->frameTag[r].store(index + 1, std::memory_order_release);
    }

private:
    SharedRegion region_;
    Slot* slot_ = nullptr;
    int slotIndex_ = -1;
    std::uint64_t token_ = 0;
};

// -----------------------------------------------------------------------------
//  Lire les autres (contrôleur, tests) : ne prend aucun emplacement
// -----------------------------------------------------------------------------
class Observer {
public:
    /// Non temps réel. false : aucune instance n'a encore créé la région.
    bool open();
    [[nodiscard]] bool isOpen() const noexcept { return header_ != nullptr; }
    void close() noexcept;
    /// Instances vivantes (pistes et bus).
    [[nodiscard]] std::vector<InstanceView> instances() const;
    /// Les trames d'une instance (toutes celles de l'anneau, cohérentes), triées.
    [[nodiscard]] std::vector<BandFrame> frames(int slot, std::uint64_t owner) const;

private:
    SharedRegion region_;
    const Header* header_ = nullptr;
};

} // namespace omnis::link
