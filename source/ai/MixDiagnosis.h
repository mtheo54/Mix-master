// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/MixDiagnosis.h — Diagnostic du mix : ce qu'OMNIS entend dans la prod
//  (avant ses modules) et ce qu'il faut corriger DANS LE MIX (projet FL), en
//  clair. Comparé à une référence (titre, style) ou, sans elle, à la moyenne des
//  sons mesurés de la base.
//
//  Ce que le master ne rattrape pas bien : grave pas mono, mix qui clippe déjà,
//  limiteur oublié sur le master, bas-médium qui s'accumule, kick et 808 qui se
//  masquent, aigus durs… La 808 hors de la gamme n'est qu'une remarque (c'est
//  parfois voulu : dissonance, côté sale).
// =============================================================================
#pragma once

#include "analysis/SpectralSignature.h"

#include <string>
#include <vector>

namespace omnis::ai {

struct MixFinding {
    enum class Level { Info = 0, Advice = 1, Important = 2 };
    Level level = Level::Advice;
    std::string problem; ///< ce qui est entendu (« le grave n'est pas mono »)
    std::string fix;     ///< quoi faire dans le mix
};

/// `reference` : signature visée (titre, style) ; nullptr : la moyenne de la base.
std::vector<MixFinding> diagnoseMix(const analysis::SpectralSignature& prod, const analysis::SpectralSignature* reference);

/// Texte prêt à afficher, les plus importants d'abord.
std::string formatDiagnosis(const std::vector<MixFinding>& findings);

} // namespace omnis::ai
