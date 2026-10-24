// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisIds.h — Identifiants de classes VST3.
//
//  Ces UID identifient le plug-in dans les sessions des DAW : ne JAMAIS les
//  modifier après une première diffusion, sinon les projets existants ne
//  retrouveront plus OMNIS.
// =============================================================================
#pragma once

#include "pluginterfaces/base/funknown.h"

namespace omnis::vst3 {

static DECLARE_UID(kProcessorUID, 0x2E5FA3CD, 0xD9AA49CF, 0x933A79CD, 0x1EAA3F2B);
static DECLARE_UID(kControllerUID, 0x5C46817C, 0x5DCC4573, 0xB7F4FBA6, 0x3E122B5C);

/// Message contrôleur -> processeur : nom de la piste dans l'hôte (attribut
/// binaire « name », UTF-8). Sert à reconnaître l'élément d'un stem.
inline constexpr const char* kTrackNameMessage = "omnis.trackName";
inline constexpr const char* kTrackNameAttribute = "name";

/// Chef d'orchestre (master). Contrôleur -> processeur :
///  - kProposalsRequest : le processeur répond par kProposalsMessage, texte
///    UTF-8 (attribut binaire « text »), une ligne par proposition en attente
///    « P<tab>clé<tab>question », puis par retouche en place
///    « A<tab>clé<tab>stem<tab>sorte<tab>Hz<tab>dB » ;
///  - kAnswerMessage : réponse de la personne, attributs entiers « key » (clé
///    de la proposition) et « answer » (0 accepter, 1 refuser, 2 annuler).
inline constexpr const char* kProposalsRequest = "omnis.getProposals";
inline constexpr const char* kProposalsMessage = "omnis.proposals";
inline constexpr const char* kProposalsAttribute = "text";
inline constexpr const char* kAnswerMessage = "omnis.answer";
inline constexpr const char* kAnswerKeyAttribute = "key";
inline constexpr const char* kAnswerAttribute = "answer";

} // namespace omnis::vst3
