// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestSearchMaster.cpp — AUTO précis (tools/common/LoudnessTuner,
//  searchMaster) : trois répartitions clip / limiteur menées à la sonie visée,
//  jamais au-delà du garde-fou, la mieux notée retenue ; une prod déjà assez
//  forte n'est pas poussée (un seul essai) ; l'annulation est entendue.
// =============================================================================
#include "TestFramework.h"

#include "AnalysisReport.h"
#include "LoudnessTuner.h"
#include "TestSignals.h"

#include "params/Parameters.h"

#include <atomic>
#include <cmath>
#include <string>
#include <vector>

using namespace omnis;
using namespace omnis::tools;

namespace {

std::vector<std::pair<std::string, double>> defaults() {
    std::vector<std::pair<std::string, double>> base;
    for (const auto& spec : params::allParameters())
        if (params::isPersistent(spec)) base.emplace_back(std::string(spec.key), spec.defaultValue);
    return base;
}

AudioFile beat(double peakDb) {
    // 4 mesures à 140 BPM (~6,9 s), un fond de bruit rose : une boucle de drums. (Tant que
    // le clip ne dépasse pas son plafond, clip et limiteur se valent : mêmes notes.)
    AudioFile f = signals::drumLoop(48000.0, 4, 140.0, peakDb);
    const auto pink = signals::pinkNoise(48000.0, f.durationSeconds(), peakDb - 22.0, 5);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < f.numFrames(); ++i)
            f.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] += pink.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)];
    return f;
}

} // namespace

OMNIS_TEST("AUTO précis: 3 répartitions menées à -11 LUFS, la mieux notée retenue, garde-fou jamais dépassé") {
    const AudioFile prod = beat(-8.0);
    std::printf("      prod : %.1f LUFS\n", analyze(prod).integratedLufs);
    SearchOptions opt;
    opt.targetLufs = -11.0;
    opt.maxGainDb = 14.0;
    int calls = 0;
    opt.progress = [&calls](int, int) { ++calls; };
    const auto found = searchMaster(prod, defaults(), opt);
    OMNIS_CHECK(found.tried.size() == 3);
    OMNIS_CHECK(calls == 3);
    OMNIS_CHECK(found.best >= 0 && !found.cancelled);
    for (const auto& r : found.tried) {
        std::printf("      %-13s gain %5.1f dB  %6.1f LUFS  distorsion %6.1f dB  tonal %.1f\n", r.strategy.name.c_str(), r.totalGainDb, r.integratedLufs,
                    r.distortion.weightedDb, r.tonal.maxDeviationDb);
        OMNIS_CHECK(r.totalGainDb <= opt.maxGainDb + 1e-9);
        OMNIS_CHECK(std::fabs(r.integratedLufs - opt.targetLufs) < 0.35);
    }
    // Le meilleur : la plus faible note (distorsion, pénalités) ; ici tous atteignent la cible.
    const auto& best = found.tried[static_cast<std::size_t>(found.best)];
    for (const auto& r : found.tried)
        if (r.tonalOk && best.tonalOk) OMNIS_CHECK(best.distortion.weightedDb <= r.distortion.weightedDb + 1e-9);
    // Ses réglages : les parts du gain total, clip et limiteur.
    double clip = 0.0, lim = 0.0;
    for (const auto& [key, v] : settingsOf(best)) {
        if (key == "clip_drive") clip = v;
        if (key == "lim_gain") lim = v;
    }
    OMNIS_CHECK(std::fabs(clip + lim - best.totalGainDb) < 1e-6);
}

OMNIS_TEST("AUTO précis: garde-fou serré -> la cible n'est pas atteinte, aucun essai ne le dépasse") {
    const AudioFile prod = beat(-14.0);
    SearchOptions opt;
    opt.targetLufs = -9.0;
    opt.maxGainDb = 3.0;
    const auto found = searchMaster(prod, defaults(), opt);
    OMNIS_CHECK(found.tried.size() == 3);
    for (const auto& r : found.tried) {
        OMNIS_CHECK(r.totalGainDb <= 3.0 + 1e-9);
        OMNIS_CHECK(!r.reached);
    }
}

OMNIS_TEST("AUTO précis: une prod déjà plus forte que la cible n'est pas poussée (un seul essai, gain nul)") {
    const AudioFile prod = beat(-1.0);
    SearchOptions opt;
    opt.targetLufs = -14.0;
    opt.maxGainDb = 10.0;
    const auto found = searchMaster(prod, defaults(), opt);
    std::printf("      prod %.1f LUFS -> %zu essai(s)\n", analyze(prod).integratedLufs, found.tried.size());
    OMNIS_CHECK(found.tried.size() == 1);
    OMNIS_CHECK(found.best == 0);
    OMNIS_CHECK(found.tried.front().totalGainDb == 0.0);
}

OMNIS_TEST("AUTO précis: annulé avant de commencer, rien n'est essayé") {
    std::atomic<bool> cancel{true};
    SearchOptions opt;
    opt.cancel = &cancel;
    const auto found = searchMaster(beat(-12.0), defaults(), opt);
    OMNIS_CHECK(found.cancelled);
    OMNIS_CHECK(found.best == -1);
    OMNIS_CHECK(found.tried.empty());
}
