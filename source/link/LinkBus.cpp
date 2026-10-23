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

std::uint64_t newToken() {
    std::random_device rd;
    std::uint64_t t = 0;
    while (t == 0)
        t = (static_cast<std::uint64_t>(rd()) << 32) ^ rd() ^
            static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return t;
}

/// Lecture cohérente de la position de l'anneau d'un stem.
struct RingState {
    std::uint64_t written = 0;
    std::int64_t projectEnd = kNoPosition;
    bool playing = false;
    std::uint64_t playStart = 0, playStop = 0;
};

bool readRing(const Slot& s, RingState& out) noexcept {
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::uint32_t a = s.ringSeq.load(std::memory_order_acquire);
        if (a & 1u) continue;
        out.written = s.written.load(std::memory_order_relaxed);
        out.projectEnd = s.projectEnd.load(std::memory_order_relaxed);
        out.playing = s.playing.load(std::memory_order_relaxed) != 0;
        out.playStart = s.playStart.load(std::memory_order_relaxed);
        out.playStop = s.playStop.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (s.ringSeq.load(std::memory_order_relaxed) == a) return true;
    }
    return false;
}

std::string readName(const Slot& s) {
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::uint32_t a = s.nameSeq.load(std::memory_order_acquire);
        if (a & 1u) continue;
        std::string name;
        for (const auto& c : s.name) {
            const char ch = c.load(std::memory_order_relaxed);
            if (!ch) break;
            name += ch;
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        if (s.nameSeq.load(std::memory_order_relaxed) == a) return name;
    }
    return {};
}

} // namespace


std::string regionName(int group) {
    // Court et sans caractère spécial : macOS limite les noms à 31 caractères.
    std::string prefix = environmentVariable("OMNIS_LINK_NAMESPACE");
    if (prefix.empty()) prefix = "omnislink";
    return prefix + std::to_string(kLayoutVersion) + "g" + std::to_string(std::clamp(group, 1, kGroups));
}

