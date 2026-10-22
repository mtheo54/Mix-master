// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/ReferenceCapture.h — Écouter une référence jouée dans le DAW.
//
//  La personne joue un titre de référence qu'elle possède (titre acheté, prod
//  personnelle…) sur l'entrée auxiliaire du plug-in. OMNIS en mesure la
//  signature (analysis::SignatureAccumulator) et, dans le même temps, celle de
//  la prod qui passe sur l'entrée principale. Seuls des nombres sont gardés :
//  aucun audio n'est enregistré.
//
//  Temps réel :
//    thread audio  : push() copie les échantillons (float) dans deux files SPSC
//                    sans verrou ; si une file est pleine, l'analyse perd des
//                    échantillons plutôt que de faire attendre l'audio
//                    (droppedSamples()).
//    thread d'analyse (démarré par prepare(), arrêté par release()) : vide les
//                    files dans les accumulateurs. Quand l'écoute s'arrête
//                    (setCapturing(false)), il termine l'analyse et appelle le
//                    gestionnaire de fin — sur SON thread, jamais l'audio.
// =============================================================================
#pragma once

#include "analysis/SpectralSignature.h"
#include "core/SpscRingBuffer.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace omnis {

class ReferenceCapture {
public:
    struct Result {
        analysis::SpectralSignature reference;
        analysis::SpectralSignature prod;
        double referenceSeconds = 0.0; ///< secondes de référence non silencieuse
    };
    using CompletionHandler = std::function<void(const Result&)>;

    ReferenceCapture() = default;
    ~ReferenceCapture() { release(); }
    ReferenceCapture(const ReferenceCapture&) = delete;
    ReferenceCapture& operator=(const ReferenceCapture&) = delete;

    /// Non temps réel : alloue les files (`ringSeconds` d'avance) et démarre l'analyse.
    void prepare(double sampleRate, double ringSeconds = 8.0);
    /// Non temps réel : arrête le thread d'analyse.
    void release();
    /// Avant prepare().
    void setCompletionHandler(CompletionHandler handler) { handler_ = std::move(handler); }

    // ---- Thread audio --------------------------------------------------------
    void setCapturing(bool on) noexcept { capturing_.store(on, std::memory_order_release); }
    [[nodiscard]] bool capturing() const noexcept { return capturing_.load(std::memory_order_acquire); }
    /// N'a d'effet que pendant l'écoute. Une voie absente (nullptr) compte comme du silence.
    template <typename Sample>
    void push(const Sample* refL, const Sample* refR, const Sample* prodL, const Sample* prodR, int numSamples) noexcept;

    // ---- État (n'importe quel thread) ---------------------------------------
    [[nodiscard]] double referenceSeconds() const noexcept { return refSeconds_.load(std::memory_order_relaxed); }
    [[nodiscard]] double referenceLufs() const noexcept { return refLufs_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t droppedSamples() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    /// Nombre d'échantillons en attente d'analyse (approximation).
    [[nodiscard]] std::size_t pending() const noexcept { return pending_.load(std::memory_order_relaxed); }
    /// Dernier résultat terminé (false si aucun).
    bool lastResult(Result& out) const;

private:
    struct Frame {
        float l, r;
    };
    void workerLoop();
    std::size_t drain(bool& sawSignal);

    double sampleRate_ = 48000.0;
    SpscRingBuffer<Frame> refRing_, prodRing_;
    std::atomic<bool> capturing_{false}, running_{false};
    std::atomic<double> refSeconds_{0.0}, refLufs_{-70.0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::size_t> pending_{0};
    std::thread worker_;
    CompletionHandler handler_;

    // Thread d'analyse uniquement.
    analysis::SignatureAccumulator refAcc_, prodAcc_;
    std::vector<Frame> chunkRef_, chunkProd_;
    std::vector<double> l_, r_, pl_, pr_;
    double activeSamples_ = 0.0;

    mutable std::mutex resultMutex_;
    Result result_;
    bool hasResult_ = false;
};

template <typename Sample>
void ReferenceCapture::push(const Sample* refL, const Sample* refR, const Sample* prodL, const Sample* prodR, int numSamples) noexcept {
    if (!capturing_.load(std::memory_order_acquire) || numSamples <= 0) return;
    constexpr int kChunk = 256;
    Frame ref[kChunk], prod[kChunk];
    for (int pos = 0; pos < numSamples; pos += kChunk) {
        const int n = std::min(kChunk, numSamples - pos);
        for (int i = 0; i < n; ++i) {
            const int k = pos + i;
            ref[i] = {refL ? static_cast<float>(refL[k]) : 0.0f, refR ? static_cast<float>(refR[k]) : (refL ? static_cast<float>(refL[k]) : 0.0f)};
            prod[i] = {prodL ? static_cast<float>(prodL[k]) : 0.0f,
                       prodR ? static_cast<float>(prodR[k]) : (prodL ? static_cast<float>(prodL[k]) : 0.0f)};
        }
        // Les deux files avancent ensemble : on n'écrit que ce que les deux acceptent.
        const std::size_t room = std::min(refRing_.freeSpace(), prodRing_.freeSpace());
        const std::size_t take = std::min<std::size_t>(room, static_cast<std::size_t>(n));
        refRing_.push(ref, take);
        prodRing_.push(prod, take);
        if (take < static_cast<std::size_t>(n)) dropped_.fetch_add(static_cast<std::uint64_t>(n) - take, std::memory_order_relaxed);
        pending_.fetch_add(take, std::memory_order_relaxed);
    }
}

} // namespace omnis
