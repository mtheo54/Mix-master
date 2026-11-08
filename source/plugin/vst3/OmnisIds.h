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

/// Interface : contrôleur -> processeur kFeedRequest ; le processeur répond par
/// kFeedMessage, attribut entier « token » (plugin/vst3/UiFeed.h).
inline constexpr const char* kFeedRequest = "omnis.getFeed";
inline constexpr const char* kFeedMessage = "omnis.feed";
inline constexpr const char* kFeedAttribute = "token";

} // namespace omnis::vst3