std::uint64_t nowMs() noexcept {
    // Horloge monotone du système, commune à tous les processus (QPC,
    // CLOCK_MONOTONIC) : les cœurs de deux processus se comparent.
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool isLive(const Slot& slot, std::uint64_t now) noexcept {
    if (slot.owner.load(std::memory_order_acquire) == 0) return false;
    const std::uint64_t beat = slot.heartbeatMs.load(std::memory_order_relaxed);
    return now < beat + kStaleMs;
}

// -----------------------------------------------------------------------------
//  Endpoint
// -----------------------------------------------------------------------------
bool Endpoint::join(int group, Role role, StemKind kind, std::string_view name, double sampleRate) {
    leave();
    if (!region_.open(regionName(group), sizeof(Header))) return false;
    auto* h = static_cast<Header*>(region_.data());
    // Première instance du groupe : la mémoire est à zéro, il ne reste qu'à la
    // marquer. Une autre instance qui l'initialise au même moment : on attend.
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

    const std::uint64_t token = newToken();
    const std::uint64_t now = nowMs();
    for (int i = 0; i < kSlots; ++i) {
        Slot& s = h->slots[i];
        std::uint64_t owner = s.owner.load(std::memory_order_acquire);
        // Libre, ou abandonné (cœur arrêté) : on le prend.
        if (owner != 0 && isLive(s, now)) continue;
        if (!s.owner.compare_exchange_strong(owner, token, std::memory_order_acq_rel)) continue;
        s.heartbeatMs.store(now, std::memory_order_relaxed);
        s.role.store(static_cast<std::uint32_t>(role), std::memory_order_relaxed);
        s.kind.store(static_cast<std::uint32_t>(kind), std::memory_order_relaxed);
        s.sampleRate.store(static_cast<std::uint32_t>(std::max(0.0, sampleRate) + 0.5), std::memory_order_relaxed);
        s.ringSeq.store(0, std::memory_order_relaxed);
        s.written.store(0, std::memory_order_relaxed);
        s.projectEnd.store(kNoPosition, std::memory_order_relaxed);
        s.playing.store(0, std::memory_order_relaxed);
        s.playStart.store(0, std::memory_order_relaxed);
        s.playStop.store(0, std::memory_order_relaxed);
        s.orderSeq.store(0, std::memory_order_relaxed);
        s.orderGainDb.store(0.0f, std::memory_order_relaxed);
        s.orderVersion.store(0, std::memory_order_release);
        header_ = h;
        slot_ = &s;
        slotIndex_ = i;
        group_ = group;
        role_ = role;
        token_ = token;
        lastOrders_ = {};
        setName(name);
        return true;
    }
    region_.close(); // groupe plein
    return false;
}

void Endpoint::leave() noexcept {
    if (slot_) {
        std::uint64_t mine = token_;
        slot_->owner.compare_exchange_strong(mine, 0, std::memory_order_acq_rel);
    }
    slot_ = nullptr;
    header_ = nullptr;
    slotIndex_ = -1;
    role_ = Role::None;
    token_ = 0;
    region_.close();
}

void Endpoint::heartbeat() noexcept {
    if (!slot_) return;
    // Repris par une autre instance pendant un long gel (cœur arrêté plus de
    // kStaleMs) : on ne touche plus à cet emplacement.
    if (slot_->owner.load(std::memory_order_acquire) != token_) return;
    slot_->heartbeatMs.store(nowMs(), std::memory_order_relaxed);
}

void Endpoint::setKind(StemKind kind) noexcept {
    if (slot_) slot_->kind.store(static_cast<std::uint32_t>(kind), std::memory_order_relaxed);
}

void Endpoint::setName(std::string_view name) noexcept {
    if (!slot_) return;
    const std::uint32_t seq = slot_->nameSeq.load(std::memory_order_relaxed);
    slot_->nameSeq.store(seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    std::size_t n = std::min<std::size_t>(name.size(), kNameLength - 1);
    // Pas de caractère UTF-8 coupé en deux (octets de continuation 10xxxxxx).
    while (n > 0 && n < name.size() && (static_cast<unsigned char>(name[n]) & 0xC0u) == 0x80u) --n;
    for (std::size_t i = 0; i < static_cast<std::size_t>(kNameLength); ++i)
        slot_->name[i].store(i < n ? name[i] : '\0', std::memory_order_relaxed);
    slot_->nameSeq.store(seq + 2, std::memory_order_release);
}

std::size_t Endpoint::readOwnPlayback(std::uint64_t& readFrame, std::vector<double>& left, std::vector<double>& right) const {
    left.clear();
    right.clear();
    const Slot* s = slot_;
    RingState ring;
    if (!s || !readRing(*s, ring)) return 0;
    if (readFrame == kNoFrame || readFrame > ring.written) readFrame = ring.written;
    std::uint64_t from = readFrame;
    // Trop de retard : on garde le plus récent, sans risque d'être rattrapé.
    if (ring.written - from > static_cast<std::uint64_t>(kRingFrames) / 2) from = ring.written - static_cast<std::uint64_t>(kRingFrames) / 2;
    // Seulement la lecture : du dernier départ jusqu'à maintenant (ou à l'arrêt).
    std::uint64_t a = std::max(from, ring.playStart), b = ring.written;
    if (!ring.playing) b = ring.playStop > ring.playStart ? std::min(b, ring.playStop) : a;
    readFrame = ring.written;
    if (b <= a) return 0;
    const auto n = static_cast<std::size_t>(b - a);
    left.resize(n);
    right.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto k = static_cast<std::size_t>((a + i) & (kRingFrames - 1));
        left[i] = s->audio[0][k].load(std::memory_order_relaxed);
        right[i] = s->audio[1][k].load(std::memory_order_relaxed);
    }
    // Le thread audio n'a pas réécrit ces trames pendant la copie (demi-anneau d'avance au plus).
    RingState after;
    if (!readRing(*s, after) || after.written - a > static_cast<std::uint64_t>(kRingFrames)) {
        left.clear();
        right.clear();
        return 0;
    }
    return n;
}

StemOrders Endpoint::readOrders() const noexcept {
    const Slot* s = slot_;
    if (!s) return lastOrders_;
    // Quelques essais seulement : thread audio. Sinon, les ordres précédents.
    for (int attempt = 0; attempt < 4; ++attempt) {
        const std::uint32_t a = s->orderSeq.load(std::memory_order_acquire);
        if (a & 1u) continue;
        StemOrders o;
        o.gainDb = s->orderGainDb.load(std::memory_order_relaxed);
        o.version = s->orderVersion.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (s->orderSeq.load(std::memory_order_relaxed) == a) {
            lastOrders_ = o;
            break;
        }
    }
    return lastOrders_;
}

std::vector<StemView> Endpoint::stems() const {
    std::vector<StemView> out;
    if (!header_) return out;
    const std::uint64_t now = nowMs();
    for (int i = 0; i < kSlots; ++i) {
        const Slot& s = header_->slots[i];
        if (!isLive(s, now) || s.role.load(std::memory_order_relaxed) != static_cast<std::uint32_t>(Role::Stem)) continue;
        StemView v;
        v.slot = i;
        v.owner = s.owner.load(std::memory_order_acquire);
        v.kind = static_cast<StemKind>(s.kind.load(std::memory_order_relaxed));
        v.name = readName(s);
        v.sampleRate = s.sampleRate.load(std::memory_order_relaxed);
        RingState ring;
        v.playing = readRing(s, ring) && ring.playing;
        out.push_back(std::move(v));
    }
    return out;
}

int Endpoint::liveMasters() const noexcept {
    if (!header_) return 0;
    const std::uint64_t now = nowMs();
    int n = 0;
    for (const auto& s : header_->slots)
        if (isLive(s, now) && s.role.load(std::memory_order_relaxed) == static_cast<std::uint32_t>(Role::Master)) ++n;
    return n;
}

bool Endpoint::writeOrders(int slot, std::uint64_t owner, const StemOrders& orders) noexcept {
    if (!header_ || slot < 0 || slot >= kSlots) return false;
    Slot& s = header_->slots[slot];
    // Le stem a pu partir (et un autre arriver) depuis la liste : jeton vérifié.
    if (s.owner.load(std::memory_order_acquire) != owner) return false;
    const std::uint32_t seq = s.orderSeq.load(std::memory_order_relaxed);
    s.orderSeq.store(seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    s.orderGainDb.store(orders.gainDb, std::memory_order_relaxed);
    s.orderVersion.store(s.orderVersion.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    s.orderSeq.store(seq + 2, std::memory_order_release);
    return true;
}

// -----------------------------------------------------------------------------
//  StemListener
// -----------------------------------------------------------------------------
void StemListener::poll(const Endpoint& master) {
    heardView_.clear();
    const Header* h = master.header();
    if (!h) return;
    const auto views = master.stems();
    std::array<bool, kSlots> present{};
    for (const auto& v : views) {
        present[static_cast<std::size_t>(v.slot)] = true;
        State& st = states_[static_cast<std::size_t>(v.slot)];
        const Slot& s = h->slots[v.slot];
        RingState ring;
        if (!readRing(s, ring)) continue;
        if (st.owner != v.owner || st.sampleRate != v.sampleRate) {
            // Nouveau stem dans cet emplacement : on l'écoute depuis maintenant.
            st.owner = v.owner;
            st.sampleRate = v.sampleRate;
            st.readFrame = ring.written;
            st.accumulator.prepare(v.sampleRate > 0.0 ? v.sampleRate : 48000.0);
            st.heard = {};
            st.signatureSeconds = 0.0;
        }
        if (ring.written < st.readFrame) st.readFrame = ring.written; // emplacement réinitialisé
        std::uint64_t from = st.readFrame;
        if (ring.written - from > static_cast<std::uint64_t>(kRingFrames) / 2) {
            // Trop de retard : on garde le plus récent, sans risque d'être rattrapé.
            const std::uint64_t keep = static_cast<std::uint64_t>(kRingFrames) / 2;
            overruns_ += ring.written - from - keep;
            from = ring.written - keep;
        }
        const auto n = static_cast<std::size_t>(ring.written - from);
        if (n > 0) {
            left_.resize(n);
            right_.resize(n);
            for (std::size_t i = 0; i < n; ++i) {
                const auto k = static_cast<std::size_t>((from + i) & (kRingFrames - 1));
                left_[i] = s.audio[0][k].load(std::memory_order_relaxed);
                right_[i] = s.audio[1][k].load(std::memory_order_relaxed);
            }
            // Vérification : le stem n'a pas réécrit ces trames pendant la copie.
            RingState after;
            const bool intact = readRing(s, after) && after.written - from <= static_cast<std::uint64_t>(kRingFrames);
            // On n'écoute que la lecture : à l'arrêt, un stem envoie du silence
            // ou des notes jouées au clavier, pas le morceau. Trames exactes de
            // la lecture : du dernier départ jusqu'à maintenant (ou à l'arrêt).
            std::uint64_t a = std::max(from, ring.playStart), b = ring.written;
            if (!ring.playing) b = ring.playStop > ring.playStart ? std::min(b, ring.playStop) : a;
            if (intact && b > a) {
                st.accumulator.push(left_.data() + (a - from), right_.data() + (a - from), static_cast<int>(b - a));
                // La signature relit toutes les trames : une fois par seconde écoutée suffit.
                if (st.accumulator.secondsAnalysed() >= st.signatureSeconds + 1.0) {
                    st.heard.signature = st.accumulator.result();
                    st.signatureSeconds = st.accumulator.secondsAnalysed();
                }
            }
            if (!intact) ++overruns_;
            st.readFrame = ring.written;
        }
        st.heard.view = v;
        st.heard.secondsHeard = st.accumulator.secondsAnalysed();
        st.heard.lastProjectPosition = ring.projectEnd;
        heardView_.push_back(st.heard);
    }
    for (int i = 0; i < kSlots; ++i)
        if (!present[static_cast<std::size_t>(i)]) states_[static_cast<std::size_t>(i)].owner = 0;
}

} // namespace omnis::link
