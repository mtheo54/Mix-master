// =============================================================================
//  Abrasion Labs // OMNIS
//  link/LinkBus.h — Les bus d'un projet se voient.
//
//  Une instance OMNIS en rôle BUS (sur un bus du mixer : DRUMS, MÉLO, EFX…) ne
//  touche pas au son : elle écoute le bus (stems/BusListener) et publie dans
//  une région de mémoire partagée (link/SharedRegion.h) le nom du bus dans le
//  DAW et ce qu'elle y entend (kick, 808, nappe, reverse cymbal…). Toutes les
//  instances (le master comme les autres bus) la lisent : page COMPOSITION.
//  Rien à poser sur les pistes : le DAW ne dit pas aux plug-ins ce qui entre
//  dans un bus, OMNIS l'entend.
//
//  Rejoindre, battre le cœur, publier : thread du lien (jamais le thread
//  audio). Un emplacement dont le cœur ne bat plus depuis kStaleMs est repris.
//  Disposition fixe, versionnée dans le nom de la région : deux versions
//  d'OMNIS ne lisent jamais la même mémoire.
// =============================================================================
#pragma once

#include "SharedRegion.h"

#include "core/Platform.h"
#include "stems/BusListener.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace omnis::link {

inline constexpr std::uint32_t kLayoutVersion = 4; ///< 4 : bus seulement, ce qu'ils contiennent (plus de pistes ni d'empreintes)
inline constexpr int kSlots = 64;
inline constexpr int kNameLength = 48;
inline constexpr int kMaxItems = 16;
inline constexpr std::uint64_t kStaleMs = 3000;

enum class Role : std::uint32_t { None = 0, Bus = 2 };

static_assert(std::atomic<float>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free &&
                  std::atomic<std::uint32_t>::is_always_lock_free,
              "la mémoire partagée exige des atomiques sans verrou");

/// Ce qu'on voit d'un bus.
struct InstanceView {
    int slot = -1;
    std::uint64_t owner = 0;
    std::string name;          ///< nom du bus dans le DAW
    stems::BusContent content; ///< ce qu'OMNIS y entend
};

// ---- Disposition de la mémoire partagée (une mémoire neuve est à zéro : tout libre)
struct alignas(kCacheLineSize) Slot {
    std::atomic<std::uint64_t> owner; ///< jeton de l'instance, 0 = libre
    std::atomic<std::uint64_t> heartbeatMs;
    std::atomic<std::uint32_t> role;
    std::atomic<std::uint32_t> infoSeq; ///< impair : écriture en cours
    std::atomic<char> name[kNameLength];
    std::atomic<float> secondsHeard;
    std::atomic<std::uint32_t> itemCount;
    struct Item {
        std::atomic<std::uint32_t> element, count;
        std::atomic<float> confidence, seconds;
    } items[kMaxItems];
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
//  Un bus dans la région (thread du lien seulement)
// -----------------------------------------------------------------------------
class Endpoint {
public:
    Endpoint() = default;
    ~Endpoint() { leave(); }
    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;

    bool join(std::string_view name);
    void leave() noexcept;
    [[nodiscard]] bool joined() const noexcept { return slot_ != nullptr; }
    [[nodiscard]] int slotIndex() const noexcept { return slotIndex_; }
    /// Au moins toutes les secondes.
    void heartbeat() noexcept;
    void publish(std::string_view name, const stems::BusContent& content) noexcept;

private:
    SharedRegion region_;
    Slot* slot_ = nullptr;
    int slotIndex_ = -1;
    std::uint64_t token_ = 0;
};

// -----------------------------------------------------------------------------
//  Lire les bus (contrôleur de chaque instance, tests) : ne prend aucun emplacement
// -----------------------------------------------------------------------------
class Observer {
public:
    /// false : aucune instance n'a encore créé la région.
    bool open();
    [[nodiscard]] bool isOpen() const noexcept { return header_ != nullptr; }
    void close() noexcept;
    [[nodiscard]] std::vector<InstanceView> instances() const;

private:
    SharedRegion region_;
    const Header* header_ = nullptr;
};

} // namespace omnis::link
