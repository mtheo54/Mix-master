// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/match — « Fais sonner ma prod comme ce titre, sans la détruire. »
//
//  Usage : omnis_match prod.wav [reference.wav] [--demande "texte"]
//                     [--db refs_utilisateur.txt] [--lexique lexique_utilisateur.txt]
//                     [--resemblance 50] [--target -6] [--ceiling -0.3]
//                     [--full] [--render master.wav]
//
//   1. Comprend la demande (ai::understandRequest : fautes, nuances, artiste,
//      titre ou genre cité). La référence vient du son fourni, sinon de la
//      base ; un artiste inconnu sans son -> la question est posée et rien
//      n'est rendu. Un son fourni pour un artiste inconnu est ajouté à la base
//      utilisateur (--db) ;
//   2. écoute la référence et la prod (analysis::SpectralSignature) ;
//   3. en déduit l'intention : déplacement partiel et borné (ai::planMatch),
//      expliqué en clair ;
//   4. règle la tenue dans le temps (multibande, ai::planDynamics) d'après la
//      variation de chaque bande dans la référence, compensation recalée par
//      mesure pour rendre à chaque bande son énergie ;
//   5. cherche la sonie la plus forte, jusqu'à la cible, qui respecte cette
//      intention (garde tonale mesurée par rapport à la prod passée dans
//      l'EQ et le multibande) ; témoins : sans rien, EQ seul ;
//   6. rend le master et mesure la ressemblance avant / après.
// =============================================================================
#include "AudioRead.h"
#include "AiFiles.h"
#include "AnalysisReport.h"
#include "LoudnessTuner.h"
#include "TonalBalance.h"
#include "WavFile.h"

