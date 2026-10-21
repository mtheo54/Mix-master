// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/MasteringChain.h — Construction de la chaîne de mastering complète.
//
//  Point unique où l'ordre des étages est défini : le plug-in VST3 et l'outil de
//  rendu hors ligne appellent cette fonction, ils entendent donc la même chaîne.
//  Les étages des modules suivants s'ajoutent ici, dans l'ordre de
//  docs/ARCHITECTURE.md §2.
// =============================================================================
#pragma once

#include "Engine.h"

namespace omnis {

/// Non temps réel, avant Engine::prepare().
void buildMasteringChain(Engine& engine);

} // namespace omnis
