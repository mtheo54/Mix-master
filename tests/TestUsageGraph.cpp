// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestUsageGraph.cpp — Ce que la personne fait le plus, et si c'est utile.
// =============================================================================
#include "TestFramework.h"

#include "ai/UsageGraph.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace omnis::ai;

namespace {

UsageState base() {
    UsageState s;
    s.habits = {0.0, 0.0, 30.0, 6.0, 110.0, -18.0};
    s.modules = {true, true, true, true, true, true, true, false};
    s.lowWidth = 100.0;
    return s;
}

const Gesture* find(const std::vector<Gesture>& g, const std::string& id) {
    for (const auto& x : g)
        if (x.id == id) return &x;
    return nullptr;
}

UsageSession session(std::initializer_list<std::pair<Habit, double>> moves, bool exported = false) {
    UsageSession s;
    s.context = "trap";
    s.proposed = base();
    s.kept = base();
    for (const auto& [h, d] : moves) {
        s.kept.habits[static_cast<std::size_t>(h)] += d;
        s.movement[static_cast<std::size_t>(h)] += std::abs(d);
    }
    s.exported = exported;
    return s;
}

} // namespace

OMNIS_TEST("usages: le juge (connaissances de base) dit utile, inaudible, hésitation, à risque, annulé") {
    // Petit, inaudible : on passe à côté (aucun geste).
    OMNIS_CHECK(judgeSession(session({{Habit::Sub, 0.3}, {Habit::Tilt, 0.1}})).empty());
    // Un vrai geste, gardé : utile.
    auto g = judgeSession(session({{Habit::Sub, 1.5}}, true));
    OMNIS_CHECK(g.size() == 1 && g[0].id == "sub+" && g[0].verdict == Verdict::Useful && g[0].label == "Plus de sub");
    // Contre les règles du métier : sub au-delà de +6 dB, volume écrasé, pente extrême.
    g = judgeSession(session({{Habit::Sub, 7.0}, {Habit::Loudness, 9.0}, {Habit::Tilt, -4.5}}));
    for (const char* id : {"sub+", "volume+", "tilt-"}) {
        const auto* x = find(g, id);
        OMNIS_CHECK(x && x->verdict == Verdict::Risky);
        if (x) std::printf("      %-8s %s : %s\n", id, verdictLabel(x->verdict), x->why.c_str());
    }
    // Baisser le volume n'est jamais « écraser » : utile.
    OMNIS_CHECK(find(judgeSession(session({{Habit::Loudness, -2.0}})), "volume-")->verdict == Verdict::Useful);
    // Hésitation : beaucoup d'allers-retours pour revenir presque au départ.
    auto s = session({{Habit::Sub, 0.2}});
    s.movement[static_cast<std::size_t>(Habit::Sub)] = 6.0;
    g = judgeSession(s);
    OMNIS_CHECK(g.size() == 1 && g[0].id == "sub~" && g[0].verdict == Verdict::Hesitation);
    s = session({{Habit::Width, 9.0}});
    s.movement[static_cast<std::size_t>(Habit::Width)] = 60.0; // de 110 à 170 et retour vers 119
    OMNIS_CHECK(judgeSession(s)[0].verdict == Verdict::Hesitation);
    // Modules et caractères : Comp allumé (pas « plus de colle » en plus), Inflator → Lampe ; limiteur éteint : à risque.
    s = session({});
    s.kept.modules[7] = true;
    s.kept.habits[static_cast<std::size_t>(Habit::Comp)] = -24.0;
    s.kept.inflCharacter = 2;
    s.kept.modules[6] = false;
    g = judgeSession(s);
    OMNIS_CHECK(find(g, "mod7:on") && find(g, "mod7:on")->verdict == Verdict::Useful && !find(g, "comp-") && !find(g, "comp+"));
    OMNIS_CHECK(find(g, "infl:2") && find(g, "infl:2")->label == "Inflator → Lampe");
    OMNIS_CHECK(find(g, "mod6:off") && find(g, "mod6:off")->verdict == Verdict::Risky);
    // Le grave élargi : contre la règle (le grave reste au centre).
    s = session({});
    s.kept.lowWidth = 130.0;
    OMNIS_CHECK(find(judgeSession(s), "grave:large") && find(judgeSession(s), "grave:large")->verdict == Verdict::Risky);
    // Annulé aussitôt : rien ne s'apprend.
    s = session({{Habit::Sub, 1.5}});
    s.undone = true;
    OMNIS_CHECK(judgeSession(s)[0].verdict == Verdict::Undone);
    // Le réseau n'apprend que les gestes utiles.
    s = session({{Habit::Sub, 1.5}, {Habit::Loudness, 9.0}});
    const auto d = usefulDeltas(s, judgeSession(s));
    OMNIS_CHECK(d[static_cast<std::size_t>(Habit::Sub)] == 1.5 && d[static_cast<std::size_t>(Habit::Loudness)] == 0.0);
}