#include "ai/DynamicsIntent.h"
#include "ai/MatchIntent.h"
#include "analysis/BandLevels.h"
#include "analysis/SpectralSignature.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <optional>
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
    if (argc < 2) {
        std::puts("Abrasion Labs // OMNIS — adaptation à une référence\n"
                  "  omnis_match prod.wav [reference.wav] [--demande \"texte\"] [--db refs.txt] [--lexique lexique.txt]\n"
                  "              [--resemblance 50] [--target -6] [--ceiling -0.3] [--full] [--render master.wav]");
        return 1;
    }
    std::string demande, renderPath, refPath, dbPath, lexiconPath;
    double ceiling = -0.3, resemblance = -1.0, target = 1.0;
    TuningOptions options;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--demande" && i + 1 < argc) demande = argv[++i];
        else if (a == "--db" && i + 1 < argc) dbPath = argv[++i];
        else if (a == "--lexique" && i + 1 < argc) lexiconPath = argv[++i];
        else if (i == 2 && a.rfind("--", 0) != 0) refPath = a;
        else if (a == "--resemblance" && i + 1 < argc) resemblance = std::atof(argv[++i]) * 0.01;
        else if (a == "--target" && i + 1 < argc) target = std::atof(argv[++i]);
        else if (a == "--ceiling" && i + 1 < argc) ceiling = std::atof(argv[++i]);
        else if (a == "--full") options.excerptSeconds = 1e9;
        else if (a == "--render" && i + 1 < argc) renderPath = argv[++i];
    }

    AudioFile prod, ref;
    std::string error;
    if (!readAudioFile(argv[1], prod, error) || (!refPath.empty() && !readAudioFile(refPath, ref, error))) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    // ---- 1. Demande ------------------------------------------------------------
    const auto lexicon = loadLexicon(lexiconPath);
    const auto library = loadLibrary(dbPath);
    const auto understanding = ai::understandRequest(demande, lexicon, library, !refPath.empty());
    auto request = understanding.request;
    if (resemblance >= 0.0) request.resemblance = resemblance;
    if (target <= 0.0) request.targetLufs = target;
    std::printf("Demande : « %s »\n", demande.c_str());
    for (const auto& s : request.understood) std::printf("  - %s\n", s.c_str());
    for (const auto& s : request.pending) std::printf("  - (à venir) %s\n", s.c_str());
    if (understanding.suggestion) std::printf("  - Conseil : %s\n", understanding.suggestion->c_str());
    if (understanding.question) {
        std::printf("\nQuestion : %s\n", understanding.question->c_str());
        return 2;
    }

    // ---- 2-3. Écoute et intention ----------------------------------------------
    const auto sigProd = signatureOf(prod);
    std::optional<analysis::SpectralSignature> sigRefOpt;
    if (!refPath.empty()) {
        sigRefOpt = signatureOf(ref);
        // Un son fourni pour un artiste que la base ne connaissait pas : on l'apprend.
        if (!understanding.unknownReferences.empty() && !dbPath.empty()) {
            ai::ReferenceLibrary user;
            std::string text;
            if (readTextFile(dbPath, text)) user.parse(text);
            ai::ReferenceEntry e;
            e.artist = understanding.unknownReferences.front();
            e.signature = *sigRefOpt;
            user.add(e);
            if (writeTextFile(dbPath, user.serialize()))
                std::printf("  - « %s » ajouté à ta base de références (%s)\n", e.artist.c_str(), dbPath.c_str());
        }
    } else if (understanding.hasReference()) {
        sigRefOpt = ai::aggregateSignature(understanding.referenceEntries);
    }
    const auto intent = sigRefOpt ? ai::planMatch(sigProd, *sigRefOpt, request) : ai::planFromWords(sigProd, request);
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
    // ---- 4 bis. Tenue dans le temps (multibande), réglée sur l'entrée du multibande.
    const auto levelsOf = [&](const AudioFile& f) {
        return analysis::measureBandLevels(f.channels[0].data(), f.channels[1].data(), f.numFrames(), f.sampleRate, {120.0, 800.0, 5000.0});
    };
    Settings dyn;
    AudioFile intentionMb;
    if (sigRefOpt) {
        auto dynamics = ai::planDynamics(sigProd, *sigRefOpt, levelsOf(intention), request, understanding.interpretation.desire);
        // Compensation recalée par mesure : chaque bande retrouve son énergie moyenne.
        const auto energy = levelsOf(intention);
        for (int pass = 0; pass < 3; ++pass) {
            const auto out = levelsOf(renderWithSettings(excerpt, concat(concat(eq, ai::dynamicsSettings(dynamics)), {{"lim_ceiling", 0.0}})));
            for (int b = 0; b < params::kMbBands; ++b) {
                auto& band = dynamics.bands[static_cast<std::size_t>(b)];
                if (band.active()) band.makeupDb = std::clamp(band.makeupDb + energy.meanDb[static_cast<std::size_t>(b)] - out.meanDb[static_cast<std::size_t>(b)], -12.0, 12.0);
            }
        }
        std::printf("\nTenue dans le temps (multibande) :\n");
        for (const auto& line : dynamics.explanation) std::printf("  - %s\n", line.c_str());
        for (int b = 0; b < params::kMbBands; ++b)
            if (dynamics.bands[static_cast<std::size_t>(b)].active())
                std::printf("    %s : compensation %+.1f dB\n", ai::mbBandName(b), dynamics.bands[static_cast<std::size_t>(b)].makeupDb);
        dyn = ai::dynamicsSettings(dynamics);
    }
    intentionMb = renderWithSettings(excerpt, concat(concat(eq, dyn), {{"lim_ceiling", 0.0}}));
    TuningOptions held = options;
    held.tonalReference = &intentionMb;

    const TuningStrategy witness{"sans Match EQ (témoin)", base, {{"clip_drive", 0.7}, {"lim_gain", 0.3}}};
    const TuningStrategy eqClip{"Match EQ + clip 70/30", concat(base, eq), {{"clip_drive", 0.7}, {"lim_gain", 0.3}}};
    const TuningStrategy mbLim{"EQ + multibande + limiteur", concat(concat(base, eq), dyn), {{"lim_gain", 1.0}}};
    const TuningStrategy mbClip{"EQ + multibande + clip 70/30", concat(concat(base, eq), dyn), {{"clip_drive", 0.7}, {"lim_gain", 0.3}}};
    auto fWitness = std::async(std::launch::async, [&] { return loudestClean(excerpt, witness, options); });
    auto fEq = std::async(std::launch::async, [&] { return loudestClean(excerpt, eqClip, matched); });
    auto fMbLim = std::async(std::launch::async, [&] { return loudestClean(excerpt, mbLim, held); });
    auto fMbClip = std::async(std::launch::async, [&] { return loudestClean(excerpt, mbClip, held); });
    const TuningResult rWitness = fWitness.get(), rEq = fEq.get(), rMbLim = fMbLim.get(), rMbClip = fMbClip.get();
    std::printf("\nCible %.1f LUFS (plafond), plafond true peak %.1f dBTP, tolérance %.1f dB, %s (%.0f s)\n", options.targetLufs, ceiling,
                options.maxTonalDeviationDb, excerpt.numFrames() == prod.numFrames() ? "morceau entier" : "passage le plus fort",
                excerpt.durationSeconds());
    printResult(witness.name.c_str(), rWitness);
    printResult(eqClip.name.c_str(), rEq);
    printResult(mbLim.name.c_str(), rMbLim);
    printResult(mbClip.name.c_str(), rMbClip);
    // La tenue fait partie de l'intention (trait persistant de la référence) :
    // dès que le multibande est actif, on garde la meilleure de ses deux
    // variantes (la plus forte ; à 0.1 LU près, la plus propre). L'EQ seul ne
    // sert que de témoin. Sans tenue à prendre, l'EQ seul est retenu.
    const bool holding = std::any_of(dyn.begin(), dyn.end(), [](const auto& kv) {
        return kv.first.find("_ratio") != std::string::npos && kv.second > 1.001;
    });
    const TuningResult* best = holding ? &rMbClip : &rEq;
    if (holding && (rMbLim.integratedLufs > rMbClip.integratedLufs + 0.1 ||
                    (std::fabs(rMbLim.integratedLufs - rMbClip.integratedLufs) <= 0.1 &&
                     rMbLim.distortion.weightedDb < rMbClip.distortion.weightedDb)))
        best = &rMbLim;
    std::printf("Retenu : %s (%+.1f LU par rapport au témoin)\n", best->strategy.name.c_str(), best->integratedLufs - rWitness.integratedLufs);
    std::printf("    vs intention : %s", best->tonal.toText().c_str());
    if (const char* dump = std::getenv("OMNIS_SETTINGS_OUT")) {
        if (FILE* f = std::fopen(dump, "w")) {
            for (const auto& [k, v] : settingsOf(*best)) std::fprintf(f, "%s %.6f\n", k.c_str(), v);
            std::fclose(f);
        }
    }

    // ---- 5. Rendu et ressemblance -------------------------------------------------
    if (!renderPath.empty()) {
        const AudioFile master = renderWithSettings(prod, settingsOf(*best));
        if (!writeWav(renderPath, master, SampleFormat::Pcm24, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        const auto rep = analyze(master);
        const auto sigOut = signatureOf(master);
        std::printf("\nRendu : %s  I=%.1f LUFS  TP=%.2f dBTP  LRA=%.1f LU\n", renderPath.c_str(), rep.integratedLufs, rep.truePeakDb,
                    rep.loudnessRangeLu);
        if (sigRefOpt)
            std::printf("Distance tonale à la référence : %.1f dB -> %.1f dB (prod -> master)\n", ai::tonalDistanceDb(sigProd, *sigRefOpt),
                        ai::tonalDistanceDb(sigOut, *sigRefOpt));
        std::printf("Changement par rapport à la prod :\n    %s", compareTonalBalance(prod, master).toText().c_str());
    }
    return 0;
}
