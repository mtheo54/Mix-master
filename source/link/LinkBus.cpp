// =============================================================================
//  Abrasion Labs // OMNIS
//  link/LinkBus.cpp
// =============================================================================
#include "LinkBus.h"

#include "core/UserDataDir.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <thread>

namespace omnis::link {

namespace {

constexpr std::uint32_t kMagic = 0x4B4C4D4Fu;       // « OMLK »
constexpr std::uint32_t kInitializing = 0x54494E49u; // « INIT »
constexpr std::uint32_t kFlagDisagrees = 1u, kFlagKick = 2u, kFlagSnare = 4u, kFlagHat = 8u, kFlagOpenHat = 16u;

std::uint64_t newToken() {
    std::random_device rd;
    std::uint64_t t = 0;
    while (t == 0)
        t = (static_cast<std::uint64_t>(rd()) << 32) ^ rd() ^
            static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return t;
}

stems::Element elementOf(std::uint32_t v) {
    return v < static_cast<std::uint32_t>(stems::kElementCount) ? static_cast<stems::Element>(v) : stems::Element::Other;
}

/// Nom et identité lus d'un seul tenant (séquence paire, inchangée pendant la lecture).
bool readInfo(const Slot& s, std::string& name, Identity& id) {
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::uint32_t a = s.infoSeq.load(std::memory_order_acquire);
        if (a & 1u) {
            std::this_thread::yield();
            continue;
        }
        name.clear();
        for (const auto& c : s.name) {
            const char ch = c.load(std::memory_order_relaxed);
            if (!ch) break;
            name += ch;
        }
        id.element = elementOf(s.element.load(std::memory_order_relaxed));
        id.heard = elementOf(s.heard.load(std::memory_order_relaxed));
        const std::uint32_t f = s.flags.load(std::memory_order_relaxed);
        id.nameDisagrees = (f & kFlagDisagrees) != 0;
        id.kick = (f & kFlagKick) != 0;
        id.snare = (f & kFlagSnare) != 0;
        id.hat = (f & kFlagHat) != 0;
        id.openHat = (f & kFlagOpenHat) != 0;
        id.confidence = s.confidence.load(std::memory_order_relaxed);
        id.secondsHeard = s.secondsHeard.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (s.infoSeq.load(std::memory_order_relaxed) == a) return true;
    }
    return false;
}

} // namespace

std::string regionName() {
    // Court et sans caractère spécial : macOS limite les noms à 31 caractères.
    std::string prefix = environmentVariable("OMNIS_LINK_NAMESPACE");
    if (prefix.empty()) prefix = "omnislink";
    return prefix + std::to_string(kLayoutVersion);
}

