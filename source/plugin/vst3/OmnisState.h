// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisState.h — Format binaire de l'état du processeur.
//
//  Partagé par le processeur (getState/setState) et le contrôleur
//  (setComponentState). Petit-boutiste, versionné, extensible :
//
//      uint32  magic    = 'OMNS'
//      uint32  version  = 1
//      uint32  count
//      count × { uint32 paramId ; float64 valeurNormalisée }
//
//  Les identifiants inconnus sont ignorés et les paramètres absents gardent
//  leur valeur par défaut : un preset créé avec une version antérieure (moins
//  de paramètres) ou postérieure (plus de paramètres) se charge sans erreur.
// =============================================================================
#pragma once

#include "pluginterfaces/base/ibstream.h"

#include <cstdint>
#include <functional>
#include <string>

namespace omnis::vst3 {

inline constexpr std::uint32_t kStateMagic = 0x534E4D4F; // "OMNS" en petit-boutiste
inline constexpr std::uint32_t kStateVersion = 1;

/// Écrit tous les paramètres persistants. `getNormalized(id)` fournit la valeur.
bool writeState(Steinberg::IBStream* stream, const std::function<double(std::uint32_t)>& getNormalized);

/// Lit un état ; `setNormalized(id, value)` est appelée pour chaque paramètre
/// connu et persistant. Renvoie false si le flux n'est pas un état OMNIS.
bool readState(Steinberg::IBStream* stream, const std::function<void(std::uint32_t, double)>& setNormalized);

/// Données en plus des paramètres, écrites APRÈS eux (un lecteur plus ancien
/// s'arrête aux paramètres et les ignore) : uint32 'OMRF', uint32 longueur,
/// texte (signatures de la dernière référence écoutée, format de la base).
inline constexpr std::uint32_t kExtrasMagic = 0x46524D4F; // "OMRF"
bool writeExtras(Steinberg::IBStream* stream, const std::string& text);
/// À appeler juste après readState() ; false si le flux n'en contient pas.
bool readExtras(Steinberg::IBStream* stream, std::string& text);

} // namespace omnis::vst3
