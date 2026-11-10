// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestCommunityHabits.cpp — La moyenne de ce que font les utilisateurs : anonyme,
//  publiée seulement à partir de 5 personnes, de moins en moins forte à mesure que la
//  personne a ses propres habitudes.
// =============================================================================
#include "TestFramework.h"

#include "ai/CommunityHabits.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace omnis::ai;

namespace {

UsageSession trapSession(double sub, SessionStart start = SessionStart::Auto, const char* ctx = "trap") {
    UsageSession s;
    s.start = start;
    s.context = ctx;
    s.proposed.habits = {0.0, 0.0, 30.0, 6.0, 110.0, -18.0};
    s.kept = s.proposed;
    s.kept.habits[static_cast<std::size_t>(Habit::Sub)] += sub;
    s.movement[static_cast<std::size_t>(Habit::Sub)] = std::abs(sub);
    return s;
}

std::vector<ContributionRow> person(std::initializer_list<ContributionRow> rows) { return rows; }

} // namespace

OMNIS_TEST("communauté: la contribution est anonyme (contextes génériques, nombres) et se relit") {
    OMNIS_CHECK(genericContext("ctx:auto:trap") == "auto:trap");
    OMNIS_CHECK(genericContext("ctx:preset:style rage fr") == "style:rage fr");
    OMNIS_CHECK(genericContext("ctx:preset:SLAYR - Sloppy Joe").empty()); // le nom d'une référence : gardé chez soi
    UsageGraph g;
    for (int i = 0; i < 3; ++i) {
        auto s = trapSession(1.2);
        g.record(s, judgeSession(s));
    }
    auto ref = trapSession(1.0, SessionStart::Preset, "SLAYR - Sloppy Joe");
    g.record(ref, judgeSession(ref));
    const std::string text = contributionText(g);
    std::printf("%s", text.c_str());
    OMNIS_CHECK(text.find("G\tauto:trap\tsub+\t3\t3\t0\t1.") != std::string::npos);
    OMNIS_CHECK(text.find("SLAYR") == std::string::npos && text.find("Sloppy") == std::string::npos);
    const auto rows = parseContribution(text);
    OMNIS_CHECK(rows && rows->size() == 1 && (*rows)[0].useful == 3);
    OMNIS_CHECK(!parseContribution("autre chose"));
    OMNIS_CHECK(!parseContribution("omnis-contribution\tv1\tseances\t1\nG\tauto:trap\tsub+\t1\t3\t0\t1.0\n")); // utile > vu
    OMNIS_CHECK(!parseContribution("omnis-contribution\tv1\tseances\t1\nG\tauto:trap\tsub+\t3\t3\t0\tnan\n"));
}

OMNIS_TEST("communauté: la moyenne ne publie qu'à partir de 5 personnes, et AUTO s'en sert de moins en moins") {
    std::vector<std::vector<ContributionRow>> all;
    // 6 personnes : plus de sub en trap (une habitude chez chacune).
    for (int i = 0; i < 6; ++i) all.push_back(person({{"auto:trap", "sub+", 4, 4, 0, 1.0 + 0.1 * i}}));
    // 4 personnes : plus fort en trap — trop peu pour publier.
    for (int i = 0; i < 4; ++i) all.push_back(person({{"auto:trap", "volume+", 3, 3, 0, 2.0}}));
    // 2 personnes en trap sans habitude ; une qui n'a fait qu'une fois le geste (pas une habitude).
    all.push_back(person({{"auto:trap", "tilt+", 1, 1, 0, 0.5}}));
    all.push_back(person({{"auto:trap", "sub+", 1, 1, 0, 3.0}}));
    const auto c = CommunityHabits::aggregate(all);
    std::printf("%s", c.serialize().c_str());
    OMNIS_CHECK(c.contributions() == 12);
    OMNIS_CHECK(c.habits().size() == 1 && c.habits()[0].gesture == "sub+" && c.habits()[0].people == 6);
    OMNIS_CHECK(std::fabs(c.habits()[0].share - 0.5) < 1e-9);           // 6 sur les 12 personnes qui ont fait du trap
    OMNIS_CHECK(std::fabs(c.habits()[0].mean - 1.25) < 1e-9);
    // Sans habitude à soi : la moitié de (moyenne × part) ; avec beaucoup d'habitudes : presque rien.
    const double alone = c.delta("auto:trap", 0.0)[static_cast<std::size_t>(Habit::Sub)];
    const double used = c.delta("auto:trap", 0.9)[static_cast<std::size_t>(Habit::Sub)];
    OMNIS_CHECK(std::fabs(alone - 0.5 * 1.25 * 0.5) < 1e-9 && std::fabs(used - alone * 0.1) < 1e-9);
    OMNIS_CHECK(c.delta("auto:drill", 0.0)[static_cast<std::size_t>(Habit::Sub)] == 0.0); // un autre contexte : rien
    OMNIS_CHECK(c.peopleIn("auto:trap") == 12);
    const std::string said = describeCommunity(c.delta("auto:trap", 0.0), "auto:trap", c.peopleIn("auto:trap"));
    std::printf("      %s\n", said.c_str());
    OMNIS_CHECK(said == "ce que font les autres en trap (12 personnes) : sub +0.3 dB");
    // Toujours borné, même si tout le monde pousse fort.
    std::vector<std::vector<ContributionRow>> loud;
    for (int i = 0; i < 8; ++i) loud.push_back(person({{"auto:rage", "volume+", 5, 5, 5, 40.0}}));
    const auto l = CommunityHabits::aggregate(loud);
    OMNIS_CHECK(l.delta("auto:rage", 0.0)[static_cast<std::size_t>(Habit::Loudness)] <= 0.5 * habitBound(Habit::Loudness) + 1e-12);
    // Se relit à l'identique ; un fichier abîmé ou sous le seuil d'anonymat est refusé.
    const auto back = CommunityHabits::parse(c.serialize());
    OMNIS_CHECK(back && back->serialize() == c.serialize());
    OMNIS_CHECK(!CommunityHabits::parse("omnis-communaute\tv1\tcontributions\t3\nH\tauto:trap\tsub+\t2\t0.5\t1.0\n"));
    OMNIS_CHECK(!CommunityHabits::parse("rien"));
    // Celle livrée avec le plug-in se lit (vide tant qu'aucune moyenne n'a été publiée).
    OMNIS_CHECK(CommunityHabits::shipped().contributions() >= 0);
}
