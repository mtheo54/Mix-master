// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/stems — Ce qu'OMNIS reconnaît et fait sur des stems exportés.
//
//  Usage : omnis_stems fichier.wav [...] [--render dossier] [--manual element] [--accept]
//  Pour chaque fichier : l'élément (nom du fichier + écoute), son caractère,
//  les réglages Auto de sa chaîne, puis la chaîne passée sur tout le fichier
//  (comme dans le DAW, blocs de 512) : niveau, crête, bandes avant / après,
//  compression et coupes d'EQ dynamique au fil du morceau.
//  --render : écrit le stem traité (WAV 32 bits flottants) dans le dossier.
//  Plusieurs fichiers (les stems d'une même prod, alignés au début) : le chef
//  d'orchestre (stems/Conductor) les écoute ensemble, après leur chaîne, comme
//  le master, et fait ses propositions. --accept : les accepte toutes et
//  refait la chaîne (et le rendu) des stems retouchés.
// =============================================================================
#include "AudioRead.h"
#include "WavFile.h"

#include "stems/Conductor.h"
#include "stems/Element.h"
#include "stems/DrumBus.h"
#include "stems/ElementClassifier.h"
#include "stems/StemChain.h"
#include "stems/StemSettings.h"
#include "stems/StemTimeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace omnis;
using namespace omnis::tools;
using namespace omnis::stems;

namespace {

double db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

struct Levels {
    double rmsDb = 0.0, peakDb = 0.0, sideToMidDb = 0.0;
    std::array<double, kAnalysisBands> bandDb{};
};

/// Niveaux par bande (mêmes limites que le classifieur), filtres SVF passe-bande en cascade.
Levels measure(const std::vector<double>& l, const std::vector<double>& r, double fs) {
    static constexpr std::array<double, kAnalysisBands + 1> edges{20.0, 60.0, 150.0, 400.0, 2000.0, 6000.0, 20000.0};
    Levels out;
    double sum = 0.0, peak = 0.0, mid = 0.0, side = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i) {
        sum += 0.5 * (l[i] * l[i] + r[i] * r[i]);
        peak = std::max({peak, std::fabs(l[i]), std::fabs(r[i])});
        const double m = 0.5 * (l[i] + r[i]), s = 0.5 * (l[i] - r[i]);
        mid += m * m;
        side += s * s;
    }
    out.rmsDb = 10.0 * std::log10(std::max(sum / static_cast<double>(l.size()), 1e-24));
    out.peakDb = db(peak);
    out.sideToMidDb = 10.0 * std::log10(std::max(side, 1e-24) / std::max(mid, 1e-24));
    for (int b = 0; b < kAnalysisBands; ++b) {
        const double lo = edges[static_cast<std::size_t>(b)], hi = edges[static_cast<std::size_t>(b) + 1];
        dsp::SvfFilter<1> hp1, hp2, lp1, lp2;
        hp1.set(dsp::SvfType::HighPass, fs, lo, dsp::butterworthQ(4, 0));
        hp2.set(dsp::SvfType::HighPass, fs, lo, dsp::butterworthQ(4, 1));
        lp1.set(dsp::SvfType::LowPass, fs, hi, dsp::butterworthQ(4, 0));
        lp2.set(dsp::SvfType::LowPass, fs, hi, dsp::butterworthQ(4, 1));
        double p = 0.0;
        for (std::size_t i = 0; i < l.size(); ++i) {
            const double m = 0.5 * (l[i] + r[i]);
            const double y = lp2.process(0, lp1.process(0, hp2.process(0, hp1.process(0, m))));
            p += y * y;
        }
        out.bandDb[static_cast<std::size_t>(b)] = 10.0 * std::log10(std::max(p / static_cast<double>(l.size()), 1e-24));
    }
    return out;
}

