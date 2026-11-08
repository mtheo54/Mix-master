// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/ProdListener.h — Le master écoute la prod qui passe (son entrée),
//  en continu pendant la lecture : la barre IA et AUTO partent de ce qu'il a
//  entendu. Rien n'est écrit : des nombres (signature), et pour AUTO le seul
//  passage le plus fort, en mémoire le temps des essais.
//
//    thread audio (un rédacteur) : push() copie l'entrée dans une file SPSC,
//      sans attente ; file pleine : des échantillons sont perdus pour
//      l'analyse, jamais pour le son ;
//    thread d'analyse (prepare / release) : accumule la signature de toute
//      l'écoute (SignatureAccumulator) et en publie une photo toutes les 2 s ;
//      AUTO : après startAuto(), attend du son, puis écoute TOUT ce qui est
//      joué (le morceau entier) jusqu'à l'arrêt de la lecture (plus de bloc
//      pendant kStopGapSeconds, ou kEndSilenceSeconds de silence), un second
//      clic (finishAuto) ou kAutoMaxSeconds ;
//      au moins kAutoMinSeconds. Garde en mémoire les kExcerptSeconds les plus
//      forts (pour les essais d'AUTO précis : jamais écrits, oubliés ensuite).
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
    static constexpr double kAutoMinSeconds = 12.0;  ///< en dessous, AUTO attend la suite
    static constexpr double kAutoMaxSeconds = 480.0; ///< 8 min : un morceau entier
    static constexpr double kStopGapSeconds = 1.5;   ///< plus aucun bloc pendant ce temps : lecture arrêtée
    static constexpr double kEndSilenceSeconds = 6.0; ///< silence continu pendant la lecture : fin du morceau
    static constexpr double kExcerptSeconds = 15.0;  ///< passage le plus fort gardé pour les essais
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

    /// Côté producteur seulement (le thread qui appelle push : audio, ou un test) :
    /// secondes de son en attente d'analyse dans la file.
    [[nodiscard]] double queuedSeconds() noexcept {
        return sampleRate_ > 0.0 ? static_cast<double>(ring_.capacity() - ring_.freeSpace()) / (2.0 * sampleRate_) : 0.0;
    }

    struct Snapshot {
        analysis::SpectralSignature signature;
        double seconds = 0.0; ///< secondes de son non silencieux entendues
    };
    [[nodiscard]] Snapshot snapshot() const;
    /// Oublier ce qui a été entendu (nouvelle prod).
    void forget() { forget_.store(true, std::memory_order_release); }

    void startAuto() {
        finishAuto_.store(false, std::memory_order_release);
        autoState_.store(static_cast<int>(AutoState::Waiting), std::memory_order_release);
    }
    /// Second clic sur AUTO : finir l'écoute maintenant (si assez de son a été entendu).
    void finishAuto() { finishAuto_.store(true, std::memory_order_release); }
    /// Secondes de son entendues par AUTO en cours.
    [[nodiscard]] double autoSeconds() const noexcept { return autoSeconds_.load(std::memory_order_relaxed); }
    void cancelAuto() { autoState_.store(static_cast<int>(AutoState::Idle), std::memory_order_release); }
    [[nodiscard]] AutoState autoState() const noexcept { return static_cast<AutoState>(autoState_.load(std::memory_order_acquire)); }
    [[nodiscard]] double autoProgress() const noexcept { return autoProgress_.load(std::memory_order_relaxed); }
    /// Le morceau écouté ; repasse l'état à Idle. `excerpt` (facultatif) : le passage le plus
    /// fort, entrelacé L R, kExcerptSeconds au plus (vide si rien).
    bool takeAutoResult(analysis::SpectralSignature& out, std::vector<float>* excerpt = nullptr);
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

private:
    void loop();

    double sampleRate_ = 0.0;
    SpscRingBuffer<float> ring_;
    std::atomic<bool> running_{false}, forget_{false}, finishAuto_{false};
    std::atomic<double> autoSeconds_{0.0};
    std::atomic<int> autoState_{0};
    std::atomic<double> autoProgress_{0.0};
    std::thread worker_;

    // Thread d'analyse.
    analysis::SignatureAccumulator all_, auto_;
    analysis::MusicAnalyzer allMusic_, autoMusic_; ///< la fiche complète (tempo, punch, stéréo…)
    double activeSamples_ = 0.0, autoActive_ = 0.0;
    // Passage le plus fort : fenêtre glissante (entrelacée, circulaire), énergie par seconde.
    std::vector<float> window_, best_;
    std::vector<double> secondEnergy_;
    std::size_t windowPos_ = 0, windowFilled_ = 0, secondIndex_ = 0, inSecond_ = 0;
    double secondAccum_ = 0.0, bestEnergy_ = -1.0;
    bool bestReady_ = false;
    void keepLoudest(const double* l, const double* r, std::size_t n);
    void finishListening();

    mutable std::mutex mutex_;
    Snapshot snapshot_;
    analysis::SpectralSignature autoResult_;
    std::vector<float> autoExcerpt_;
};

} // namespace omnis
