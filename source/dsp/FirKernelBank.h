// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/FirKernelBank.h — Banque de noyaux FIR sans verrou (thread audio / calcul).
//
//  Trois emplacements, chacun avec un état atomique :
//      Free ──(calcul)──► Writing ──► Ready ──(thread audio)──► Active ──► Retiring ──► Free
//  Le thread audio ne fait que des compare_exchange et des store ; il n'attend
//  jamais. Le thread de calcul ne touche qu'aux emplacements Free ou Ready.
//  Avec 3 emplacements (actif + en fondu + 1), le calcul trouve toujours une place.
//
//  Mode synchrone (rendu hors ligne, tests) : pas de thread, le noyau est
//  calculé immédiatement -> rendu déterministe, indépendant du découpage.
//
//  Request : type copiable avec `bool nearlyEquals(const Request&) const`.
//  Designer : `prepare(fs, N, B)` et `design(const Request&, Kernel&)` sans allocation.
// =============================================================================
#pragma once

#include "StereoConvolver.h"

#include "core/TripleBuffer.h"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

namespace omnis::dsp {

template <typename Request, typename Designer>
class FirKernelBank {
public:
    enum State : int { Free = 0, Writing, Ready, Active, Retiring };

    struct Slot {
        StereoConvolver::Kernel kernel;
        Request request{};
        std::atomic<int> state{Free};
    };

    FirKernelBank() = default;
    ~FirKernelBank() { stop(); }
    FirKernelBank(const FirKernelBank&) = delete;
    FirKernelBank& operator=(const FirKernelBank&) = delete;

    /// Non temps réel. Calcule le noyau initial (actif immédiatement) et démarre
    /// le thread de calcul si `background`.
    void prepare(double sampleRate, int kernelLength, int blockSize, bool background, const Request& initial) {
        stop();
        designer_.prepare(sampleRate, kernelLength, blockSize);
        for (auto& slot : slots_) {
            slot.kernel.allocate(kernelLength / blockSize, blockSize);
            slot.state.store(Free, std::memory_order_relaxed);
        }
        designer_.design(initial, slots_[0].kernel);
        slots_[0].request = initial;
        slots_[0].state.store(Active, std::memory_order_release);
        lastRequest_ = initial;
        requests_.writeBuffer() = initial;
        requests_.publish();
        requests_.update();
        background_ = background;
        if (background_) {
            running_.store(true, std::memory_order_release);
            worker_ = std::thread([this] { workerLoop(); });
        }
    }

    void stop() {
        if (worker_.joinable()) {
            running_.store(false, std::memory_order_release);
            worker_.join();
        }
    }

    // ---- Thread audio -------------------------------------------------------
    /// Demande un noyau pour ces réglages (sans effet s'ils n'ont pas changé).
    void request(const Request& r) noexcept {
        if (r.nearlyEquals(lastRequest_)) return;
        lastRequest_ = r;
        if (background_) {
            requests_.writeBuffer() = r;
            requests_.publish();
            return;
        }
        if (Slot* slot = acquireForWriting()) {
            designer_.design(r, slot->kernel);
            slot->request = r;
            slot->state.store(Ready, std::memory_order_release);
        }
    }

    /// Noyau prêt à remplacer l'actif (passe à Active), ou nullptr.
    Slot* takeReady() noexcept {
        for (auto& slot : slots_) {
            int expected = Ready;
            if (slot.state.compare_exchange_strong(expected, Active, std::memory_order_acquire)) return &slot;
        }
        return nullptr;
    }

    Slot* initialSlot() noexcept { return &slots_[0]; }
    static void markRetiring(Slot* s) noexcept { s->state.store(Retiring, std::memory_order_relaxed); }
    static void release(Slot* s) noexcept { s->state.store(Free, std::memory_order_release); }

private:
    Slot* acquireForWriting() noexcept {
        // Priorité aux emplacements libres, sinon on écrase un noyau Ready périmé
        // (le thread audio ne l'a pas encore pris : le CAS échoue s'il le prend).
        for (int wanted : {static_cast<int>(Free), static_cast<int>(Ready)}) {
            for (auto& slot : slots_) {
                int expected = wanted;
                if (slot.state.compare_exchange_strong(expected, Writing, std::memory_order_acquire)) return &slot;
            }
        }
        return nullptr;
    }

    void workerLoop() {
        bool pending = false;
        Request current{};
        while (running_.load(std::memory_order_acquire)) {
            if (requests_.update()) {
                current = requests_.readBuffer();
                pending = true;
            }
            if (pending) {
                if (Slot* slot = acquireForWriting()) {
                    designer_.design(current, slot->kernel);
                    slot->request = current;
                    slot->state.store(Ready, std::memory_order_release);
                    pending = false;
                    continue; // une demande plus récente est peut-être déjà arrivée
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    std::array<Slot, 3> slots_;
    Designer designer_;
    bool background_ = false;
    Request lastRequest_{};
    TripleBuffer<Request> requests_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

} // namespace omnis::dsp
