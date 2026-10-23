// =============================================================================
//  Abrasion Labs // OMNIS
//  link/LinkBus.h — Les instances d'un projet travaillent ensemble.
//
//  Une instance MASTER (sur le bus master) et des instances STEM (une par
//  piste : 808, kick, voix…) d'un même GROUPE (1 à 8) partagent une région
//  de mémoire (link/SharedRegion.h) :
//
//      stem ──► son emplacement : qui il est (808, kick…), ce qu'il envoie au
//               mix (audio, marqué de la position dans le projet), s'il joue
//      master ──► lit tous les stems, les écoute (signature de chacun) et
//                 écrit dans chaque emplacement des ORDRES (gain pour l'instant)
//      stem ──► applique les ordres de son emplacement
//
//  Le son du mix passe toujours par la console de l'hôte : le master ne
//  ressort pas l'audio des stems (il serait compté deux fois), il s'en sert
//  pour décider.
//
//  Temps réel : le thread audio d'un stem écrit son audio et lit ses ordres
//  sans verrou ni appel système (atomiques sans verrou, séquence de cohérence
//  « seqlock »). Rejoindre, battre le cœur, écouter : thread à part.
//  Un emplacement dont le cœur ne bat plus depuis kStaleMs (hôte planté,
//  processus tué) est repris par la prochaine instance.
//
//  Disposition fixe et versionnée : la version est dans le nom de la région,
//  deux versions d'OMNIS ne lisent jamais la même mémoire.
// =============================================================================
#pragma once

#include "SharedRegion.h"

#include "analysis/SpectralSignature.h"
#include "core/Platform.h"
#include "stems/Element.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace omnis::link {

inline constexpr std::uint32_t kLayoutVersion = 2; ///< 2 : « kind » est un stems::Element (22 éléments)
inline constexpr int kSlots = 32;
inline constexpr int kRingFrames = 1 << 16; ///< ~1.4 s à 48 kHz : le master lit toutes les 50 ms
inline constexpr int kNameLength = 48;
inline constexpr int kGroups = 8;
inline constexpr std::uint64_t kStaleMs = 3000;
inline constexpr std::int64_t kNoPosition = std::numeric_limits<std::int64_t>::min();

enum class Role : std::uint32_t { None = 0, Stem = 1, Master = 2 };
/// Ce que porte un stem : l'élément reconnu (ou choisi), même ordre que le
/// paramètre « Element » (params::ParamId::LinkStemKind).
using StemKind = stems::Element;
inline const char* stemKindName(StemKind kind) noexcept { return stems::elementLabel(kind); }

static_assert(std::atomic<float>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free &&
                  std::atomic<std::int64_t>::is_always_lock_free && std::atomic<std::uint32_t>::is_always_lock_free,
              "la mémoire partagée exige des atomiques sans verrou");

/// Ordres du master pour un stem.
struct StemOrders {
    float gainDb = 0.0f;
    std::uint32_t version = 0; ///< incrémenté à chaque écriture du master
};

/// Ce que le master voit d'un stem.
struct StemView {
    int slot = -1;
    std::uint64_t owner = 0;
    StemKind kind = StemKind::Other;
    std::string name;
    double sampleRate = 0.0;
    bool playing = false;
};

// ---- Disposition de la mémoire partagée -------------------------------------
// Une mémoire neuve est remplie de zéros : tout à zéro = tous les
// emplacements libres (les atomiques sans verrou valent 0).
struct alignas(kCacheLineSize) Slot {
    std::atomic<std::uint64_t> owner;       ///< jeton de l'instance, 0 = libre
    std::atomic<std::uint64_t> heartbeatMs; ///< horloge monotone du système
    std::atomic<std::uint32_t> role;
    std::atomic<std::uint32_t> kind;
    std::atomic<std::uint32_t> sampleRate;
    std::atomic<std::uint32_t> nameSeq;
    std::atomic<char> name[kNameLength];

    alignas(kCacheLineSize) std::atomic<std::uint32_t> ringSeq; ///< impair : écriture en cours
    std::atomic<std::uint64_t> written;                          ///< trames écrites depuis l'arrivée
    std::atomic<std::int64_t> projectEnd;                        ///< position projet de la trame `written`
    std::atomic<std::uint32_t> playing;
    std::atomic<std::uint64_t> playStart; ///< trame du dernier départ de la lecture
    std::atomic<std::uint64_t> playStop;  ///< trame du dernier arrêt

    alignas(kCacheLineSize) std::atomic<std::uint32_t> orderSeq;
    std::atomic<float> orderGainDb;
    std::atomic<std::uint32_t> orderVersion;
    std::atomic<float> orderReserved[64]; ///< EQ, dynamique… sans changer la disposition

    alignas(kCacheLineSize) std::atomic<float> audio[2][kRingFrames];
};

struct Header {
    std::atomic<std::uint32_t> magic;
    std::atomic<std::uint32_t> version;
    Slot slots[kSlots];
};

/// Nom de la région d'un groupe (1 … kGroups). OMNIS_LINK_NAMESPACE le
/// préfixe (tests : ne jamais croiser une vraie session).
std::string regionName(int group);
std::uint64_t nowMs() noexcept;

// -----------------------------------------------------------------------------
//  Une instance dans son groupe
// -----------------------------------------------------------------------------
class Endpoint {
public:
    Endpoint() = default;
    ~Endpoint() { leave(); }
    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;