OMNIS_TEST("usages: le graphe garde ce qui revient et sert, oublie le reste, se relit à l'identique") {
    UsageGraph g;
    // 5 séances : sub+ à chaque fois (utile, 3 exportées), Comp allumé 3 fois, un roll inutile, du volume écrasé 3 fois.
    for (int i = 0; i < 5; ++i) {
        auto s = session({{Habit::Sub, 1.0 + 0.2 * i}}, i < 3);
        if (i < 3) {
            s.kept.modules[7] = true;
            s.kept.habits[static_cast<std::size_t>(Habit::Comp)] = -20.0;
            s.kept.habits[static_cast<std::size_t>(Habit::Loudness)] += 9.0;
        }
        g.record(s, judgeSession(s));
    }
    auto once = session({{Habit::Tilt, 1.0}}); // une seule fois
    g.record(once, judgeSession(once));
    const auto h = g.habits();
    OMNIS_CHECK(!h.empty() && h[0]->id == "sub+" && h[0]->useful == 5 && h[0]->exported == 3);
    OMNIS_CHECK(std::any_of(h.begin(), h.end(), [](const auto* n) { return n->id == "mod7:on"; }));
    OMNIS_CHECK(std::none_of(h.begin(), h.end(), [](const auto* n) { return n->id == "volume+" || n->id == "tilt+"; })); // à risque / pas assez répété
    const auto doubt = g.doubtful();
    OMNIS_CHECK(!doubt.empty() && doubt[0]->id == "volume+" && doubt[0]->risky == 3);
    // Liens : le contexte « AUTO · trap » vers sub+, et sub+ ↔ Comp allumé (faits ensemble).
    const auto edge = [&](const std::string& a, const std::string& b) {
        for (const auto& e : g.edges())
            if (e.a == a && e.b == b) return e.count;
        return 0;
    };
    OMNIS_CHECK(edge("ctx:auto:trap", "sub+") == 5 && edge("mod7:on", "sub+") == 3);
    const std::string text = g.describe();
    std::printf("      %s\n", text.c_str());
    OMNIS_CHECK(text.find("Plus de sub (5 fois)") != std::string::npos && text.find("Plus fort (3)") != std::string::npos);
    // Relu à l'identique ; un fichier abîmé est refusé.
    const auto back = UsageGraph::parse(g.serialize());
    OMNIS_CHECK(back && back->serialize() == g.serialize());
    OMNIS_CHECK(!UsageGraph::parse("omnis-graphe\tv1\tseances\t3\nN\tx\t0\t1\t5\t0\t0\t0\t1\tX\n")); // utile > vu
    OMNIS_CHECK(!UsageGraph::parse("autre chose"));
    // Export pour la page de données : des noms de réglages et des nombres.
    const std::string json = g.exportJson();
    OMNIS_CHECK(json.find("\"id\":\"sub+\"") != std::string::npos && json.find("\"habitude\":true") != std::string::npos &&
                json.find("\"seances\":6") != std::string::npos);
    // Ce qui n'est arrivé qu'une fois, il y a longtemps, est oublié.
    for (int i = 0; i < UsageGraph::kForgetAfter; ++i) {
        auto s = session({{Habit::Sub, 1.0}});
        g.record(s, judgeSession(s));
    }
    OMNIS_CHECK(g.find("tilt+") == nullptr && g.find("sub+") != nullptr);
    for (const auto& e : g.edges()) OMNIS_CHECK(e.a != "tilt+" && e.b != "tilt+");
}
