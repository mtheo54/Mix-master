// =============================================================================
//  Abrasion Labs // OMNIS
//  link/LinkClient.cpp
// =============================================================================
#include "LinkClient.h"

#include <algorithm>
#include <chrono>

namespace omnis::link {

void LinkClient::start(Role role, StemKind kind, int group, double sampleRate) {
    stop();
    if (role == Role::None) return;
    role_ = role;
    sampleRate_ = sampleRate;
    setKind(kind);
    setGroup(group);
    quit_.store(false, std::memory_order_relaxed);
    linkedStems_.store(0, std::memory_order_relaxed);
    identified_.store(0, std::memory_order_relaxed);
    classifier_.prepare(sampleRate);
    drumBus_.prepare(sampleRate);
    // ~2,7 s de son sec à 48 kHz (le thread le lit toutes les 50 ms). Le
    // thread audio est à l'arrêt pendant l'activation : réallouer est sûr.
    listenReady_.store(false, std::memory_order_release);
    dry_.allocate(std::size_t{1} << 18);
    listening_.writeBuffer() = Listening{};
    listening_.publish();
    listenReady_.store(role == Role::Stem, std::memory_order_release);
    thread_ = std::thread([this] { run(); });
}

void LinkClient::stop() {
    if (thread_.joinable()) {
        quit_.store(true, std::memory_order_relaxed);
        thread_.join();
    }
    publish(nullptr);
    listenReady_.store(false, std::memory_order_release);
    role_ = Role::None;
    linkedStems_.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(heardMutex_);
        heard_.clear();
    }
    std::lock_guard<std::mutex> lock(conductorMutex_);
    pending_.clear();
    answers_.clear();
    fixes_.clear();
    pendingCount_.store(0, std::memory_order_relaxed);
}

std::vector<LinkClient::PendingProposal> LinkClient::proposals() const {
    std::lock_guard<std::mutex> lock(conductorMutex_);
    return pending_;
}

void LinkClient::answer(std::uint32_t key, Answer a) {
    std::lock_guard<std::mutex> lock(conductorMutex_);
    answers_.emplace_back(key, a);
}

std::vector<LinkClient::Applied> LinkClient::applied() const {
    std::lock_guard<std::mutex> lock(conductorMutex_);
    std::vector<Applied> out;
    for (const auto& f : fixes_)
        for (int i = 0; i < stems::kMaxCorrections; ++i)
            if (f.corrections[static_cast<std::size_t>(i)].type != stems::Correction::Type::None)
                out.push_back({f.keys[static_cast<std::size_t>(i)], f.name, f.corrections[static_cast<std::size_t>(i)]});
    return out;
}

void LinkClient::conduct() {
    const auto& heard = listener_.heard();
    std::vector<stems::ConductorStem> input;
    input.reserve(heard.size());
    for (const auto& h : heard) input.push_back({h.view.owner, h.view.kind, h.view.name, listener_.timeline(h.view.slot)});
    const auto all = stems::Conductor::propose(input);
    std::lock_guard<std::mutex> lock(conductorMutex_);
    pending_.clear();
    for (const auto& p : all) {
        if (std::find(decided_.begin(), decided_.end(), p.key) != decided_.end()) continue;
        const auto& target = heard[static_cast<std::size_t>(p.target)].view;
        pending_.push_back({p, target.name, heard[static_cast<std::size_t>(p.victim)].view.name, target.slot, target.owner});
    }
    pendingCount_.store(static_cast<int>(pending_.size()), std::memory_order_relaxed);
}

void LinkClient::handleAnswers() {
    std::lock_guard<std::mutex> lock(conductorMutex_);
    for (const auto& [key, a] : answers_) {
        if (a == Answer::Undo) {
            for (auto& f : fixes_)
                for (int i = 0; i < stems::kMaxCorrections; ++i)
                    if (f.keys[static_cast<std::size_t>(i)] == key) {
                        f.keys[static_cast<std::size_t>(i)] = 0;
                        f.corrections[static_cast<std::size_t>(i)] = {};
                    }
            continue; // reste « décidée » : la personne n'en veut pas
        }
        const auto it = std::find_if(pending_.begin(), pending_.end(), [&](const PendingProposal& p) { return p.proposal.key == key; });
        if (it == pending_.end()) continue;
        decided_.push_back(key);
        if (a == Answer::Accept) {
            auto f = std::find_if(fixes_.begin(), fixes_.end(), [&](const StemFixes& x) { return x.owner == it->targetOwner; });
            if (f == fixes_.end()) {
                fixes_.push_back({it->targetSlot, it->targetOwner, it->targetName, {}, {}});
                f = fixes_.end() - 1;
            }
            // Une place libre (au-delà de 4, la plus ancienne est remplacée).
            int place = 0;
            for (int i = stems::kMaxCorrections - 1; i >= 0; --i)
                if (f->corrections[static_cast<std::size_t>(i)].type == stems::Correction::Type::None) place = i;
            f->keys[static_cast<std::size_t>(place)] = key;
            f->corrections[static_cast<std::size_t>(place)] = it->proposal.correction;
        }
        pending_.erase(it);
    }
    answers_.clear();
    pendingCount_.store(static_cast<int>(pending_.size()), std::memory_order_relaxed);
}

void LinkClient::sendCorrections() {
    std::lock_guard<std::mutex> lock(conductorMutex_);
    for (auto it = fixes_.begin(); it != fixes_.end();) {
        link::StemOrders orders;
        orders.corrections = it->corrections;
        // Le stem est parti (son emplacement a changé de main) : ses retouches aussi.
        if (!endpoint_ || !endpoint_->writeOrders(it->slot, it->owner, orders)) it = fixes_.erase(it);
        else ++it;
    }
}

