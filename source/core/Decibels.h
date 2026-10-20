// =============================================================================
//  Abrasion Labs // OMNIS
//  core/Decibels.h — Conversions amplitude <-> décibels.
// =============================================================================
#pragma once

#include <cmath>
#include <limits>

namespace omnis {

/// Plancher utilisé pour l'affichage et les conversions : -200 dB ~ 1e-10,
/// bien sous le bruit de quantification 32 bits flottants (~ -150 dBFS).
inline constexpr double kMinusInfinityDb = -200.0;

/// Gain linéaire -> dB (amplitude) : 20·log10(g).
inline double gainToDb(double gain, double floorDb = kMinusInfinityDb) noexcept {
    return gain > 0.0 ? std::fmax(20.0 * std::log10(gain), floorDb) : floorDb;
}

/// dB -> gain linéaire : 10^(dB/20). dbToGain(0.0) vaut exactement 1.0.
inline double dbToGain(double db) noexcept {
    return db <= kMinusInfinityDb ? 0.0 : std::pow(10.0, db * 0.05);
}

/// Puissance (carré moyen) -> dB : 10·log10(p).
inline double powerToDb(double power, double floorDb = kMinusInfinityDb) noexcept {
    return power > 0.0 ? std::fmax(10.0 * std::log10(power), floorDb) : floorDb;
}

inline double dbToPower(double db) noexcept {
    return db <= kMinusInfinityDb ? 0.0 : std::pow(10.0, db * 0.1);
}

} // namespace omnis