std::uint64_t nowMs() noexcept {
    // Horloge monotone commune à tous les processus (QPC, CLOCK_MONOTONIC).
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool isLive(const Slot& slot, std::uint64_t now) noexcept {
    if (slot.owner.load(std::memory_order_acquire) == 0) return false;
    return now < slot.heartbeatMs.load(std::memory_order_relaxed) + kStaleMs;
}

// -----------------------------------------------------------------------------
//  Endpoint
// -----------------------------------------------------------------------------
bool Endpoint::join(Role role, std::string_view name, double sampleRate) {
    leave();
    if (!region_.open(regionName(), sizeof(Header))) return false;
    auto* h = static_cast<Header*>(region_.data());
    // Première instance : la mémoire est à zéro, il ne reste qu'à la marquer.
    std::uint32_t expected = 0;
    if (h->magic.compare_exchange_strong(expected, kInitializing, std::memory_order_acq_rel)) {
        h->version.store(kLayoutVersion, std::memory_order_relaxed);
        h->magic.store(kMagic, std::memory_order_release);
    } else {
        for (int i = 0; i < 1000 && h->magic.load(std::memory_order_acquire) == kInitializing; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (h->magic.load(std::memory_order_acquire) != kMagic || h->version.load(std::memory_order_relaxed) != kLayoutVersion) {
        region_.close();
        return false;
    }
    const std::uint64_t token = newToken(), now = nowMs();
    for (int i = 0; i < kSlots; ++i) {
        Slot& s = h->slots[i];
        std::uint64_t owner = s.owner.load(std::memory_order_acquire);
        if (owner != 0 && isLive(s, now)) continue; // libre, ou abandonné : on le prend
        if (!s.owner.compare_exchange_strong(owner, token, std::memory_order_acq_rel)) continue;
        s.heartbeatMs.store(now, std::memory_order_relaxed);
        s.role.store(static_cast<std::uint32_t>(role), std::memory_order_relaxed);
        s.sampleRate.store(static_cast<std::uint32_t>(std::max(0.0, sampleRate) + 0.5), std::memory_order_relaxed);
        for (auto& t : s.frameTag) t.store(0, std::memory_order_relaxed); // pas de trame héritée
        std::atomic_thread_fence(std::memory_order_release);
        slot_ = &s;
        slotIndex_ = i;
        token_ = token;
        publish(name, {});
        return true;
    }
    region_.close(); // plein
    return false;
}

void Endpoint::leave() noexcept {
    if (slot_) {
        std::uint64_t mine = token_;
        slot_->owner.compare_exchange_strong(mine, 0, std::memory_order_acq_rel);
    }
    slot_ = nullptr;
    slotIndex_ = -1;
    token_ = 0;
    region_.close();
}

void Endpoint::heartbeat() noexcept {
    // Repris par une autre instance pendant un long gel : on n'y touche plus.
    if (!slot_ || slot_->owner.load(std::memory_order_acquire) != token_) return;
    slot_->heartbeatMs.store(nowMs(), std::memory_order_relaxed);
}

void Endpoint::publish(std::string_view name, const Identity& id) noexcept {
    if (!slot_) return;
    Slot& s = *slot_;
    const std::uint32_t seq = s.infoSeq.load(std::memory_order_relaxed);
    s.infoSeq.store(seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    std::size_t n = std::min<std::size_t>(name.size(), kNameLength - 1);
    // Pas de caractère UTF-8 coupé en deux (octets de continuation 10xxxxxx).
    while (n > 0 && n < name.size() && (static_cast<unsigned char>(name[n]) & 0xC0u) == 0x80u) --n;
    for (std::size_t i = 0; i < static_cast<std::size_t>(kNameLength); ++i) s.name[i].store(i < n ? name[i] : '\0', std::memory_order_relaxed);
    s.element.store(static_cast<std::uint32_t>(id.element), std::memory_order_relaxed);
    s.heard.store(static_cast<std::uint32_t>(id.heard), std::memory_order_relaxed);
    s.flags.store((id.nameDisagrees ? kFlagDisagrees : 0u) | (id.kick ? kFlagKick : 0u) | (id.snare ? kFlagSnare : 0u) | (id.hat ? kFlagHat : 0u) |
                      (id.openHat ? kFlagOpenHat : 0u),
                  std::memory_order_relaxed);
    s.confidence.store(id.confidence, std::memory_order_relaxed);
    s.secondsHeard.store(id.secondsHeard, std::memory_order_relaxed);
    s.infoSeq.store(seq + 2, std::memory_order_release);
}

// -----------------------------------------------------------------------------
//  Observer
// -----------------------------------------------------------------------------
bool Observer::open() {
    if (header_) return true;
    // N'ouvre que si la région existe déjà et porte la bonne version.
    if (!region_.open(regionName(), sizeof(Header))) return false;
    const auto* h = static_cast<const Header*>(region_.data());
    if (h->magic.load(std::memory_order_acquire) != kMagic || h->version.load(std::memory_order_relaxed) != kLayoutVersion) {
        region_.close();
        return false;
    }
    header_ = h;
    return true;
}

void Observer::close() noexcept {
    header_ = nullptr;
    region_.close();
}

std::vector<InstanceView> Observer::instances() const {
    std::vector<InstanceView> out;
    if (!header_) return out;
    const std::uint64_t now = nowMs();
    for (int i = 0; i < kSlots; ++i) {
        const Slot& s = header_->slots[i];
        if (!isLive(s, now)) continue;
        InstanceView v;
        v.slot = i;
        v.owner = s.owner.load(std::memory_order_acquire);
        const std::uint32_t role = s.role.load(std::memory_order_relaxed);
        v.role = role == 1 ? Role::Track : role == 2 ? Role::Bus : Role::None;
        v.sampleRate = s.sampleRate.load(std::memory_order_relaxed);
        if (v.role == Role::None || !readInfo(s, v.name, v.identity)) continue;
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<BandFrame> Observer::frames(int slot, std::uint64_t owner) const {
    std::vector<BandFrame> out;
    if (!header_ || slot < 0 || slot >= kSlots) return out;
    const Slot& s = header_->slots[slot];
    if (s.owner.load(std::memory_order_acquire) != owner) return out;
    out.reserve(kFrameRing);
    for (int r = 0; r < kFrameRing; ++r) {
        const std::int64_t tag = s.frameTag[r].load(std::memory_order_acquire);
        if (tag <= 0) continue;
        BandFrame f;
        f.index = tag - 1;
        for (int b = 0; b < kBands; ++b) f.energy[static_cast<std::size_t>(b)] = s.frameEnergy[r][b].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (s.frameTag[r].load(std::memory_order_relaxed) != tag) continue; // réécrite pendant la lecture
        out.push_back(f);
    }
    std::sort(out.begin(), out.end(), [](const BandFrame& a, const BandFrame& b) { return a.index < b.index; });
    return out;
}

} // namespace omnis::link
