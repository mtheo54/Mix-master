// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/UsageGraph.h — Ce que la personne fait le plus, et si c'est utile.
//
//  Une SÉANCE commence quand OMNIS propose des réglages : AUTO, ou un preset
//  (une référence ou un style choisi au menu RÉF). Elle finit à la proposition
//  suivante, à l'export, à « annule » ou à la fermeture. On compare alors ce
//  qui a été proposé à ce que la personne a gardé : chaque différence est un
//  GESTE (« plus de sub », « Comp allumé », « Inflator → Lampe »).
//
//  Avant d'apprendre un geste, OMNIS le JUGE avec ses connaissances de base de
//  mastering (judgeSession) :
//    - inaudible : trop petit pour s'entendre (moins de 0,5 dB de sub…) ;
//    - hésitation : beaucoup bougé pour revenir presque au point de départ ;
//    - à risque : va contre les règles du métier (grave élargi, sub trop
//      poussé, volume écrasé, plus de limiteur…) : jamais appris, signalé ;
//    - annulé : la proposition a été défaite tout de suite ;
//    - utile : le reste ; gardé jusqu'à l'export, il compte double.
//  Seuls les gestes utiles nourrissent le réseau des habitudes (HabitLearning).
//
//  Le GRAPHE (sur le poste, apprentissage/graphe.tsv, des nombres et des noms
//  de réglages seulement) : des nœuds « contexte » (AUTO · trap, preset ·
//  style drill…) et « geste », des liens contexte → geste et geste ↔ geste
//  (faits dans la même séance). Une HABITUDE est un geste répété (3 séances
//  au moins) et utile la plupart du temps (60 %) ; ce qui n'a servi qu'une
//  fois il y a longtemps est oublié. Export anonyme en JSON pour la page de
//  données (aucun envoi).
// =============================================================================
#pragma once

#include "HabitLearning.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omnis::ai {

/// Les réglages clés qu'une personne retouche après une proposition.
struct UsageState {
    HabitVector habits{};             ///< pente, sub, Inflator, volume, largeur (médium), seuil du Comp (voir HabitLearning)
    std::array<bool, 8> modules{};    ///< Sub, Match EQ, Inflator, Imager, Multibande, Clipper, Limiteur, Comp (allumés)
    int inflCharacter = 0, compCharacter = 2, matchCharacter = 0;
    double lowWidth = 100.0;          ///< largeur du grave (bandes 1 et 2 de l'Imager), en %
};

enum class SessionStart { Auto, Preset };

struct UsageSession {
    SessionStart start = SessionStart::Auto;
    std::string context;   ///< groupe sonore reconnu (AUTO) ou nom du preset
    UsageState proposed, kept;
    HabitVector movement{}; ///< chemin parcouru par chaque réglage pendant la séance (allers-retours compris)
    bool undone = false, exported = false;
};

enum class Verdict { Useful, Inaudible, Hesitation, Risky, Undone };
const char* verdictLabel(Verdict v) noexcept; ///< « utile », « inaudible », « hésitation », « à risque », « annulé »

struct Gesture {
    std::string id;     ///< « sub+ », « comp:on », « infl:2 »
    std::string label;  ///< « Plus de sub », « Comp allumé », « Inflator → Lampe »
    int habit = -1;     ///< réglage continu concerné (Habit), −1 : un module ou un caractère
    double amount = 0.0; ///< écart (unités du réglage), 0 pour un module / caractère
    Verdict verdict = Verdict::Useful;
    std::string why;    ///< la règle qui a jugé
};

/// Le juge : ce que la séance contient de gestes, et ce qu'OMNIS en pense.
std::vector<Gesture> judgeSession(const UsageSession& s);
/// Les écarts à apprendre : seulement ceux des gestes utiles (les autres à zéro).
HabitVector usefulDeltas(const UsageSession& s, const std::vector<Gesture>& gestures);

class UsageGraph {
public:
    struct Node {
        std::string id, label;
        bool context = false;
        int seen = 0, useful = 0, exported = 0, risky = 0;
        double amount = 0.0; ///< somme des écarts utiles (moyenne = amount / useful)
        int lastSession = 0;
    };
    struct Edge {
        std::string a, b; ///< a < b pour geste ↔ geste ; contexte → geste sinon
        int count = 0, useful = 0;
    };
    static constexpr int kVersion = 1;
    static constexpr int kMinRepeats = 3;      ///< séances (utiles) avant qu'un geste soit une habitude
    static constexpr double kMinUsefulShare = 0.6;
    static constexpr int kForgetAfter = 30;    ///< séances sans le revoir : un geste vu une seule fois est oublié

    void record(const UsageSession& s, const std::vector<Gesture>& gestures);
    /// Les habitudes : gestes répétés et utiles, du plus fréquent au moins fréquent.
    [[nodiscard]] std::vector<const Node*> habits() const;
    /// Ce qui est fait souvent mais jugé inutile ou à risque (le dire à la personne, ne pas l'apprendre).
    [[nodiscard]] std::vector<const Node*> doubtful() const;
    [[nodiscard]] int sessions() const noexcept { return sessions_; }
    [[nodiscard]] const std::vector<Node>& nodes() const noexcept { return nodes_; }
    [[nodiscard]] const std::vector<Edge>& edges() const noexcept { return edges_; }
    [[nodiscard]] const Node* find(std::string_view id) const;

    [[nodiscard]] std::string serialize() const;
    static std::optional<UsageGraph> parse(std::string_view text);
    /// Pour la page de données (anonyme : aucun nom de fichier, aucun audio, aucune date exacte).
    [[nodiscard]] std::string exportJson() const;
    /// En clair, pour la barre IA. Vide s'il n'y a encore rien.
    [[nodiscard]] std::string describe() const;

private:
    Node& node(const std::string& id, const std::string& label, bool context);
    Edge& edge(const std::string& a, const std::string& b);
    void prune();
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
    int sessions_ = 0;
};

bool saveUsageGraph(const std::string& path, const UsageGraph& g);
UsageGraph loadUsageGraph(const std::string& path);

} // namespace omnis::ai
