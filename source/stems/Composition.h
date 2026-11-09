// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/Composition.h — Tes bus et ce qu'il y a dedans : la page COMPOSITION.
//
//  Chaque OMNIS posé sur un bus (RÔLE : Bus) écoute ce bus (stems/BusListener)
//  et publie son nom et ce qu'il y entend (link/LinkBus). Toutes les instances
//  (le master comme les bus) lisent la liste : le nom du bus dans le DAW, son
//  groupe (DRUMS, 808 / BASSE, MÉLO, VOIX, EFX), et chaque élément entendu
//  dedans. Rien n'est posé sur les pistes, le mixage n'est pas touché.
// =============================================================================
#pragma once

#include "BusListener.h"
#include "Element.h"
#include "link/LinkBus.h"

#include <string>
#include <vector>

namespace omnis::stems {

struct BusView {
    int id = -1;
    std::string name;                ///< nom du bus dans le DAW
    Family family = Family::Unknown; ///< d'après son nom, sinon ce qu'il contient
    BusContent content;              ///< ce qu'OMNIS y entend
    [[nodiscard]] bool listening() const noexcept { return content.secondsHeard < BusListener::kMinSeconds; }
};

struct Composition {
    std::vector<BusView> buses; ///< dans l'ordre des groupes, puis des noms
    [[nodiscard]] bool empty() const noexcept { return buses.empty(); }
};

Composition buildComposition(const std::vector<link::InstanceView>& instances);

/// Ce que le bus a écouté du morceau : « morceau écouté : 2:27 » (vide : rien encore).
std::string describeListened(const BusView& b);

/// En clair, pour la barre IA : « BUS « DRUMS » (DRUMS) : Kick (24), Snare / clap (23)… ». Vide sans bus.
std::string describeComposition(const Composition& c);

} // namespace omnis::stems
