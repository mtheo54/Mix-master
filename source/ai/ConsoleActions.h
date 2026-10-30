// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/ConsoleActions.h — Ce que la barre IA et le bouton AUTO font dans la
//  console : une demande comprise (ai::understandRequest) et ce qu'OMNIS a
//  entendu de la prod deviennent des réglages des modules, et une sonie à
//  viser (le contrôleur y amène le limiteur par la mesure, pas par calcul).
//
//  Ce qui agit :
//    - le Match EQ : la courbe d'intention (référence nommée, ou les mots) ;
//    - punch      -> Punch du limiteur ;
//    - densité    -> compression vers le haut et « glue » du multibande ;
//    - saturation -> Inflator (la 808 reste propre si on la protège) ;
//    - propreté   -> moins de clip et d'Inflator (ou plus, si on veut sale) ;
//    - largeur    -> Imager (médium-aigu et aigu, le bas jamais) ;
//    - sonie      -> la cible (LUFS) que le limiteur doit atteindre.
//  Les réglages partent de l'état courant : deux demandes successives
//  s'ajoutent (« plus de punch » deux fois = plus de punch encore).
// =============================================================================
#pragma once

#include "MatchIntent.h"

#include "params/Parameters.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace omnis::ai {

struct ConsolePlan {
    std::vector<std::pair<params::ParamId, double>> settings; ///< valeurs « plain »
    std::optional<double> targetLufs;                         ///< sonie à atteindre (limiteur)
    std::vector<std::string> said;                            ///< ce qui a été fait, en clair
};

/// `current` : valeur plain actuelle d'un paramètre (l'état de la console).
using CurrentValue = std::function<double(params::ParamId)>;

/// Une demande en texte libre, la prod telle qu'entendue.
ConsolePlan planRequest(const Understanding& understanding, const analysis::SpectralSignature& prod, const CurrentValue& current);

/// AUTO : le passage le plus fort de la prod, mené vers les titres mesurés du
/// style (genre de la base ; vide ou inconnu : toute la base mesurée).
ConsolePlan planAuto(const analysis::SpectralSignature& loudestPassage, const ReferenceLibrary& library, const std::string& style,
                     const CurrentValue& current);

/// AUTO vers des titres choisis (menu RÉFÉRENCE) ; `from` les nomme dans le compte rendu.
ConsolePlan planAuto(const analysis::SpectralSignature& loudestPassage, const std::vector<const ReferenceEntry*>& refs, const std::string& from,
                     const CurrentValue& current);

/// Une référence choisie au menu (style, artiste ou titre) : la courbe du Match EQ,
/// ce que sa fiche complète apprend (punch, grave mono, sections, écrêtage) et sa sonie.
ConsolePlan planReference(const analysis::SpectralSignature& prod, const std::vector<const ReferenceEntry*>& refs, const std::string& name,
                          const CurrentValue& current);

/// Glue du multibande (0 … 1) -> réglages des quatre bandes (même règle que le gros bouton du rack).
std::vector<std::pair<params::ParamId, double>> glueSettings(double amount);
double glueOf(const CurrentValue& current);

} // namespace omnis::ai
