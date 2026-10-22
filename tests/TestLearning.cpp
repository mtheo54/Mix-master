// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestLearning.cpp — Apprentissage local et communautaire.
// =============================================================================
#include "TestFramework.h"

#include "ai/Feedback.h"
#include "ai/Lexicon.h"

#include <string>
#include <vector>

using namespace omnis;
using namespace omnis::ai;

namespace {

/// Retour simulé : la phrase comprise par le lexique, puis la correction faite.
FeedbackRecord session(const Lexicon& lexicon, const char* text, const char* correction, int rating = 0) {
    const auto in = interpret(text, lexicon);
    FeedbackRecord r;
    r.request = text;
    r.hits = in.hits;
    r.unknownWords = in.unknownWords;
    r.correction = parseCorrection(correction).value_or(Descriptor{});
    r.rating = rating;
    return r;
}

} // namespace

OMNIS_TEST("retours: journal relu à l'identique") {
    const auto r = session(Lexicon::builtin(), "ça manque de crispy mais garde ma 808", "presence=+1.5, sub=-0.5", 1);
    const auto back = parseFeedback(serializeFeedback(r));
    OMNIS_CHECK(back.has_value());
    OMNIS_CHECK(back->request == r.request);
    OMNIS_CHECK(back->hits.size() == r.hits.size());
    OMNIS_CHECK(back->hits[1].protect);
    OMNIS_CHECK_NEAR(back->correction[Axis::Presence], 1.5, 1e-9);
    OMNIS_CHECK(back->rating == 1);
}

OMNIS_TEST("apprentissage local: « crispy » s'ajuste aux corrections, un défaut dans le bon sens") {
    Lexicon l = Lexicon::builtin();
    std::vector<FeedbackRecord> log;
    for (int i = 0; i < 3; ++i) log.push_back(session(l, "ça manque de crispy", "presence=+1.5"));
    log.push_back(session(l, "c'est boueux", "bas_medium=-1"));
    const auto report = learnFromFeedback(l, log);
    OMNIS_CHECK(report.recordsUsed == 4);
    OMNIS_CHECK_NEAR(l.find("crispy")->effect[Axis::Presence], 1.5 + 3 * 0.3 * 1.5, 1e-9);
    // « boueux » décrit le défaut : couper plus = le défaut était plus fort.
    OMNIS_CHECK_NEAR(l.find("boueux")->effect[Axis::LowMid], 2.0 + 0.3, 1e-9);
    // Les axes non encore appliqués (saturation) ne bougent pas.
    OMNIS_CHECK_NEAR(l.find("crispy")->effect[Axis::Saturation], 0.5, 1e-12);
}

OMNIS_TEST("apprentissage local: un mot inconnu devient un concept s'il est cohérent") {
    Lexicon l = Lexicon::builtin();
    std::vector<FeedbackRecord> log{session(l, "ça manque de zinzolin", "presence=+2"),
                                    session(l, "plus de zinzolin stp", "presence=+1.6, air=+0.2")};
    learnFromFeedback(l, log);
    OMNIS_CHECK(l.find("appris_zinzolin") != nullptr);
    OMNIS_CHECK(interpret("ça manque de zinzolin", l).desire[Axis::Presence] > 1.7);

    Lexicon l2 = Lexicon::builtin();
    std::vector<FeedbackRecord> contradictory{session(l2, "plus de blorp", "presence=+2"), session(l2, "plus de blorp", "presence=-2")};
    learnFromFeedback(l2, contradictory);
    OMNIS_CHECK(l2.find("appris_blorp") == nullptr);
}

OMNIS_TEST("communauté: export anonyme, fusion, seules les tendances confirmées entrent") {
    const Lexicon& base = Lexicon::builtin();
    std::vector<std::string> exports;
    for (int user = 0; user < 12; ++user) {
        std::vector<FeedbackRecord> log{session(base, "ça manque de crispy", "presence=+1"),
                                        session(base, "manque de crispy", "presence=+1"),
                                        session(base, "plus de zinzolin", "air=+1.5"),
                                        session(base, "encore plus de zinzolin", "air=+1.5")}; // 24 observations ≥ 20
        exports.push_back(exportAggregate(log));
    }
    OMNIS_CHECK(exports[0].find("manque") == std::string::npos); // jamais la phrase
    const std::string merged = mergeAggregates(exports);

    Lexicon everyone = base;
    const auto report = applyCommunity(everyone, merged);
    OMNIS_CHECK_NEAR(everyone.find("crispy")->effect[Axis::Presence], 1.5 + 0.3 * 1.0, 1e-9);
    OMNIS_CHECK(everyone.find("appris_zinzolin") != nullptr);
    OMNIS_CHECK(!report.changes.empty());

    // 5 personnes seulement : rien ne bouge.
    Lexicon few = base;
    applyCommunity(few, mergeAggregates(std::vector<std::string>(exports.begin(), exports.begin() + 5)));
    OMNIS_CHECK_NEAR(few.find("crispy")->effect[Axis::Presence], 1.5, 1e-12);
    OMNIS_CHECK(few.find("appris_zinzolin") == nullptr);
}

OMNIS_TEST("retours: correction déduite des courbes proposée / gardée") {
    MatchCurve pm{}, km{}, ps{}, ks{};
    for (int k = 15; k <= 17; ++k) km[static_cast<std::size_t>(k)] = 1.0; // présence +1
    for (int k = 12; k < params::kMatchBands; ++k) ks[static_cast<std::size_t>(k)] = km[static_cast<std::size_t>(k)] + 2.0; // plus large
    const auto d = correctionFromCurves(pm, km, ps, ks, -9.0, -8.0);
    OMNIS_CHECK_NEAR(d[Axis::Presence], 1.0, 1e-12);
    OMNIS_CHECK(d[Axis::Width] > 1.0);
    OMNIS_CHECK_NEAR(d[Axis::Loudness], 1.0, 1e-12);
}
