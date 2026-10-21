// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/SubKernelBank.h — Noyaux FIR à phase linéaire du Sub Control.
//
//  Conception par échantillonnage fréquentiel :
//    1. On écrit directement les réponses en module cibles, à phase nulle :
//         Mid  : H_M(f) = HP(f) · (1 + (g − 1)·LP_sub(f))
//         Side : H_S(f) = HP(f) · (1 − a·LP_mono(f))
//       avec  HP      = Butterworth 4 (low cut)  |H| = w⁴ / sqrt(1 + w⁸)
//             LP_mono = Linkwitz-Riley           |H| = 1/(1 + w⁴) (LR4) ou 1/(1 + w⁸) (LR8)
//             LP_sub  = LR4 à la fréquence Sub,  g = 10^(gain/20),  a = Mono Amount
//       1 − LP_LR est exactement le passe-haut LR complémentaire : Mid et Side
//       restent rigoureusement en phase (aucune rotation, aucun pré-écho M/S).
//    2. IFFT sur 2N points -> réponse impulsionnelle réelle, paire.
//    3. Fenêtrage Kaiser (β = 6) sur N taps centrés -> FIR causal, retard N/2.
//       Prototype mesuré (N = 16384 à 44.1 kHz) : Mono Maker à ±0.7 dB de la
//       cible jusqu'à -60 dB ; low cut 16 Hz : -3.3 dB à fc, -64 dB au DC.
//    4. Découpage en partitions pour le convolueur UPOLS.
//  Mid et Side sont calculés ensemble : une seule IFFT complexe (Mid en partie
//  réelle, Side en partie imaginaire).
//
//  Banque de noyaux sans verrou : 3 emplacements, chacun avec un état atomique.
//      Free ──(calcul)──► Writing ──► Ready ──(thread audio)──► Active ──► Retiring ──► Free
//  Le thread audio ne fait que des compare_exchange et des store ; il n'attend
//  jamais. Le thread de calcul ne touche qu'aux emplacements Free ou Ready.
//  Avec 3 emplacements (actif + en fondu + 1), le calcul trouve toujours une place.
//
//  Mode synchrone (rendu hors ligne, tests) : pas de thread, le noyau est calculé
//  immédiatement à la frontière de trame -> rendu déterministe, identique quel
//  que soit le découpage en blocs.
// =============================================================================
#pragma once

#include "dsp/Fft.h"
#include "dsp/StereoConvolver.h"

#include <array>
#include <atomic>
#include <complex>
#include <thread>
#include <vector>

#include "core/TripleBuffer.h"

namespace omnis {

struct SubKernelRequest {
    double lowCutHz = 16.0;
    double monoHz = 120.0;
    double monoAmount = 1.0; ///< 0..1
    double slopeMix = 0.0;   ///< 0 = LR4, 1 = LR8 (fondu continu)
    double subGainDb = 0.0;
    double subHz = 70.0;

    /// Égalité à une tolérance inaudible (évite des recalculs inutiles).
    [[nodiscard]] bool nearlyEquals(const SubKernelRequest& o) const noexcept;

    [[nodiscard]] double midMagnitude(double frequency) const noexcept;
    [[nodiscard]] double sideMagnitude(double frequency) const noexcept;
};

/// Calcul pur d'un noyau stéréo (Mid, Side). Possède son propre espace de travail.
class SubKernelDesigner {
public:
    void prepare(double sampleRate, int kernelLength, int blockSize);
    /// Sans allocation.
    void design(const SubKernelRequest& request, dsp::StereoConvolver::Kernel& out) noexcept;

    static constexpr double kKaiserBeta = 6.0;

private:
    double sampleRate_ = 48000.0;
    int length_ = 0;
    dsp::Fft fft_;
    std::vector<std::complex<double>> spectrum_;
    std::vector<double> window_, timeMid_, timeSide_;
    dsp::StereoConvolver::KernelBuilder builder_;
};

class SubKernelBank {
public:
    enum State : int { Free = 0, Writing, Ready, Active, Retiring };

    struct Slot {
        dsp::StereoConvolver::Kernel kernel;
        SubKernelRequest request;
        std::atomic<int> state{Free};
    };

    SubKernelBank() = default;
    ~SubKernelBank() { stop(); }
    SubKernelBank(const SubKernelBank&) = delete;
    SubKernelBank& operator=(const SubKernelBank&) = delete;

    /// Non temps réel. Calcule le noyau initial (actif immédiatement) et démarre
    /// le thread de calcul si `background`.
    void prepare(double sampleRate, int kernelLength, int blockSize, bool background,
                 const SubKernelRequest& initial);
    void stop();

    // ---- Thread audio -------------------------------------------------------
    /// Demande un noyau pour ces réglages (sans effet s'ils n'ont pas changé).
    void request(const SubKernelRequest& r) noexcept;
    /// Noyau prêt à remplacer l'actif (passe à Active), ou nullptr.
    Slot* takeReady() noexcept;
    Slot* initialSlot() noexcept { return &slots_[0]; }
    static void markRetiring(Slot* s) noexcept { s->state.store(Retiring, std::memory_order_relaxed); }
    static void release(Slot* s) noexcept { s->state.store(Free, std::memory_order_release); }

private:
    Slot* acquireForWriting() noexcept;
    void workerLoop();

    std::array<Slot, 3> slots_;
    SubKernelDesigner designer_;
    bool background_ = false;
    SubKernelRequest lastRequest_{};

    TripleBuffer<SubKernelRequest> requests_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

} // namespace omnis
