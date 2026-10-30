// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/gui/OpenUrl.h — Ouvrir une page web dans le navigateur (le lien
//  d'une référence). À part : <windows.h> ne touche pas le reste de la console.
// =============================================================================
#pragma once

#include <string>

namespace omnis::vst3::gui {

/// Adresse https et caractères d'URL seulement ; sinon rien. Ne bloque pas.
bool openUrl(const std::string& url);

} // namespace omnis::vst3::gui
