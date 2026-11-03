// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/ReferenceDynamics.h — Topologie des étages non linéaires et constantes
//  de temps déduites de la MESURE : l'écart entre la prod entendue et la
//  référence choisie (signature spectrale + fiche musicale). Aucune table par
//  genre : deux références aux mesures opposées donnent deux réglages opposés,
//  quel que soit leur style.
//
//  Ce que ça règle (paramètres existants, rien de nouveau) :
//    sub_phase                     PLR / sonie de la réf (écrêtée -> Linear) ;
//    clip_mode, clip_knee          « mur » de la réf (PLR) et écart de PLR à combler ;
//    clip_sub_room, clip_sub_drive part du sub de la prod, écart de sub ;
//    mb_<bande>_thr/ratio/makeup   dynamique des 21 nœuds regroupés par bande ;
//    mb_<bande>_attack             micro-dynamique (crête 10 ms − niveau 400 ms) par zone ;
//    mb_<bande>_release, lim_release  sous-division du tempo de la prod ;
//    lim_auto_release              « pompe » de la réf / sub tenu de la réf ;
//    infl_low, infl_curve          808 qui plafonne la prod face à une réf dense ;
//    infl_warmth                   bas-médium de la réf plus dense et plus stable ;
//    lim_unlink_tr                 largeur et dynamique des côtés dans l'aigu.
//
//  Toutes les règles sont des rampes continues entre deux points d'ancrage
//  (les seuils de la spécification), sauf les choix discrets (sub_phase,
//  clip_mode) qui changent au bout de leur rampe. Calibration des ancrages sur
//  la base mesurée (429 titres) : docs/RAPPORT_TECHNIQUE.md §8.7.
// =============================================================================
#pragma once

#include "ConsoleActions.h"

#include "analysis/SpectralSignature.h"

#include <array>

namespace omnis::ai {

/// Les grandeurs intermédiaires (pour le compte rendu et les tests).
struct ReferenceMeasures {
    double refPlr = 0.0, prodPlr = 0.0, plrGap = 0.0; ///< plrGap = prod − réf (dB)
    double refLufs = -70.0;
    double wall = 0.0;          ///< 0 (réf aérée, PLR ≥ 8,5) … 1 (mur, PLR ≤ 6,5)
    double gapToClose = 0.0;    ///< 0 … 1 : écart de PLR à combler (1 dB -> 0, 3 dB -> 1)
    double prodSubShare = 0.0;  ///< part de l'énergie Mid sous la coupure du clipper (prod)
    double refSubShare = 0.0;
    double subGapDb = 0.0;      ///< part du sub, prod − réf (dB)
    std::array<double, params::kMbBands> prodSpread{}, refSpread{}; ///< P90 − P10 par bande (dB)
    std::array<double, params::kMbBands> attackGap{};                ///< micro-dynamique réf − prod (dB)
    double pump = 0.0;          ///< 0 … 1 : la réf varie vite mais ses sections sont plates
    double subHold = 0.0;       ///< 0 … 1 : le sub de la réf est tenu (faible dynamique, note tenue)
    double inflate808 = 0.0;    ///< 0 … 1 : la 808 de la prod plafonne face à une réf dense
    double warmth = 0.0;        ///< 0 … 1
    double sideWidth = 0.0;     ///< 0 … 1 : côtés larges et dynamiques dans l'aigu (réf)
};

/// Mesures de l'écart. `ref` est une signature agrégée (aggregateSignature) ou
/// celle d'un titre ; `refMusic` sa fiche (moyenne si plusieurs titres).
ReferenceMeasures measureReferenceGap(const analysis::SpectralSignature& prod, const analysis::SpectralSignature& ref,
                                      const analysis::MusicDescriptors& refMusic, const CurrentValue& current);

/// Écrit les réglages déduits dans le plan (valeurs absolues : rechoisir une
/// référence ne cumule pas). Rien n'est écrit si la prod n'a pas été entendue.
void applyReferenceDynamics(ConsolePlan& plan, const analysis::SpectralSignature& prod, const analysis::SpectralSignature& ref,
                            const analysis::MusicDescriptors& refMusic, const CurrentValue& current);

} // namespace omnis::ai