    /// Non temps réel. Ouvre la région du groupe et prend un emplacement.
    bool join(int group, Role role, StemKind kind, std::string_view name, double sampleRate);
    void leave() noexcept;
    [[nodiscard]] bool joined() const noexcept { return slot_ != nullptr; }
    [[nodiscard]] int group() const noexcept { return group_; }
    [[nodiscard]] int slotIndex() const noexcept { return slotIndex_; }
    [[nodiscard]] Role role() const noexcept { return role_; }

    /// Non temps réel, au moins toutes les secondes : « je suis vivant ».
    void heartbeat() noexcept;
    void setKind(StemKind kind) noexcept;
    void setName(std::string_view name) noexcept;

    // ---- Stem, thread audio (sans verrou, sans appel système) ----------------
    /// Ce que le stem envoie au mix ; `projectStart` : position de la première
    /// trame dans le projet (kNoPosition sans transport).
    template <typename Sample>
    void writeAudio(const Sample* left, const Sample* right, int numFrames, std::int64_t projectStart, bool playing) noexcept;
    /// Derniers ordres cohérents (inchangés si le master écrit au même instant).
    [[nodiscard]] StemOrders readOrders() const noexcept;

    // ---- Stem (non temps réel, thread du lien) ------------------------------
    /// Ce que ce stem a envoyé depuis `readFrame` pendant la lecture, ajouté à
    /// `left` / `right` (vidés d'abord). Sert à le reconnaître (stems/ElementClassifier).
    /// `readFrame` avance ; au premier appel, passer kNoFrame pour partir de maintenant.
    static constexpr std::uint64_t kNoFrame = ~std::uint64_t{0};
    std::size_t readOwnPlayback(std::uint64_t& readFrame, std::vector<double>& left, std::vector<double>& right) const;

    // ---- Master (non temps réel) ----------------------------------------------
    [[nodiscard]] std::vector<StemView> stems() const;
    [[nodiscard]] int liveMasters() const noexcept;
    bool writeOrders(int slot, std::uint64_t owner, const StemOrders& orders) noexcept;
    [[nodiscard]] Header* header() const noexcept { return header_; }

private:
    SharedRegion region_;
    Header* header_ = nullptr;
    Slot* slot_ = nullptr;
    int slotIndex_ = -1;
    int group_ = 0;
    Role role_ = Role::None;
    std::uint64_t token_ = 0;
    mutable StemOrders lastOrders_{};
};

/// Emplacement vivant : pris et cœur battant depuis moins de kStaleMs.
bool isLive(const Slot& slot, std::uint64_t now) noexcept;

// -----------------------------------------------------------------------------
//  Le master écoute les stems
// -----------------------------------------------------------------------------
/// Non temps réel (thread du lien du master). poll() lit ce que chaque stem
/// a envoyé depuis la dernière fois et l'écoute (signature au fil de l'eau,
/// seulement pendant la lecture). Un stem qui revient avec un nouveau jeton
/// repart de zéro.
class StemListener {
public:
    struct Heard {
        StemView view;
        double secondsHeard = 0.0;
        std::int64_t lastProjectPosition = kNoPosition;
        analysis::SpectralSignature signature; ///< recalculée chaque seconde écoutée
    };

    void poll(const Endpoint& master);
    [[nodiscard]] const std::vector<Heard>& heard() const noexcept { return heardView_; }
    [[nodiscard]] std::uint64_t overruns() const noexcept { return overruns_; } ///< trames perdues (master trop lent)

private:
    struct State {
        std::uint64_t owner = 0;
        std::uint64_t readFrame = 0;
        double sampleRate = 0.0;
        double signatureSeconds = 0.0; ///< écoute au dernier calcul de la signature
        analysis::SignatureAccumulator accumulator;
        Heard heard;
    };
    std::array<State, kSlots> states_{};
    std::vector<Heard> heardView_;
    std::vector<double> left_, right_;
    std::uint64_t overruns_ = 0;
};

// ---- Gabarit temps réel ------------------------------------------------------
template <typename Sample>
void Endpoint::writeAudio(const Sample* left, const Sample* right, int numFrames, std::int64_t projectStart, bool playing) noexcept {
    Slot* s = slot_;
    if (!s || numFrames <= 0) return;
    const std::uint64_t start = s->written.load(std::memory_order_relaxed);
    const bool wasPlaying = s->playing.load(std::memory_order_relaxed) != 0;
    // Un NaN de la piste ne doit pas atteindre l'analyse du master.
    const auto clean = [](Sample v) noexcept {
        const double x = static_cast<double>(v);
        return std::isfinite(x) ? static_cast<float>(std::clamp(x, -1.0e4, 1.0e4)) : 0.0f;
    };
    for (int i = 0; i < numFrames; ++i) {
        const auto k = static_cast<std::size_t>((start + static_cast<std::uint64_t>(i)) & (kRingFrames - 1));
        s->audio[0][k].store(clean(left[i]), std::memory_order_relaxed);
        s->audio[1][k].store(clean(right[i]), std::memory_order_relaxed);
    }
    const std::uint32_t seq = s->ringSeq.load(std::memory_order_relaxed);
    s->ringSeq.store(seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    s->written.store(start + static_cast<std::uint64_t>(numFrames), std::memory_order_relaxed);
    s->projectEnd.store(projectStart == kNoPosition ? kNoPosition : projectStart + numFrames, std::memory_order_relaxed);
    s->playing.store(playing ? 1u : 0u, std::memory_order_relaxed);
    if (playing && !wasPlaying) s->playStart.store(start, std::memory_order_relaxed);
    if (!playing && wasPlaying) s->playStop.store(start, std::memory_order_relaxed);
    s->ringSeq.store(seq + 2, std::memory_order_release);
}

} // namespace omnis::link