void LinkClient::setTrackName(std::string name) {
    std::lock_guard<std::mutex> lock(nameMutex_);
    trackName_ = std::move(name);
}

std::string LinkClient::trackName() const {
    std::lock_guard<std::mutex> lock(nameMutex_);
    return trackName_;
}

stems::Identification LinkClient::identification() const {
    std::lock_guard<std::mutex> lock(nameMutex_);
    return identification_;
}

stems::DrumContent LinkClient::drumContent() const {
    std::lock_guard<std::mutex> lock(nameMutex_);
    return drumContent_;
}

std::vector<StemListener::Heard> LinkClient::heard() const {
    std::lock_guard<std::mutex> lock(heardMutex_);
    return heard_;
}

void LinkClient::publish(std::unique_ptr<Endpoint> next) {
    rtEndpoint_.store(next.get(), std::memory_order_seq_cst);
    // Le thread audio a pu prendre l'ancien juste avant : on attend qu'il
    // l'ait rendu (un bloc audio, quelques millisecondes au pire).
    while (rtBusy_.load(std::memory_order_seq_cst)) std::this_thread::sleep_for(std::chrono::microseconds(200));
    endpoint_ = std::move(next); // l'ancien quitte son emplacement ici
}

void LinkClient::run() {
    int joinedGroup = 0;
    auto lastTry = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    auto lastIdentify = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    auto lastConduct = std::chrono::steady_clock::now();
    auto lastOrders = std::chrono::steady_clock::now();
    // Ce que l'emplacement affiche (élément et nom) : republié quand ça change.
    StemKind shownKind = StemKind::Auto, lastManual = StemKind::Auto;
    std::string shownName;
    std::vector<float> interleaved(dry_.capacity());
    std::vector<double> left, right;
    const auto publishedName = [&](StemKind element) {
        std::string name = trackName();
        return name.empty() ? std::string(stemKindName(element == StemKind::Auto ? StemKind::Other : element)) : name;
    };
    while (!quit_.load(std::memory_order_relaxed)) {
        const int group = group_.load(std::memory_order_relaxed);
        const auto kind = static_cast<StemKind>(kind_.load(std::memory_order_relaxed));
        const auto now = std::chrono::steady_clock::now();
        // Rejoindre (au départ, au changement de groupe, ou après un échec,
        // une fois par seconde : groupe plein, mémoire refusée).
        if ((!endpoint_ || group != joinedGroup) && now - lastTry >= std::chrono::seconds(1)) {
            lastTry = now;
            auto next = std::make_unique<Endpoint>();
            const StemKind element = role_ == Role::Stem ? identifiedElement() : StemKind::Other;
            const std::string name = role_ == Role::Master ? std::string("Master") : publishedName(element);
            if (next->join(group, role_, element, name, sampleRate_)) {
                publish(std::move(next));
                joinedGroup = group;
                shownKind = element;
                shownName = name;
            } else if (group != joinedGroup) {
                publish(nullptr);
                joinedGroup = 0;
            }
        }
        if (endpoint_) {
            endpoint_->heartbeat();
            if (role_ == Role::Stem) {
                // S'écouter (le son sec, pendant la lecture), puis se reconnaître
                // chaque seconde, ou tout de suite quand la personne change l'élément.
                const std::size_t got = dry_.pop(interleaved.data(), interleaved.size()) / 2;
                if (got > 0) {
                    left.resize(got);
                    right.resize(got);
                    for (std::size_t i = 0; i < got; ++i) {
                        left[i] = interleaved[2 * i];
                        right[i] = interleaved[2 * i + 1];
                    }
                    classifier_.push(left.data(), right.data(), static_cast<int>(got));
                    drumBus_.push(left.data(), right.data(), static_cast<int>(got));
                }
                if (now - lastIdentify >= std::chrono::seconds(1) || kind != lastManual) {
                    lastIdentify = now;
                    lastManual = kind;
                    stems::Identification id = classifier_.identify(kind, trackName());
                    identified_.store(static_cast<int>(id.element), std::memory_order_relaxed);
                    listening_.writeBuffer() = Listening{id.element, id.character, id.secondsHeard, drumBus_.content()};
                    listening_.publish();
                    {
                        std::lock_guard<std::mutex> lock(nameMutex_);
                        identification_ = id;
                        drumContent_ = drumBus_.content();
                    }
                }
                const StemKind element = identifiedElement();
                const std::string name = publishedName(element);
                if (element != shownKind) {
                    endpoint_->setKind(element);
                    shownKind = element;
                }
                if (name != shownName) {
                    endpoint_->setName(name);
                    shownName = name;
                }
            } else if (role_ == Role::Master) {
                listener_.poll(*endpoint_);
                linkedStems_.store(static_cast<int>(listener_.heard().size()), std::memory_order_relaxed);
                {
                    std::lock_guard<std::mutex> lock(heardMutex_);
                    heard_ = listener_.heard();
                }
                if (now - lastConduct >= std::chrono::milliseconds(conductorPeriodMs_.load(std::memory_order_relaxed))) {
                    lastConduct = now;
                    conduct();
                }
                bool answered = false;
                {
                    std::lock_guard<std::mutex> lock(conductorMutex_);
                    answered = !answers_.empty();
                }
                if (answered) handleAnswers();
                // Ordres renvoyés à chaque réponse, et chaque seconde (un stem réactivé les retrouve).
                if (answered || now - lastOrders >= std::chrono::seconds(1)) {
                    lastOrders = now;
                    sendCorrections();
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace omnis::link
