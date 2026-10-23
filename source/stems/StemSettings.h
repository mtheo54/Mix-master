// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/StemSettings.h — Ce que la chaîne d'un stem applique, selon l'élément.
//
//  Une famille, une chaîne :
//    drums      : passe-haut, EQ dynamique, compression, saturation (courbe de
//                 l'Inflator), espace M/S ;
//    808 / basse : passe-haut anti-grondement, saturation, EQ dynamique, mono
//                 dans le grave ;
//    instruments : passe-haut doux, EQ dynamique (boue, carton, dureté), mono
//                 sous ~120 Hz, largeur ;
//    voix       : passe-haut, EQ dynamique (boue, dureté, sifflantes),
//                 compression, au centre.
//  Le mix/master fait le reste (chef d'orchestre).
//
//  Trois modes : Off (rien, au bit près), Auto (OMNIS règle à partir de
//  l'élément et de ce qu'il a mesuré du son), Manuel (la personne règle les
//  doses ; les fréquences restent celles de l'élément).
//
//  Ce que l'écoute a mesuré est respecté : un son déjà écrasé (crête basse)
//  reçoit moins de saturation et de compression, une piste déjà large n'est
//  pas élargie, un instrument qui joue vraiment dans le grave n'a pas de
//  passe-haut haut placé. Bornes fixes quoi qu'il arrive (« sans partir dans
//  l'absurde ») : passe-haut ≤ 500 Hz, coupe d'EQ dynamique ≤ 6 dB, saturation
//  ≤ 50 %, compression ≤ 4:1 et ≤ 8 dB, largeur 0 … 150 %.
//
//  Les valeurs des modèles sont des points de départ : à calibrer sur de vrais
//  stems.
// =============================================================================
#pragma once

#include "Element.h"
#include "ElementClassifier.h"

#include <array>

namespace omnis::stems {

/// Une bande d'EQ dynamique. Son équilibre : l'énergie autour de `hz`
/// (passe-bande de facteur `q`) rapportée à celle du reste de la piste. Elle
/// coupe quand cet équilibre dépasse, à cet instant, celui d'un bruit rose de
/// plus de `allowDb` (la zone prend trop de place), au plus de `maxCutDb`.
/// Au-delà du seuil : 2:1.
struct DynamicBand {
    double hz = 1000.0;
    double q = 1.0;
    double allowDb = 0.0;  ///< équilibre permis, en dB au-dessus de celui d'un bruit rose
    double maxCutDb = 0.0; ///< 0 : bande éteinte
};

/// Équilibre (dB) bande / reste d'un bruit rose 20 Hz … 20 kHz pour un
/// passe-bande SVF de facteur q (largeur équivalente π·f/(2q), énergie par
/// octave constante) : −5,2 dB à Q 1.
double pinkBalanceDb(double q) noexcept;

inline constexpr int kDynamicBands = 3;

struct StemSettings {
    bool active = false;   ///< false : la chaîne laisse passer le son au bit près
    double trimDb = 0.0;
    double hpfHz = 0.0;    ///< 0 : pas de passe-haut
    bool hpfSteep = false; ///< 24 dB/oct (sinon 12)
    std::array<DynamicBand, kDynamicBands> dynamic{};
    double compRatio = 1.0;       ///< 1 : pas de compression
    double compDepthDb = 0.0;     ///< seuil sous la crête récente
    double compAttackMs = 10.0, compReleaseMs = 100.0;
    double saturation = 0.0;      ///< dose de la courbe de l'Inflator (0 … 0.5)
    double monoBelowHz = 0.0;     ///< 0 : pas de mono dans le grave
    double monoAmount = 0.0;      ///< 0 … 1
    double width = 1.0;           ///< Side au-dessus du grave (0 … 1.5)
};

enum class StemMode : int { Off = 0, Auto = 1, Manual = 2 };

/// Doses réglées par la personne en mode Manuel (paramètres stem_*).
struct StemMacros {
    double hpfHz = 20.0;   ///< 10 … 500 (10 = éteint)
    double comp = 0.0;     ///< 0 … 1
    double saturation = 0.0;
    double clean = 0.0;    ///< EQ dynamique, 0 … 1
    double width = 1.0;    ///< 0 … 2 (borné à 1.5)
    double trimDb = 0.0;
};

/// Modèle de l'élément (ce qu'un ingénieur ferait sur ce genre de son, à dose moyenne).
StemSettings templateFor(Element element) noexcept;

/// Réglages appliqués. `character` : ce que l'écoute a mesuré (secondsHeard > 0) ;
/// sans écoute, le modèle seul (Auto) est appliqué tel quel.
StemSettings settingsFor(StemMode mode, Element element, const Character& character, double secondsHeard,
                         const StemMacros& macros) noexcept;

/// Applique les bornes fixes (toujours appelée par settingsFor).
StemSettings clampSettings(StemSettings s) noexcept;

} // namespace omnis::stems
