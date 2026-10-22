// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/match — « Fais sonner ma prod comme ce titre, sans la détruire. »
//
//  Usage : omnis_match prod.wav reference.wav [--demande "texte"]
//                     [--resemblance 50] [--target -6] [--ceiling -0.3]
//                     [--full] [--render master.wav]
//
//   1. Lit la demande (ai::parseRequest) ;
//   2. écoute la référence et la prod (analysis::SpectralSignature) ;
//   3. en déduit l'intention : déplacement partiel et borné (ai::planMatch),
//      expliqué en clair ;
//   4. cherche la sonie la plus forte, jusqu'à la cible, qui respecte cette
//      intention (garde tonale mesurée par rapport à la prod passée dans
//      l'EQ seul) ; la même recherche sans Match EQ sert de témoin ;
//   5. rend le master et mesure la ressemblance avant / après.
// =============================================================================
#include "AnalysisReport.h"
#include "LoudnessTuner.h"
#include "TonalBalance.h"
#include "WavFile.h"

#include "ai/MatchIntent.h"
#include "analysis/SpectralSignature.h"

#include <cstdio>
#include <cstdlib>
#include <future>
#include <string>
#include <vector>

using namespace omnis;
using namespace omnis::tools;

namespace {

using Settings = std::vector<std::pair<std::string, double>>;

analysis::SpectralSignature signatureOf(const AudioFile& f) {
    const auto& l = f.channels[0];
    const auto& r = f.channels[std::min<std::size_t>(1, f.channels.size() - 1)];
    return analysis::analyzeSignature(l.data(), r.data(), f.numFrames(), f.sampleRate);
}

Settings matchSettings(const ai::MatchIntent& intent) {
    Settings s{{"match_amount", 100.0}};
    for (int k = 0; k < params::kMatchBands; ++k) {
        s.emplace_back(std::string(params::findParameter(params::matchMidId(k))->key), intent.midDb[static_cast<std::size_t>(k)]);
        s.emplace_back(std::string(params::findParameter(params::matchSideId(k))->key), intent.sideDb[static_cast<std::size_t>(k)]);
    }
    return s;
}

Settings concat(Settings a, const Settings& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

void printResult(const char* label, const TuningResult& r) {
    std::printf("  %-34s I=%6.2f LUFS  TP=%6.2f  distorsion %6.1f dB (médium %6.1f)  écart à l'intention %.1f dB\n", label,
                r.integratedLufs, r.truePeakDb, r.distortion.weightedDb, r.distortion.midDb, r.tonal.maxDeviationDb);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::puts("Abrasion Labs // OMNIS — adaptation à une référence\n"
                  "  omnis_match prod.wav reference.wav [--demande \"texte\"] [--resemblance 50] [--target -6]\n"
                  "              [--ceiling -0.3] [--full] [--render master.wav]");
        return 1;
    }
    std::string demande, renderPath;
    double ceiling = -0.3, resemblance = -1.0, target = 1.0;
    TuningOptions options;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--demande" && i + 1 < argc) demande = argv[++i];
        else if (a == "--resemblance" && i + 1 < argc) resemblance = std::atof(argv[++i]) * 0.01;
        else if (a == "--target" && i + 1 < argc) target = std::atof(argv[++i]);
        else if (a == "--ceiling" && i + 1 < argc) ceiling = std::atof(argv[++i]);
        else if (a == "--full") options.excerptSeconds = 1e9;
        else if (a == "--render" && i + 1 < argc) renderPath = argv[++i];
    }

