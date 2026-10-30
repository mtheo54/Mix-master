// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/ProdListener.h — Le master écoute la prod qui passe (son entrée),
//  en continu pendant la lecture : la barre IA et AUTO partent de ce qu'il a
//  entendu. Seuls des nombres sont gardés (signature), jamais d'audio.
//
//    thread audio (un rédacteur) : push() copie l'entrée dans une file SPSC,
//      sans attente ; file pleine : des échantillons sont perdus pour
//      l'analyse, jamais pour le son ;
//    thread d'analyse (prepare / release) : accumule la signature de toute
//      l'écoute (SignatureAccumulator) et en publie une photo toutes les 2 s ;
//      AUTO : après startAuto(), attend du son, mesure kAutoSeconds de son
//      non silencieux (le passage le plus fort que la personne joue), publie.
//    n'importe quel thread : snapshot(), autoState(), autoResult().
// =============================================================================
#pragma once

#include "analysis/SpectralSignature.h"
#include "core/SpscRingBuffer.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace omnis {

class ProdListener {
public:
    static constexpr double kAutoSeconds = 10.0;
    enum class AutoState : int { Idle = 0, Waiting = 1, Listening = 2, Done = 3 };

    ProdListener() = default;
    ~ProdListener() { release(); }
    ProdListener(const ProdListener&) = delete;
    ProdListener& operator=(const ProdListener&) = delete;

    /// Non temps réel : alloue la file (8 s d'avance), démarre l'analyse. L'écoute
    /// déjà accumulée est gardée si la fréquence ne change pas.
    void prepare(double sampleRate);
    void release();

    template <typename Sample>
    void push(const Sample* left, const Sample* right, int numSamples) noexcept {
        if (!running_.load(std::memory_order_acquire) || numSamples <= 0) return;
        constexpr int kChunk = 256;
        float buf[2 * kChunk];
        for (int pos = 0; pos < numSamples; pos += kChunk) {
            const int n = std::min(kChunk, numSamples - pos);
            for (int i = 0; i < n; ++i) {
                buf[2 * i] = static_cast<float>(left[pos + i]);
                buf[2 * i + 1] = static_cast<float>(right[pos + i]);
            }
            if (ring_.freeSpace() >= static_cast<std::size_t>(2 * n)) ring_.push(buf, static_cast<std::size_t>(2 * n));
        }
    }

    struct Snapshot {
        analysis::SpectralSignature signature;
        double seconds = 0.0; ///< secondes de son non silencieux entendues
    };
    [[nodiscard]] Snapshot snapshot() const;
    /// Oublier ce qui a été entendu (nouvelle prod).
    void forget() { forget_.store(true, std::memory_order_release); }

    void startAuto() { autoState_.store(static_cast<int>(AutoState::Waiting), std::memory_order_release); }
    void cancelAuto() { autoState_.store(static_cast<int>(AutoState::Idle), std::memory_order_release); }
    [[nodiscard]] AutoState autoState() const noexcept { return static_cast<AutoState>(autoState_.load(std::memory_order_acquire)); }
    [[nodiscard]] double autoProgress() const noexcept { return autoProgress_.load(std::memory_order_relaxed); }
    /// Le passage écouté ; repasse l'état à Idle.
    bool takeAutoResult(analysis::SpectralSignature& out);

private:
    void loop();

    double sampleRate_ = 0.0;
    SpscRingBuffer<float> ring_;
    std::atomic<bool> running_{false}, forget_{false};
    std::atomic<int> autoState_{0};
    std::atomic<double> autoProgress_{0.0};
    std::thread worker_;

    // Thread d'analyse.
    analysis::SignatureAccumulator all_, auto_;
    analysis::MusicAnalyzer allMusic_, autoMusic_; ///< la fiche complète (tempo, punch, stéréo…)
    double activeSamples_ = 0.0, autoActive_ = 0.0;

    mutable std::mutex mutex_;
    Snapshot snapshot_;
    analysis::SpectralSignature autoResult_;
};

} // namespace omnis
