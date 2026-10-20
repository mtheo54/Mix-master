// =============================================================================
//  Abrasion Labs // OMNIS
//  core/SpscRingBuffer.h — File circulaire sans verrou, 1 producteur / 1 consommateur.
//
//  Usage prévu : le thread audio (producteur) pousse des snapshots d'échantillons
//  vers le thread d'analyse / IA (consommateur) sans jamais bloquer.
//
//  Garanties :
//   - push()/pop() sont wait-free, sans allocation, O(n) en copie.
//   - Capacité arrondie à une puissance de deux -> index masqués, pas de modulo.
//   - Ordonnancement mémoire : le producteur publie `head_` en release après
//     avoir écrit les données ; le consommateur lit `head_` en acquire avant de
//     lire les données (et symétriquement pour `tail_`). C'est le minimum
//     nécessaire et suffisant du modèle mémoire C++ pour ce motif.
//   - `head_` et `tail_` sont sur des lignes de cache distinctes (pas de faux
//     partage) et chaque côté garde une copie locale de l'index adverse pour
//     limiter le trafic de cohérence.
//   - Si la file est pleine, push() n'écrit que ce qui rentre et renvoie ce
//     nombre : le thread audio perd des données d'analyse plutôt que d'attendre.
//
//  allocate() doit être appelée hors thread audio, avant tout push/pop.
// =============================================================================
#pragma once

#include "Platform.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace omnis {

template <typename T>
class SpscRingBuffer {
    static_assert(std::is_trivially_copyable_v<T>, "SpscRingBuffer<T> exige un T trivialement copiable");

public:
    SpscRingBuffer() = default;
    explicit SpscRingBuffer(std::size_t minCapacity) { allocate(minCapacity); }

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    /// Non temps réel. Capacité effective = puissance de deux >= minCapacity.
    void allocate(std::size_t minCapacity) {
        std::size_t capacity = 1;
        while (capacity < minCapacity) capacity <<= 1;
        storage_ = std::make_unique<T[]>(capacity);
        capacity_ = capacity;
        mask_ = capacity - 1;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        cachedHead_ = 0;
        cachedTail_ = 0;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    // ---- Côté producteur ---------------------------------------------------

    /// Nombre d'éléments pouvant être poussés sans écraser de données.
    [[nodiscard]] std::size_t freeSpace() noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        cachedTail_ = tail_.load(std::memory_order_acquire);
        return capacity_ - (head - cachedTail_);
    }

    /// Pousse jusqu'à `count` éléments. Renvoie le nombre réellement écrit.
    std::size_t push(const T* data, std::size_t count) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t available = capacity_ - (head - cachedTail_);
        if (available < count) {
            cachedTail_ = tail_.load(std::memory_order_acquire);
            available = capacity_ - (head - cachedTail_);
        }
        const std::size_t n = std::min(count, available);
        const std::size_t start = head & mask_;
        const std::size_t firstPart = std::min(n, capacity_ - start);
        std::copy_n(data, firstPart, storage_.get() + start);
        std::copy_n(data + firstPart, n - firstPart, storage_.get());
        head_.store(head + n, std::memory_order_release);
        return n;
    }

    bool push(const T& value) noexcept { return push(&value, 1) == 1; }

    // ---- Côté consommateur -------------------------------------------------

    [[nodiscard]] std::size_t available() noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        cachedHead_ = head_.load(std::memory_order_acquire);
        return cachedHead_ - tail;
    }

    /// Retire jusqu'à `count` éléments. Renvoie le nombre réellement lu.
    std::size_t pop(T* dst, std::size_t count) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        std::size_t ready = cachedHead_ - tail;
        if (ready < count) {
            cachedHead_ = head_.load(std::memory_order_acquire);
            ready = cachedHead_ - tail;
        }
        const std::size_t n = std::min(count, ready);
        const std::size_t start = tail & mask_;
        const std::size_t firstPart = std::min(n, capacity_ - start);
        std::copy_n(storage_.get() + start, firstPart, dst);
        std::copy_n(storage_.get(), n - firstPart, dst + firstPart);
        tail_.store(tail + n, std::memory_order_release);
        return n;
    }

    bool pop(T& value) noexcept { return pop(&value, 1) == 1; }

private:
    std::unique_ptr<T[]> storage_;
    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;

    // Index monotones (le débordement de size_t est bien défini et les
    // différences restent correctes en arithmétique modulaire).
    alignas(kCacheLineSize) std::atomic<std::size_t> head_{0}; // écrit par le producteur
    std::size_t cachedTail_ = 0;                               // copie locale producteur
    alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0}; // écrit par le consommateur
    std::size_t cachedHead_ = 0;                               // copie locale consommateur
};

} // namespace omnis