    AudioFile prod, ref;
    std::string error;
    if (!readWav(argv[1], prod, error) || !readWav(argv[2], ref, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    // ---- 1. Demande ------------------------------------------------------------
    auto request = ai::parseRequest(demande);
    if (resemblance >= 0.0) request.resemblance = resemblance;
    if (target <= 0.0) request.targetLufs = target;
    std::printf("Demande : « %s »\n", demande.c_str());
    for (const auto& s : request.understood) std::printf("  - %s\n", s.c_str());

    // ---- 2-3. Écoute et intention ----------------------------------------------
    const auto sigProd = signatureOf(prod);
    const auto sigRef = signatureOf(ref);
    const auto intent = ai::planMatch(sigProd, sigRef, request);
    std::printf("\nPourquoi la référence sonne comme ça :\n");
    for (const auto& s : intent.referenceTraits) std::printf("  - %s\n", s.c_str());
    std::printf("Ce que l'on garde de ta prod :\n");
    for (const auto& s : intent.protectedTraits) std::printf("  - %s\n", s.c_str());
    std::printf("\nCourbe d'intention (écart référence | Mid appliqué | Side appliqué, dB) :\n");
    for (int k = 0; k < params::kMatchBands; ++k) {
        const double f = params::matchBandHz(k);
        std::printf("  %7.0f Hz  %+6.1f | %+5.1f | %+5.1f\n", f, intent.gapDb[static_cast<std::size_t>(k)],
                    intent.midDb[static_cast<std::size_t>(k)], intent.sideDb[static_cast<std::size_t>(k)]);
    }

    // ---- 4. Sonie la plus forte qui respecte l'intention -------------------------
    options.targetLufs = intent.targetLufs;
    options.maxTonalDeviationDb = request.maxTonalDeviationDb;
    const AudioFile excerpt = loudestExcerpt(prod, options.excerptSeconds);
    const Settings eq = matchSettings(intent);
    const Settings base{{"lim_ceiling", ceiling}};
    const AudioFile intention = renderWithSettings(excerpt, concat(eq, {{"lim_ceiling", 0.0}}));
    TuningOptions matched = options;
    matched.tonalReference = &intention;

    std::printf("\nCible %.1f LUFS (plafond), plafond true peak %.1f dBTP, tolérance %.1f dB, %s (%.0f s)\n", options.targetLufs, ceiling,
                options.maxTonalDeviationDb, excerpt.numFrames() == prod.numFrames() ? "morceau entier" : "passage le plus fort",
                excerpt.durationSeconds());
    const TuningStrategy witness{"sans Match EQ (témoin)", base, {{"clip_drive", 0.7}, {"lim_gain", 0.3}}};
    const TuningStrategy limOnly{"Match EQ + limiteur seul", concat(base, eq), {{"lim_gain", 1.0}}};
    const TuningStrategy clip{"Match EQ + clip 70/30", concat(base, eq), {{"clip_drive", 0.7}, {"lim_gain", 0.3}}};
    auto fWitness = std::async(std::launch::async, [&] { return loudestClean(excerpt, witness, options); });
    auto fLim = std::async(std::launch::async, [&] { return loudestClean(excerpt, limOnly, matched); });
    auto fClip = std::async(std::launch::async, [&] { return loudestClean(excerpt, clip, matched); });
    const TuningResult rWitness = fWitness.get(), rLim = fLim.get(), rClip = fClip.get();
    printResult(witness.name.c_str(), rWitness);
    printResult(limOnly.name.c_str(), rLim);
    printResult(clip.name.c_str(), rClip);
    const bool clipBetter = rClip.integratedLufs > rLim.integratedLufs + 0.1 ||
                            (std::fabs(rClip.integratedLufs - rLim.integratedLufs) <= 0.1 && rClip.distortion.weightedDb < rLim.distortion.weightedDb);
    const TuningResult& best = clipBetter ? rClip : rLim;
    std::printf("Retenu : %s (%+.1f LU par rapport au témoin)\n", best.strategy.name.c_str(), best.integratedLufs - rWitness.integratedLufs);
    std::printf("    vs intention : %s", best.tonal.toText().c_str());

    // ---- 5. Rendu et ressemblance -------------------------------------------------
    if (!renderPath.empty()) {
        const AudioFile master = renderWithSettings(prod, settingsOf(best));
        if (!writeWav(renderPath, master, SampleFormat::Pcm24, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        const auto rep = analyze(master);
        const auto sigOut = signatureOf(master);
        std::printf("\nRendu : %s  I=%.1f LUFS  TP=%.2f dBTP  LRA=%.1f LU\n", renderPath.c_str(), rep.integratedLufs, rep.truePeakDb,
                    rep.loudnessRangeLu);
        std::printf("Distance tonale à la référence : %.1f dB -> %.1f dB (prod -> master)\n", ai::tonalDistanceDb(sigProd, sigRef),
                    ai::tonalDistanceDb(sigOut, sigRef));
        std::printf("Changement par rapport à la prod :\n    %s", compareTonalBalance(prod, master).toText().c_str());
    }
    return 0;
}
