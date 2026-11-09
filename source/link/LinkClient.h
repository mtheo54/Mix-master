// =============================================================================
//  Abrasion Labs // OMNIS
//  link/LinkClient.h — Une instance BUS dans la région partagée.
//
//  Thread audio (process) : le son passe tel quel ; pendant la lecture, une
//  copie part vers le thread du lien. Le morceau entier est écouté, et chaque
//  seconde du morceau une seule fois (repérée par la position dans le projet) :
//  rejouer une boucle ne compte pas ses coups deux fois. La file tient ~11 s
//  de son : le thread du lien la vide bien plus vite que le temps réel.
//  Thread du lien (toutes les 50 ms) : rejoint la région (et réessaie), bat le
//  cœur, écoute le bus (stems/BusListener) et, toutes les secondes, publie le
//  nom du bus, ce qu'il y entend et ce qu'il a écouté du morceau. Une demande
//  de réécoute (bouton RÉÉCOUTER de n'importe quelle instance) remet tout à zéro.
// =============================================================================
#pragma once

#include "LinkBus.h"

#include "core/SpscRingBuffer.h"
#include "stems/BusListener.h"

#include <algorithm>
#include <array>
#include <cstdint>
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

    /// Durée du morceau suivie (au-delà, chaque passage est écouté sans dédoublonner).
    static constexpr int kSongSeconds = 3600;
    /// Non temps réel : tout réécouter (traité par le thread du lien au tour suivant).
    void relisten() noexcept { relistenWanted_.store(true, std::memory_order_release); }

    /// Temps réel, thread audio seulement : le son du bus pendant la lecture. `position` :
    /// première trame du bloc dans le projet (négative : inconnue, tout est écouté).
    template <typename Sample>
    void process(const Sample* left, const Sample* right, int numFrames, bool playing, std::int64_t position) noexcept {
        if (!ready_.load(std::memory_order_acquire)) return;
        // Réécoute : le thread du lien a tout effacé ; on oublie la seconde en cours.
        if (relistenRequested_.exchange(false, std::memory_order_acq_rel)) lastSecond_ = -1;
        if (!playing) {
            // Arrêt : la seconde en cours, prise depuis son début, compte comme entendue (la fin d'un
            // morceau n'est jamais « quittée » vers la suivante).
            if (lastSecond_ >= 0 && lastSecond_ < kSongSeconds && enteredClean_)
                heardSecond_[static_cast<std::size_t>(lastSecond_)].store(1, std::memory_order_relaxed);
            lastSecond_ = -1;
            enteredClean_ = false;
            return;
        }
        // Une seconde du morceau est « entendue » une fois jouée de bout en bout (on y est entré
        // depuis la précédente, puis on en est sorti vers la suivante). Déjà entendue : pas réécoutée.
        if (position >= 0) {
            const auto second = static_cast<std::int64_t>(static_cast<double>(position) / sampleRate_);
            if (second != lastSecond_) {
                if (lastSecond_ >= 0 && second == lastSecond_ + 1 && lastSecond_ < kSongSeconds && enteredClean_)
                    heardSecond_[static_cast<std::size_t>(lastSecond_)].store(1, std::memory_order_relaxed);
                // Prise depuis son début : on vient de la précédente, ou la lecture part de son premier bloc.
                enteredClean_ = (lastSecond_ >= 0 && second == lastSecond_ + 1) ||
                                static_cast<double>(position) - static_cast<double>(second) * sampleRate_ < numFrames;
                lastSecond_ = second;
            }
            if (second >= 0 && second < kSongSeconds && heardSecond_[static_cast<std::size_t>(second)].load(std::memory_order_relaxed)) return;
        }
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
    std::atomic<bool> relistenRequested_{false}, relistenWanted_{false};
    std::array<std::atomic<std::uint8_t>, kSongSeconds> heardSecond_{}; ///< écrit par le thread audio, lu par le thread du lien
    std::int64_t lastSecond_ = -1;  ///< thread audio seulement
    bool enteredClean_ = false;     ///< thread audio seulement : la seconde en cours a été prise depuis son début
    std::atomic<int> slot_{-1};
    std::thread thread_;
    SpscRingBuffer<float> dry_;
    stems::BusListener listener_; ///< thread du lien seulement

    mutable std::mutex mutex_;
    std::string trackName_;
    stems::BusContent content_;
};

} // namespace omnis::link
