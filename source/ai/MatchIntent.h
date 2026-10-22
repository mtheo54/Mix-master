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
//      après les bornes automatiques — la personne a le dernier mot. Une
//      région protégée (« garde ma 808 ») ne bouge pas de plus de ±1 dB.
//   6. Sonie : explicite, sinon celle de la référence ; « fort » ne la
//      dépasse pas (c'est ce que veut dire « fort comme la réf »), « club »
//      la dépasse d'un demi-LU par LU au-delà ; « dynamique » la réduit.
// =============================================================================
#pragma once

#include "Lexicon.h"
#include "ReferenceLibrary.h"

#include "analysis/SpectralSignature.h"
#include "stages/MatchKernelBank.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omnis::ai {

using analysis::BandArray;
using analysis::SpectralSignature;

/// Régions du Match EQ (mêmes bornes que les axes Sub … Air du lexique).
inline constexpr int kRegionCount = kTonalAxes;
int regionOfBand(int band) noexcept;

/// Ce que la demande de la personne fixe.
struct MatchRequest {
    double resemblance = 0.5;           ///< fraction de l'écart refermée (0.2..0.8)
    std::optional<double> targetLufs;   ///< sonie explicite (« -8 lufs », « spotify »)
    double loudnessOffsetLu = 0.0;      ///< « fort », « club », « dynamique »…
    double maxTonalDeviationDb = 1.5;   ///< tolérance de la dynamique vis-à-vis de l'intention
    double subLimitDb = 3.0;            ///< borne du déplacement sous 100 Hz
    std::array<bool, kRegionCount> protectedRegions{}; ///< « garde ma 808 » : ±1 dB
    bool widthProtected = false;
    BandArray midBiasDb{};              ///< souhaits explicites, ajoutés après les bornes
    BandArray sideBiasDb{};
    std::vector<std::string> understood; ///< ce qui a été compris (affichable)
    std::vector<std::string> pending;    ///< compris, appliqué par un étage à venir
};

/// Traduit l'interprétation d'une phrase (ai/Lexicon.h) en demande.
MatchRequest requestFromInterpretation(const Interpretation& interpretation);

/// Raccourci : lexique livré, sans base de références.
MatchRequest parseRequest(std::string_view text);

/// La demande complète : mots, référence nommée, question si besoin.
struct Understanding {
    Interpretation interpretation;
    MatchRequest request;
    std::vector<const ReferenceEntry*> referenceEntries; ///< résolues dans la base
    std::string referenceName;                           ///< « artiste 2hollis (1 titre) »
    std::vector<std::string> unknownReferences;          ///< nommées mais inconnues
    std::optional<std::string> question;                 ///< à poser avant d'agir

    [[nodiscard]] bool hasReference() const noexcept { return !referenceEntries.empty(); }
};

/// `audioReferenceProvided` : la personne a déjà glissé un son de référence.
/// Artiste inconnu sans son -> question (artiste proche connu, ou un son).
Understanding understandRequest(std::string_view text, const Lexicon& lexicon, const ReferenceLibrary& library,
                                bool audioReferenceProvided);

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

/// Sans référence : seuls les souhaits explicites, sonie de base −10 LUFS
/// (± « fort », « dynamique »…).
MatchIntent planFromWords(const SpectralSignature& prod, const MatchRequest& request);

/// Distance tonale RMS (dB) entre deux signatures sur les nœuds significatifs :
/// mesure de la « ressemblance » avant / après.
double tonalDistanceDb(const SpectralSignature& a, const SpectralSignature& b);

} // namespace omnis::ai