const char* bandName(int b) {
    static const char* names[] = {"20-60", "60-150", "150-400", "400-2k", "2k-6k", "6k-20k"};
    return names[b];
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> files;
    std::string renderDir;
    Element manual = Element::Auto;
    bool acceptAll = false;
    struct Done {
        std::string name;
        AudioFile dry;
        StemSettings settings;
        Element element = Element::Auto;
        StemTimeline timeline;
    };
    std::vector<Done> done;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--render" && i + 1 < argc) renderDir = argv[++i];
        else if (a == "--accept") acceptAll = true;
        else if (a == "--manual" && i + 1 < argc) {
            if (const auto e = elementFromKey(argv[++i])) manual = *e;
        } else files.push_back(a);
    }
    if (files.empty()) {
        std::puts("Abrasion Labs // OMNIS — stems\n  omnis_stems fichier.wav [...] [--render dossier] [--manual element]");
        return 1;
    }
    int status = 0;
    for (const auto& path : files) {
        AudioFile f;
        std::string error;
        if (!readAudioFile(path, f, error) || f.channels.empty()) {
            std::fprintf(stderr, "%s : %s\n", path.c_str(), error.c_str());
            status = 1;
            continue;
        }
        if (f.channels.size() == 1) f.channels.push_back(f.channels[0]);
        auto& l = f.channels[0];
        auto& r = f.channels[1];
        const double fs = f.sampleRate;
        const std::string name = std::filesystem::path(path).stem().string();

        // 1. Reconnaissance (comme le thread du lien : par blocs).
        ElementClassifier classifier;
        classifier.prepare(fs);
        DrumBusAnalyzer drums;
        drums.prepare(fs);
        for (std::size_t i = 0; i < l.size(); i += 2048) {
            const int n = static_cast<int>(std::min<std::size_t>(2048, l.size() - i));
            classifier.push(l.data() + i, r.data() + i, n);
            drums.push(l.data() + i, r.data() + i, n);
        }
        const Identification id = classifier.identify(manual, name);
        const Character& c = id.character;
        std::printf("== %s (%.1f s)\n", name.c_str(), static_cast<double>(l.size()) / fs);
        std::printf("   élément : %s   (écoute : %s, confiance %.2f ; nom : %s « %s »%s%s)\n", elementLabel(id.element),
                    elementLabel(id.heard), id.confidence, id.name.strength > 0 ? elementLabel(id.name.element) : "—",
                    id.name.matched.c_str(), id.nameDisagrees ? ", désaccord" : "", id.manual ? ", choisi" : "");
        std::printf("   caractère : crête/RMS %.1f dB, bruit %.2f, décroissance %.0f ms, montée %.1f ms, %.1f coups/s,\n"
                    "               Side/Mid %.1f dB, centre %.0f Hz\n",
                    c.crestDb, c.noisiness, c.decayMs, c.attackMs, c.onsetsPerSecond, c.sideToMidDb, c.brightnessHz);
        std::printf("   part par bande :");
        for (int b = 0; b < kAnalysisBands; ++b) std::printf(" %s %.0f%%", bandName(b), 100.0 * c.bandShare[static_cast<std::size_t>(b)]);
        std::puts("");

        if (id.element == Element::DrumLoop) {
            const auto& dc = drums.content();
            std::printf("   dans le bus : kick %d coups, snare / clap %d, hi-hat %d, open hat %d\n", dc.kick.hits, dc.snare.hits, dc.hat.hits,
                        dc.openHat.hits);
            for (const auto& line : describeDrumBus(dc)) std::printf("     - %s\n", line.c_str());
        }

        // 2. Réglages Auto.
        const StemSettings s = settingsFor(StemMode::Auto, id.element, c, id.secondsHeard, {}, &drums.content());
        const StemSettings t = templateFor(id.element);
        std::printf("   réglages Auto : passe-haut %s", s.hpfHz > 0 ? "" : "aucun");
        if (s.hpfHz > 0) std::printf("%.0f Hz %s%s", s.hpfHz, s.hpfSteep ? "24" : "12", " dB/oct");
        std::printf(" ; compression %.1f:1, %.1f dB (modèle %.1f) ; saturation %.0f %% (modèle %.0f %%) ;\n", s.compRatio,
                    s.compDepthDb, t.compDepthDb, 100.0 * s.saturation, 100.0 * t.saturation);
        std::printf("                   mono < %.0f Hz à %.0f %%, largeur %.0f %% ; EQ dynamique :", s.monoBelowHz, 100.0 * s.monoAmount, 100.0 * s.width);
        for (const auto& b : s.dynamic)
            if (b.maxCutDb > 0) std::printf(" %.0f Hz (≤ %.0f dB)", b.hz, b.maxCutDb);
        std::puts("");

        // 3. La chaîne sur tout le fichier.
        Done keep;
        keep.name = name;
        keep.dry = f;
        keep.settings = s;
        keep.element = id.element;
        const Levels before = measure(l, r, fs);
        StemChain chain;
        chain.prepare(fs);
        chain.setSettings(s);
        std::array<double, kDynamicBands> cutSum{}, cutMax{};
        std::array<int, kDynamicBands> cutBlocks{};
        double grSum = 0.0, grMax = 0.0;
        int blocks = 0, grBlocks = 0;
        for (std::size_t i = 0; i < l.size(); i += 512) {
            const int n = static_cast<int>(std::min<std::size_t>(512, l.size() - i));
            chain.process(l.data() + i, r.data() + i, n);
            ++blocks;
            const double gr = chain.compressionDb();
            grSum += gr;
            grMax = std::max(grMax, gr);
            if (gr > 0.5) ++grBlocks;
            for (int b = 0; b < kDynamicBands; ++b) {
                const double cut = chain.dynamicCutDb(b);
                cutSum[static_cast<std::size_t>(b)] += cut;
                cutMax[static_cast<std::size_t>(b)] = std::max(cutMax[static_cast<std::size_t>(b)], cut);
                if (cut > 0.5) ++cutBlocks[static_cast<std::size_t>(b)];
            }
        }
        const Levels after = measure(l, r, fs);
        std::printf("   après : niveau %+.2f dB, crête %.2f -> %.2f dBFS, Side/Mid %.1f -> %.1f dB\n", after.rmsDb - before.rmsDb,
                    before.peakDb, after.peakDb, before.sideToMidDb, after.sideToMidDb);
        std::printf("   bandes (après - avant) :");
        for (int b = 0; b < kAnalysisBands; ++b)
            std::printf(" %s %+.1f", bandName(b), after.bandDb[static_cast<std::size_t>(b)] - before.bandDb[static_cast<std::size_t>(b)]);
        std::puts(" dB");
        if (s.compRatio > 1.0 && s.compDepthDb > 0.0)
            std::printf("   compression : moyenne %.1f dB, max %.1f dB, active (> 0,5 dB) %.0f %% du temps\n", grSum / blocks, grMax,
                        100.0 * grBlocks / blocks);
        for (int b = 0; b < kDynamicBands; ++b) {
            const auto i = static_cast<std::size_t>(b);
            if (s.dynamic[i].maxCutDb <= 0.0) continue;
            std::printf("   EQ dynamique %.0f Hz : moyenne %.1f dB, max %.1f dB, active %.0f %% du temps\n", s.dynamic[i].hz,
                        cutSum[i] / blocks, cutMax[i], 100.0 * cutBlocks[i] / blocks);
        }
        if (!renderDir.empty()) {
            std::filesystem::create_directories(renderDir);
            const auto out = std::filesystem::path(renderDir) / (name + "_omnis.wav");
            if (writeWav(out.string(), f, SampleFormat::Float32, error)) std::printf("   rendu : %s\n", out.string().c_str());
            else std::fprintf(stderr, "   rendu impossible : %s\n", error.c_str());
        }
        // Ce que le master entendrait : le stem après sa chaîne.
        keep.timeline.prepare(fs);
        keep.timeline.push(l.data(), r.data(), static_cast<int>(l.size()), 0);
        keep.timeline.flush();
        done.push_back(std::move(keep));
        std::puts("");
    }

    if (done.size() >= 2) {
        std::vector<ConductorStem> input;
        for (std::size_t i = 0; i < done.size(); ++i) input.push_back({i + 1, done[i].element, done[i].name, &done[i].timeline});
        const auto proposals = Conductor::propose(input);
        std::printf("== Chef d'orchestre (%zu stems) : %zu proposition(s)\n", done.size(), proposals.size());
        for (const auto& p : proposals) std::printf("   - %s\n", p.text.c_str());
        if (proposals.empty()) std::puts("   (aucun stem n'en gêne un autre)");
        for (const auto& p : Conductor::evaluate(input))
            if (p.correction.type == Correction::Type::None)
                std::printf("   bilan : %s dans la zone de %s (%.0f-%.0f Hz) : au plus près %+.1f dB du seuil de gêne, %.0f s ensemble\n",
                            done[static_cast<std::size_t>(p.target)].name.c_str(), done[static_cast<std::size_t>(p.victim)].name.c_str(),
                            p.zoneLowHz, p.zoneHighHz, p.excessDb, p.sharedSeconds);
        for (const auto& p : Conductor::evaluate(input))
            for (const auto& b : p.bands)
                std::printf("      %s / %s %4.0f-%4.0f Hz : écart %+6.1f dB (seuil %+.0f), %d blocs\n", done[static_cast<std::size_t>(p.target)].name.c_str(),
                            done[static_cast<std::size_t>(p.victim)].name.c_str(), b.lowHz, b.highHz, b.gapDb, b.thresholdDb, b.blocks);
        if (acceptAll && !proposals.empty()) {
            std::puts("\n== Propositions acceptées");
            for (std::size_t t = 0; t < done.size(); ++t) {
                StemSettings s = done[t].settings;
                int count = 0;
                for (const auto& p : proposals)
                    if (p.target == static_cast<int>(t) && count < kMaxCorrections) s.corrections[static_cast<std::size_t>(count++)] = p.correction;
                if (count == 0) continue;
                s.active = true;
                AudioFile f = done[t].dry;
                auto& l = f.channels[0];
                auto& r = f.channels[1];
                StemChain chain;
                chain.prepare(f.sampleRate);
                chain.setSettings(s);
                for (std::size_t i = 0; i < l.size(); i += 512)
                    chain.process(l.data() + i, r.data() + i, static_cast<int>(std::min<std::size_t>(512, l.size() - i)));
                StemTimeline after;
                after.prepare(f.sampleRate);
                after.push(l.data(), r.data(), static_cast<int>(l.size()), 0);
                after.flush();
                done[t].timeline = std::move(after);
                std::printf("   %s : %d retouche(s) appliquée(s)\n", done[t].name.c_str(), count);
                if (!renderDir.empty()) {
                    const auto out = std::filesystem::path(renderDir) / (done[t].name + "_omnis_retouche.wav");
                    std::string error;
                    if (writeWav(out.string(), f, SampleFormat::Float32, error)) std::printf("   rendu : %s\n", out.string().c_str());
                }
            }
            input.clear();
            for (std::size_t i = 0; i < done.size(); ++i) input.push_back({i + 1, done[i].element, done[i].name, &done[i].timeline});
            const auto again = Conductor::propose(input);
            std::printf("   après : %zu proposition(s) restante(s)\n", again.size());
            for (const auto& p : again) std::printf("   - %s\n", p.text.c_str());
        }
    }
    return status;
}
