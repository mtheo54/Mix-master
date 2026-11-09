// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestHabitLearning.cpp — Apprendre les habitudes (ai/HabitLearning) :
//  journal relu à l'identique, réseau qui retrouve une habitude cachée sur des
//  prods jamais vues, qui n'invente rien sur du hasard, toujours borné, relu
//  à l'identique ; export anonyme (rien que des nombres).
// =============================================================================
#include "TestFramework.h"

#include "ai/HabitLearning.h"

#include <cmath>
#include <random>

using namespace omnis::ai;

namespace {

constexpr int kFeatures = 37;

/// Bruit gaussien portable (Box-Muller sur mt19937) : std::normal_distribution ne tire pas les mêmes
/// valeurs sous MSVC, libc++ et libstdc++.
double gauss(std::mt19937& rng) {
    const double u1 = (static_cast<double>(rng()) + 1.0) / 4294967297.0, u2 = static_cast<double>(rng()) / 4294967296.0;
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
}

std::vector<double> randomProd(std::mt19937& rng) {
    std::vector<double> f(kFeatures);
    for (auto& v : f) v = 2.0 * gauss(rng);
    return f;
}

double uniform(std::mt19937& rng) { return static_cast<double>(rng()) / 4294967296.0 * 2.0 - 1.0; }

} // namespace

OMNIS_TEST("habitudes: journal relu à l'identique ; lignes abîmées refusées ; aucune tabulation ne s'infiltre") {
    JournalEvent e;
    e.kind = JournalKind::Retouch;
    e.time = 1762700000;
    e.group = "trap\tpiégé";
    e.features = {1.5, -2.25, 0.0, 3.125};
    e.values = {0.5, -1.0, 10.0, 0.0, -5.0, 2.0};
    const auto line = serializeJournalEvent(e);
    OMNIS_CHECK(line.find('\n') == std::string::npos);
    const auto back = parseJournalEvent(line);
    OMNIS_CHECK(back.has_value());
    OMNIS_CHECK(back->kind == JournalKind::Retouch && back->time == e.time && back->group == "trap piégé");
    OMNIS_CHECK(back->features == e.features && back->values == e.values);
    OMNIS_CHECK(!parseJournalEvent("v2\tAUTO\t0\tx\t1\t0,0,0,0,0,0"));        // version inconnue
    OMNIS_CHECK(!parseJournalEvent("v1\tAUTO\t0\tx\t1\t0,0,0"));              // écarts incomplets
    OMNIS_CHECK(!parseJournalEvent("v1\tRIEN\t0\tx\t1\t0,0,0,0,0,0"));        // type inconnu
    OMNIS_CHECK(!parseJournalEvent("v1\tAUTO\t0\tx\tnan\t0,0,0,0,0,0"));      // nombre faux
    OMNIS_CHECK(!parseJournalEvent("# commentaire"));
}

OMNIS_TEST("habitudes: le réseau retrouve une habitude cachée sur des prods jamais vues (et ne l'invente pas sur du hasard)") {
    std::mt19937 rng(11);
    // L'habitude : prod sombre (grandeur 10 basse) -> la personne remonte l'aigu (tilt +1.5) ;
    // prod brillante -> elle le baisse. Et toujours un peu plus de sub (+0.8 dB).
    const auto habit = [](const std::vector<double>& f) {
        HabitVector d{};
        d[static_cast<int>(Habit::Tilt)] = f[10] < 0.0 ? 1.5 : -1.5;
        d[static_cast<int>(Habit::Sub)] = 0.8;
        return d;
    };
    // Plus il y a de séances, plus il voit juste (prods jamais vues).
    const auto accuracy = [&](int sessions, double* subMean) {
        std::vector<HabitSample> train;
        for (int i = 0; i < sessions; ++i) {
            HabitSample s;
            s.features = randomProd(rng);
            s.deltas = habit(s.features);
            train.push_back(s);
        }
        HabitNet net;
        OMNIS_CHECK(net.train(train));
        int right = 0;
        double sub = 0.0;
        for (int i = 0; i < 200; ++i) {
            const auto f = randomProd(rng);
            const auto p = net.predict(f);
            right += (p[static_cast<int>(Habit::Tilt)] > 0.0) == (f[10] < 0.0) ? 1 : 0;
            sub += p[static_cast<int>(Habit::Sub)] / 200.0;
        }
        if (subMean) *subMean = sub;
        std::printf("      %3d séances (%zu poids) : pente dans le bon sens %d / 200 ; sub moyen %+.2f dB\n", sessions, net.weightCount(), right, sub);
        return right / 200.0;
    };
    double sub20 = 0.0, sub60 = 0.0;
    const double a20 = accuracy(20, &sub20), a60 = accuracy(60, &sub60), a200 = accuracy(200, nullptr);
    // Une habitude cachée dans 1 grandeur sur 37 : le réseau la retrouve d'autant mieux qu'il a de séances
    // (mesuré : ~69 % à 20 séances, ~74 % à 60, ~84 % à 200 ; le hasard ferait 50 %).
    OMNIS_CHECK(a60 >= 0.68);
    OMNIS_CHECK(a200 >= 0.80);
    OMNIS_CHECK(a200 >= a20 + 0.10);
    OMNIS_CHECK(sub60 > 0.4 && sub60 < 1.0); // l'habitude constante (+0.8 dB), dosée par la confiance
    OMNIS_CHECK(sub20 < sub60);               // moins d'exemples, moins d'assurance
    const int tests = 200;

    // Du hasard (aucune habitude) : le réseau ne doit rien affirmer de net en moyenne.
    std::vector<HabitSample> noise;
    for (int i = 0; i < 60; ++i) {
        HabitSample s;
        s.features = randomProd(rng);
        for (int o = 0; o < kHabits; ++o) s.deltas[static_cast<std::size_t>(o)] = uniform(rng) * habitBound(static_cast<Habit>(o));
        noise.push_back(s);
    }
    HabitNet blind;
    OMNIS_CHECK(blind.train(noise));
    double mean = 0.0;
    for (int i = 0; i < tests; ++i) mean += blind.predict(randomProd(rng))[static_cast<int>(Habit::Tilt)] / tests;
    std::printf("      sur du hasard : pente moyenne %+.2f dB\n", mean);
    OMNIS_CHECK(std::fabs(mean) < 0.4);
}

