// =============================================================================
//  Abrasion Labs // OMNIS
//  core/Platform.h — Détection plateforme, alignement, garde anti-dénormaux.
//
//  Règle temps réel : rien ici n'alloue ni ne verrouille. Tout est inline.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    #include <immintrin.h>
    #define OMNIS_ARCH_X86 1
#else
    #define OMNIS_ARCH_X86 0
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    #define OMNIS_ARCH_ARM64 1
#else
    #define OMNIS_ARCH_ARM64 0
#endif

#if defined(_MSC_VER)
    #define OMNIS_FORCE_INLINE __forceinline
    #define OMNIS_RESTRICT __restrict
#else
    #define OMNIS_FORCE_INLINE inline __attribute__((always_inline))
    #define OMNIS_RESTRICT __restrict__
#endif

namespace omnis {

/// Taille de ligne de cache supposée (x86-64 et Apple Silicon : 64 / 128 octets).
/// On prend 128 pour éviter le faux partage aussi sur M1/M2 (lignes L2 de 128 o).
inline constexpr std::size_t kCacheLineSize = 128;

/// Alignement des buffers audio : 64 octets = un registre AVX-512 complet,
/// donc a fortiori correct pour AVX2 (32 o) et NEON (16 o).
inline constexpr std::size_t kSimdAlignment = 64;

/// Nombre de canaux traités par le moteur (mastering stéréo uniquement).
inline constexpr int kNumChannels = 2;

// -----------------------------------------------------------------------------
//  ScopedNoDenormals
//
//  Les nombres dénormalisés (|x| < 2.2e-308 en double) sont traités en
//  micro-code sur x86 : une queue de réverbération ou un filtre IIR qui décroît
//  vers zéro peut multiplier le coût CPU par 100. On force donc :
//    - x86  : MXCSR.FTZ (bit 15, flush-to-zero sur les résultats)
//             MXCSR.DAZ (bit 6,  denormals-are-zero sur les entrées)
//    - ARM64: FPCR.FZ  (bit 24)
//  L'état précédent est restauré à la destruction : on ne modifie jamais
//  durablement l'environnement flottant du thread de l'hôte.
// -----------------------------------------------------------------------------
class ScopedNoDenormals {
public:
    ScopedNoDenormals() noexcept {
#if OMNIS_ARCH_X86
        previous_ = _mm_getcsr();
        _mm_setcsr(previous_ | 0x8040u);
#elif OMNIS_ARCH_ARM64 && !defined(_MSC_VER)
        std::uint64_t fpcr;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        previous_ = fpcr;
        fpcr |= (1ull << 24);
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif
    }

    ~ScopedNoDenormals() noexcept {
#if OMNIS_ARCH_X86
        _mm_setcsr(static_cast<unsigned int>(previous_));
#elif OMNIS_ARCH_ARM64 && !defined(_MSC_VER)
        __asm__ __volatile__("msr fpcr, %0" : : "r"(previous_));
#endif
    }

    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
#if OMNIS_ARCH_X86
    unsigned int previous_ = 0;
#else
    std::uint64_t previous_ = 0;
#endif
};

} // namespace omnis
