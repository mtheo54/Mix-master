// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/MatchIntent.h — De la référence à une intention : ressembler, pas copier.
//
//  Entrées : la signature de la PROD et celle de la RÉFÉRENCE citée dans la
//  demande (analysis/SpectralSignature.h). Sortie : la courbe du Match EQ
//  (Mid + Side), la sonie visée, et une explication lisible.
//
//  Règles (précurseur à base de règles du module 5) :
//   1. Écart tonal = balance relative de la référence − celle de la prod, les
//      deux ramenées à leur « corps » (200 Hz – 5 kHz) : on compare des
//      équilibres, pas des volumes. Lissé une fois ([1 2 1]/4) : seules les
//      tendances larges, persistantes, sont retenues.
//   2. Ressemblance r (0..1, défaut 0.5) : on ne referme qu'une FRACTION de
//      l'écart. r = 1 serait le copier-coller, jamais proposé (borne 0.85).
//   3. Protection de l'identité de la prod : chaque nœud est borné
//      (sub ≤ 100 Hz : ±3 dB ; ailleurs −6 / +4 dB : couper est plus sûr que
//      booster). On ne booste jamais une bande quasi vide de la prod (plus de
//      40 dB sous son corps) : il n'y a rien à faire ressortir, seulement du
//      bruit.
//   4. Largeur : même logique sur l'écart Side − Mid, au-dessus du Mono Maker
//      seulement, ±4 dB ; pas d'élargissement d'une bande quasi mono.
//   5. Demande explicite (« garde ma 808 », « plus brillant »…) : appliquée
//      après les bornes automatiques — la personne a le dernier mot.
// =============================================================================
#pragma once

#include "analysis/SpectralSignature.h"
#include "stages/MatchKernelBank.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omnis::ai {

using analysis::BandArray;
using analysis::SpectralSignature;

/// Ce que la demande de la personne fixe (ai/MatchIntent.cpp : parseRequest).
struct MatchRequest {
    double resemblance = 0.5;           ///< fraction de l'écart refermée (0..0.85)
    std::optional<double> targetLufs;   ///< sonie explicite ; sinon celle de la référence
    double maxTonalDeviationDb = 1.5;   ///< tolérance de la dynamique vis-à-vis de l'intention
    double subLimitDb = 3.0;            ///< borne du déplacement sous 100 Hz
    BandArray midBiasDb{};              ///< souhaits explicites, ajoutés après les bornes
    BandArray sideBiasDb{};
    std::vector<std::string> understood; ///< ce qui a été compris (affichable)
};

/// Lecture « au plus simple » d'une demande en français (mots-clés) : « comme
/// la réf mais garde ma 808 », « très fort », « un peu plus brillant », « −8
/// LUFS », « propre »… Le module 5 remplacera cette lecture par des embeddings.
MatchRequest parseRequest(std::string_view text);

struct MatchIntent {
    MatchCurve midDb{};  ///< courbe Mid à écrire dans le Match EQ
    MatchCurve sideDb{}; ///< courbe Side
    BandArray gapDb{};   ///< écart tonal brut (lissé) référence − prod
    BandArray widthGapDb{};
    double targetLufs = -14.0;
    std::vector<std::string> referenceTraits; ///< pourquoi la référence sonne ainsi
    std::vector<std::string> protectedTraits; ///< ce que l'on garde de la prod
};

MatchIntent planMatch(const SpectralSignature& prod, const SpectralSignature& reference, const MatchRequest& request);

/// Distance tonale RMS (dB) entre deux signatures sur les nœuds significatifs :
/// mesure de la « ressemblance » avant / après.
double tonalDistanceDb(const SpectralSignature& a, const SpectralSignature& b);

} // namespace omnis::ai