OMNIS_TEST("habitudes: toujours borné, rien sous 3 exemples, relu à l'identique, export anonyme") {
    std::mt19937 rng(3);
    std::vector<HabitSample> wild;
    for (int i = 0; i < 20; ++i) {
        HabitSample s;
        s.features = randomProd(rng);
        s.deltas.fill(1000.0); // retouches absurdes (déjà bornées par samplesFromJournal ; ici en direct)
        wild.push_back(s);
    }
    HabitNet net;
    OMNIS_CHECK(net.train(wild));
    for (int i = 0; i < 50; ++i) {
        const auto p = net.predict(randomProd(rng));
        for (int o = 0; o < kHabits; ++o) OMNIS_CHECK(std::fabs(p[static_cast<std::size_t>(o)]) <= habitBound(static_cast<Habit>(o)) + 1e-12);
    }
    HabitNet few;
    OMNIS_CHECK(!few.train({wild[0], wild[1]}));
    OMNIS_CHECK(few.predict(wild[0].features) == HabitVector{});
    const auto back = HabitNet::parse(net.serialize());
    OMNIS_CHECK(back.has_value());
    const auto x = randomProd(rng);
    const auto a = net.predict(x), b = back->predict(x);
    for (int o = 0; o < kHabits; ++o) OMNIS_CHECK(std::fabs(a[static_cast<std::size_t>(o)] - b[static_cast<std::size_t>(o)]) < 1e-6);
    OMNIS_CHECK(!HabitNet::parse("v1\nentrees 3 exemples 5\nmoyenne 1,2\n")); // tronqué : refusé

    // Journal -> exemples : seules les retouches apprennent, bornées ; l'export ne garde qu'elles.
    std::vector<JournalEvent> journal;
    JournalEvent autoEv;
    autoEv.kind = JournalKind::Auto;
    autoEv.features = randomProd(rng);
    journal.push_back(autoEv);
    JournalEvent retouch = autoEv;
    retouch.kind = JournalKind::Retouch;
    retouch.time = 1762712345;
    retouch.values = {9.0, -0.5, 0.0, 0.0, 0.0, 0.0};
    journal.push_back(retouch);
    const auto samples = samplesFromJournal(journal);
    OMNIS_CHECK(samples.size() == 1);
    OMNIS_CHECK(samples[0].deltas[0] == habitBound(Habit::Tilt)); // 9 dB ramené à 2
    const auto exported = exportAnonymous(journal);
    OMNIS_CHECK(exported.find("AUTO") == std::string::npos);
    OMNIS_CHECK(exported.find(std::to_string(1762712345 / 86400 * 86400)) != std::string::npos); // jour, pas l'heure
    OMNIS_CHECK(describeHabits(HabitVector{1.0, 0.0, 0.0, 0.0, 0.0, 0.0}, 6).find("tilt") != std::string::npos);
    OMNIS_CHECK(describeHabits(HabitVector{}, 6).empty());
}
