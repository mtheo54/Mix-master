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

OMNIS_TEST("usages: choix artistique (DA) — contre la règle, mais voulu par le style, ou gardé jusqu'à l'export encore et encore") {
    // Écraser le volume : à risque en trap, choix artistique en rage (le style visé).
    auto s = session({{Habit::Loudness, 9.0}});
    OMNIS_CHECK(find(judgeSession(s), "volume+")->verdict == Verdict::Risky);
    s.styles = {"rage fr", "new wave fr"};
    auto g = judgeSession(s);
    const auto* v = find(g, "volume+");
    OMNIS_CHECK(v && v->verdict == Verdict::Creative && v->why.find("rage") != std::string::npos);
    if (v) std::printf("      %s : %s\n", verdictLabel(v->verdict), v->why.c_str());
    OMNIS_CHECK(usefulDeltas(s, g)[static_cast<std::size_t>(Habit::Loudness)] == 9.0); // appris (pour ce style)
    // Le limiteur éteint n'est jamais une DA, même en rage.
    s.kept.modules[6] = false;
    OMNIS_CHECK(find(judgeSession(s), "mod6:off")->verdict == Verdict::Risky);
    // Un son très sombre en boom bap, un grave large en ambient : voulus. Le grave large en trap : non.
    s = session({{Habit::Tilt, -4.5}});
    s.styles = {"boom bap fr"};
    OMNIS_CHECK(find(judgeSession(s), "tilt-")->verdict == Verdict::Creative);
    s = session({});
    s.kept.lowWidth = 130.0;
    OMNIS_CHECK(find(judgeSession(s), "grave:large")->verdict == Verdict::Risky);
    s.context = "ambient";
    OMNIS_CHECK(find(judgeSession(s), "grave:large")->verdict == Verdict::Creative);
    // Annulé : même un choix artistique ne s'apprend pas.
    s.undone = true;
    OMNIS_CHECK(find(judgeSession(s), "grave:large")->verdict == Verdict::Undone);

    // Sans style qui le justifie, mais gardé jusqu'à l'export une 2e fois dans le même contexte : sa DA.
    UsageGraph graph;
    const auto run = [&](bool exported, bool limiterOff = false) {
        auto x = session({{Habit::Loudness, 9.0}}, exported);
        if (limiterOff) x.kept.modules[6] = false;
        auto gestures = judgeSession(x);
        graph.reviewArtistic(x, gestures);
        graph.record(x, gestures);
        return gestures;
    };
    OMNIS_CHECK(find(run(true, true), "volume+")->verdict == Verdict::Risky);    // 1re fois : à risque
    OMNIS_CHECK(find(run(false), "volume+")->verdict == Verdict::Risky);         // pas exporté : pas de preuve
    auto again = run(true, true);
    OMNIS_CHECK(find(again, "volume+")->verdict == Verdict::Creative);           // 2e export : sa DA
    OMNIS_CHECK(find(again, "mod6:off")->verdict == Verdict::Risky);             // le limiteur, jamais
    run(true);
    const auto* n = graph.find("volume+");
    OMNIS_CHECK(n && n->creative == 2 && n->useful == 2 && n->risky == 2);
    const std::string text = graph.describe();
    std::printf("      %s\n", text.c_str());
    OMNIS_CHECK(text.find("Tes choix artistiques (DA), appris pour leur style : Plus fort (2)") != std::string::npos);
    OMNIS_CHECK(graph.exportJson().find("\"creatif\":2") != std::string::npos);
    // Format v2 relu à l'identique ; un graphe v1 (avant les choix artistiques) se relit encore.
    const auto back = UsageGraph::parse(graph.serialize());
    OMNIS_CHECK(back && back->serialize() == graph.serialize() && back->find("volume+")->creative == 2);
    const auto v1 = UsageGraph::parse("omnis-graphe\tv1\tseances\t1\nN\tsub+\t0\t1\t1\t0\t0\t1.5\t1\tPlus de sub\nE\tctx:auto:trap\tsub+\t1\t1\n");
    OMNIS_CHECK(v1 && v1->find("sub+") && v1->find("sub+")->useful == 1 && v1->find("sub+")->creative == 0 && v1->edges().size() == 1);
    OMNIS_CHECK(!UsageGraph::parse("omnis-graphe\tv2\tseances\t1\nN\tx\t0\t2\t1\t0\t0\t2\t0\t1\tX\n")); // créatif > utile
}
