// =============================================================================
//  Abrasion Labs // OMNIS
//  core/TripleBuffer.h — Échange atomique d'un état complet entre deux threads.
//
//  C'est la forme correcte et wait-free du « double buffering atomique » : avec
//  deux tampons seulement, le rédacteur devrait attendre que le lecteur ait fini
//  de lire avant de réécrire. Avec trois tampons, aucun des deux n'attend jamais :
//
//     [back]  : propriété exclusive du rédacteur (thread IA / analyse)
//     [middle]: dernier état publié, en transit ; index stocké dans un atomique
//     [front] : propriété exclusive du lecteur (thread audio)
//
//  publish() échange back <-> middle et lève le bit « dirty ».
//  update()  échange front <-> middle si dirty, en une seule opération atomique.
//
//  Usage prévu : noyaux FIR du Match EQ (16384 taps), courbes cibles, jeux de
//  paramètres prédits par l'IA. Les objets T sont préalloués : publier ne fait
//  que permuter des index, donc zéro allocation et zéro copie côté audio.
//
//  Attention : après publish(), writeBuffer() désigne un tampon contenant un
//  état ancien. Le rédacteur doit le réécrire intégralement avant de publier.
// =============================================================================
#pragma once

#include "Platform.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace omnis {

template <typename T>
class TripleBuffer {
public:
    TripleBuffer() = default;

    /// Initialise les trois tampons avec la même valeur (non temps réel).
    explicit TripleBuffer(const T& initial) : buffers_{initial, initial, initial} {}

    TripleBuffer(const TripleBuffer&) = delete;
    TripleBuffer& operator=(const TripleBuffer&) = delete;

    // ---- Côté rédacteur ----------------------------------------------------
    T& writeBuffer() noexcept { return buffers_[back_]; }

    void publish() noexcept {
        const std::uint8_t previous = middle_.exchange(static_cast<std::uint8_t>(back_ | kDirtyBit),
                                                       std::memory_order_acq_rel);
        back_ = static_cast<std::uint8_t>(previous & kIndexMask);
    }

    // ---- Côté lecteur ------------------------------------------------------

    /// Récupère le dernier état publié s'il y en a un. Renvoie true si changé.
    bool update() noexcept {
        if ((middle_.load(std::memory_order_relaxed) & kDirtyBit) == 0)
            return false;
        const std::uint8_t previous = middle_.exchange(front_, std::memory_order_acq_rel);
        front_ = static_cast<std::uint8_t>(previous & kIndexMask);
        return true;
    }

    const T& readBuffer() const noexcept { return buffers_[front_]; }
    T& readBuffer() noexcept { return buffers_[front_]; }

private:
    static constexpr std::uint8_t kIndexMask = 0x3;
    static constexpr std::uint8_t kDirtyBit = 0x4;

    std::array<T, 3> buffers_{};
    alignas(kCacheLineSize) std::uint8_t back_ = 0;                 // rédacteur
    alignas(kCacheLineSize) std::atomic<std::uint8_t> middle_{1};   // partagé
    alignas(kCacheLineSize) std::uint8_t front_ = 2;                // lecteur
};

} // namespace omnis
