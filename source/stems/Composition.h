// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/Composition.h — Qui est dans quel bus : la page COMPOSITION.
//
//  Ce que le DAW ne dit pas aux plug-ins, OMNIS le retrouve à l'écoute : un bus
//  reçoit la somme de ses pistes, donc son empreinte (énergie de six bandes
//  par trame, link/BandFrames) est une somme pondérée de celles de ses pistes.
//  Pour chaque bus, on cherche les gains positifs qui expliquent le mieux son
//  empreinte avec celles des pistes (moindres carrés à coefficients positifs,
//  chaque bande pesée par l'énergie du bus) : une piste fait partie du bus si
//  elle en explique une part nette (≥ 3 %) et si l'enlever dégrade nettement
//  l'explication. Une piste qui joue en même temps mais ailleurs n'explique
//  rien (son spectre et ses coups ne collent pas) ; et une piste vraiment dans
//  le bus n'y pèse jamais plus que le bus lui-même : si, une fois pesée, elle
//  le dépasse souvent, elle bouche seulement un trou (une piste du bus sans
//  OMNIS) et n'est pas retenue. Le gain trouvé est le volume de la piste dans le
//  bus (fader × envoi) : sous −14 dB, elle ne fait que combler un reste.
//  Chaque piste est aussi comparée décalée d'une trame dans chaque sens (latence
//  mal compensée jusqu'à ~43 ms).
//
//  Pour que ça marche : OMNIS PISTE en dernier sur la piste, OMNIS BUS en
//  premier sur le bus (avant ce qui change le son du bus).
// =============================================================================
#pragma once

#include "Element.h"
#include "link/LinkBus.h"

#include <string>
#include <vector>

namespace omnis::stems {

/// Une instance telle que la composition la voit (copie de link::InstanceView + trames).
struct Heard {
    int id = -1; ///< emplacement dans la région
    link::Role role = link::Role::None;
    std::string name; ///< nom de la piste dans le DAW
    link::Identity identity;
    std::vector<link::BandFrame> frames;
};

struct Member {
    int id = -1;
    std::string name;           ///< nom d'origine (DAW)
    Element element = Element::Auto; ///< ce qu'OMNIS en a déduit (écoute + nom)
    Element heard = Element::Auto;   ///< ce que l'écoute seule devine
    double confidence = 0.0;
    double secondsHeard = 0.0;
    bool nameDisagrees = false;
    double share = 0.0; ///< part du bus qu'elle explique (0 … 1)
    [[nodiscard]] Family family() const noexcept { return familyOf(element); }
};

struct BusView {
    int id = -1;
    std::string name;
    Family family = Family::Unknown; ///< d'après le nom du bus, sinon ses pistes, sinon l'écoute
    Element heard = Element::Auto;   ///< ce que l'écoute du bus devine (sans ses pistes)
    double secondsHeard = 0.0;
    bool kick = false, snare = false, hat = false, openHat = false; ///< entendus dans le bus (drums)
    std::vector<Member> members;     ///< ses pistes (part décroissante)
    double explained = 0.0;          ///< part de l'empreinte du bus expliquée par ses pistes
    bool enoughToMatch = false;      ///< assez de lecture commune pour retrouver ses pistes
};

struct Composition {
    std::vector<BusView> buses;
    std::vector<Member> direct; ///< pistes qui ne sont dans aucun bus (vers le master, ou bus sans OMNIS)
    [[nodiscard]] bool empty() const noexcept { return buses.empty() && direct.empty(); }
};

/// Réglages de la décision (exposés pour les tests).
struct MatchOptions {
    int minCommonFrames = 40; ///< ~1,7 s de lecture commune
    int maxFrames = 600;      ///< les ~26 dernières secondes
    double minShare = 0.03;
    double minTrackGain = 0.04; ///< gain en énergie (−14 dB) : en dessous, une piste ne fait que combler un reste
    double maxOvershoot = 0.15; ///< part des trames où la piste dépasserait le bus (impossible si elle y entre)
    double minGain = 0.001;   ///< perte d'explication (part de la bande du bus) quand on retire la piste
};

/// Les gains positifs (un par piste) qui expliquent l'empreinte du bus ; `share` : part expliquée par chacune.
/// Exposé pour les tests. Les trames sont alignées par leur numéro ; `common` : trames comparées.
struct BusFit {
    std::vector<double> gains, shares, removalLoss;
    std::vector<double> overshoot; ///< part des trames où gain × piste > bus (une vraie piste du bus : ~0)
    double explained = 0.0;
    int common = 0;
};
BusFit fitBus(const std::vector<link::BandFrame>& bus, const std::vector<const std::vector<link::BandFrame>*>& tracks, const MatchOptions& options = {});

Composition buildComposition(const std::vector<Heard>& instances, const MatchOptions& options = {});

/// Lit la région partagée (link::Observer) : pistes et bus vivants avec leurs trames.
std::vector<Heard> readInstances(const link::Observer& observer);

/// En clair, pour la barre IA : « DRUMS : « Kick Jayy 03 » → Kick ; … ». Vide si aucune instance.
std::string describeComposition(const Composition& c);
/// Une ligne par élément, « nom d'origine → ce qu'OMNIS devine (groupe, %) ».
std::string describeMember(const Member& m);

} // namespace omnis::stems
