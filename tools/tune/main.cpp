// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/tune — Recherche du réglage le plus propre pour une sonie cible.
//
//  Usage : omnis_tune mix.wav --target -6 [--ceiling -1] [--excerpt 30]
//                    [--max-clean] [--max-tonal 1.5] [--render meilleur.wav]
//  Compare plusieurs stratégies de répartition du gain à sonie intégrée égale
//  (passage le plus fort du morceau) et les classe par distorsion pondérée K.
//  --max-clean : cherche au contraire la sonie la plus forte que chaque
//  stratégie atteint sans s'écarter de plus de --max-tonal dB (par octave) de
//  l'équilibre tonal de la prod ; --target devient un plafond.
// =============================================================================
#include "AudioRead.h"
#include "AnalysisReport.h"
#include "LoudnessTuner.h"
#include "WavFile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace omnis::tools;

namespace {

std::vector<TuningStrategy> strategies(double ceiling) {
    const std::vector<std::pair<std::string, double>> base{{"lim_ceiling", ceiling}};
    const auto with = [&](std::vector<std::pair<std::string, double>> extra) {
        auto v = base;
        v.insert(v.end(), extra.begin(), extra.end());
        return v;
    };
    return {
        {"limiteur seul", with({}), {{"lim_gain", 1.0}}},
        {"clip 70/30", with({}), {{"clip_drive", 0.7}, {"lim_gain", 0.3}}},
        {"clip 50/50", with({}), {{"clip_drive", 0.5}, {"lim_gain", 0.5}}},
        {"clip 70/30, marge sub 50 %", with({{"clip_sub_room", 50.0}}), {{"clip_drive", 0.7}, {"lim_gain", 0.3}}},
    };
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::puts("Abrasion Labs // OMNIS — sonie cible\n  omnis_tune mix.wav --target -6 [--ceiling -1] [--excerpt 30 | --full] [--max-clean] [--max-tonal 1.5] [--render out.wav]");
        return 1;
    }
    TuningOptions options;
    double ceiling = -1.0;
    std::string renderPath;
    int only = -1; // index d'une seule stratégie (exécutions parallèles)
    bool maxClean = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--target" && i + 1 < argc) options.targetLufs = std::atof(argv[++i]);
        else if (a == "--ceiling" && i + 1 < argc) ceiling = std::atof(argv[++i]);
        else if (a == "--excerpt" && i + 1 < argc) options.excerptSeconds = std::atof(argv[++i]);
        else if (a == "--full") options.excerptSeconds = 1e9; // morceau entier
        else if (a == "--only" && i + 1 < argc) only = std::atoi(argv[++i]);
        else if (a == "--render" && i + 1 < argc) renderPath = argv[++i];
        else if (a == "--max-clean") maxClean = true;
        else if (a == "--max-tonal" && i + 1 < argc) options.maxTonalDeviationDb = std::atof(argv[++i]);
    }
    AudioFile input;
    std::string error;
    if (!readAudioFile(argv[1], input, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const AudioFile excerpt = loudestExcerpt(input, options.excerptSeconds);
    std::printf("Cible %.1f LUFS, plafond %.1f dBTP, %s (%.0f s)\n", options.targetLufs, ceiling,
                excerpt.numFrames() == input.numFrames() ? "morceau entier" : "passage le plus fort", excerpt.durationSeconds());

    std::vector<TuningResult> results;
    const auto all = strategies(ceiling);
    for (int idx = 0; idx < static_cast<int>(all.size()); ++idx) {
        if (only >= 0 && idx != only) continue;
        const auto& s = all[static_cast<std::size_t>(idx)];
        results.push_back(maxClean ? loudestClean(excerpt, s, options) : tuneStrategy(excerpt, s, options));
        const auto& r = results.back();
        std::printf("  %-30s gain %5.1f dB  I=%6.2f  TP=%6.2f  distorsion %6.1f dB (sub %6.1f | bas-méd %6.1f | médium %6.1f | aigu %6.1f)  tonal max %.1f dB%s%s\n",
                    s.name.c_str(), r.totalGainDb, r.integratedLufs, r.truePeakDb, r.distortion.weightedDb, r.distortion.subDb,
                    r.distortion.lowMidDb, r.distortion.midDb, r.distortion.highDb, r.tonal.maxDeviationDb,
                    r.reached ? "" : "  [cible non atteinte]", r.tonalOk ? "" : "  [ÉQUILIBRE TONAL DÉTRUIT]");
        std::printf("    %s", r.tonal.toText().c_str());
        std::fflush(stdout);
    }
    const auto best = std::min_element(results.begin(), results.end(), [maxClean](const TuningResult& a, const TuningResult& b) {
        // Respect de la prod d'abord, puis cible atteinte, puis propreté.
        if (a.tonalOk != b.tonalOk) return a.tonalOk;
        if (a.reached != b.reached) return a.reached;
        // --max-clean : la plus forte des sonies qui respectent la prod.
        if (maxClean && !a.reached && std::fabs(a.integratedLufs - b.integratedLufs) > 0.1) return a.integratedLufs > b.integratedLufs;
        return a.distortion.weightedDb < b.distortion.weightedDb;
    });
    std::printf("Le plus propre : %s\n", best->strategy.name.c_str());
    for (const auto& [k, v] : settingsOf(*best)) std::printf("  --set %s=%.2f\n", k.c_str(), v);

    if (!renderPath.empty()) {
        const AudioFile full = renderWithSettings(input, settingsOf(*best));
        if (!writeWav(renderPath, full, SampleFormat::Pcm24, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        const auto rep = analyze(full);
        std::printf("Rendu complet : %s  I=%.1f LUFS  TP=%.2f dBTP\n", renderPath.c_str(), rep.integratedLufs, rep.truePeakDb);
    }
    return 0;
}
