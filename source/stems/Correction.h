// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/Correction.h — Une retouche que le master demande à un stem.
//
//  Le master (stems/Conductor) propose ; la personne accepte ; la retouche part
//  au stem par la mémoire partagée (link::StemOrders) et s'ajoute à sa chaîne,
//  sans latence (stems/StemChain). Rien n'est appliqué sans accord.
// =============================================================================
#pragma once

#include <array>

namespace omnis::stems {

struct Correction {
    enum class Type : int {
        None = 0,
        LowCut = 1,   ///< passe-haut 12 dB/oct à `hz`
        Bell = 2,     ///< cloche `gainDb` à `hz`, facteur `q`
        LowShelf = 3, ///< plateau grave `gainDb` sous `hz`
    };
    Type type = Type::None;
    float hz = 0.0f;
    float gainDb = 0.0f;
    float q = 0.7071f;

    friend bool operator==(const Correction&, const Correction&) = default;
};

/// Retouches qu'un stem peut porter en même temps.
inline constexpr int kMaxCorrections = 4;
using Corrections = std::array<Correction, kMaxCorrections>;

/// Bornes fixes : passe-haut 20 … 300 Hz, cloche et plateau −6 … +3 dB,
/// fréquences 20 Hz … 16 kHz, q 0,3 … 4. Valeurs non finies : retouche retirée.
Correction clampCorrection(Correction c) noexcept;

[[nodiscard]] inline bool anyCorrection(const Corrections& c) noexcept {
    for (const auto& x : c)
        if (x.type != Correction::Type::None) return true;
    return false;
}

} // namespace omnis::stems
