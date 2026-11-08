// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/auto/main.cpp — AUTO du plug-in, hors ligne, sur un fichier.
//
//  Ce que fait le bouton AUTO (OmnisController::pollAutomation) : la signature
//  de 10 s écoutées, ai::planAuto vers un style, puis le volume mené à la cible
//  par la mesure (gain du limiteur). Ici : les 10 s les plus fortes (ou tout le
//  morceau avec --entier), et le gain du limiteur cherché sur le passage écouté.
//  Sert à reproduire et à mesurer ce qu'OMNIS fait d'une prod (docs/REFONTE.md).
//
//      omnis_auto prod.wav sortie.wav [--style "rap fr"] [--entier] [--set cle=valeur ...]
// =============================================================================
#include "AudioRead.h"
#include "WavFile.h"

#include "ai/ConsoleActions.h"
#include "ai/ReferenceLibrary.h"
#include "analysis/LoudnessMeter.h"
#include "analysis/MusicDescriptors.h"
#include "analysis/SpectralSignature.h"
#include "engine/Engine.h"
#include "engine/MasteringChain.h"
#include "params/Parameters.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace omnis;
using namespace omnis::tools;
using params::ParamId;

namespace {

void usage() {
    std::puts("Abrasion Labs // OMNIS — AUTO hors ligne\n"
              "  omnis_auto prod.wav sortie.wav [--style \"rap fr\"] [--entier] [--set cle=valeur ...]\n"
              "  Les 10 s les plus fortes (ou tout le morceau avec --entier) -> réglages d'AUTO -> volume mené à la cible -> rendu.");
}

/// Rendu complet (latence retirée) avec les réglages donnés.
AudioFile render(const AudioFile& in, const std::map<ParamId, double>& settings) {
    Engine engine;
    buildMasteringChain(engine);
    engine.prepare(in.sampleRate, 1024);
    for (const auto& [id, v] : settings) engine.setParameterImmediate(id, v);
    const int latency = engine.latencySamples(), frames = in.numFrames(), total = frames + latency;
    std::vector<double> l(static_cast<std::size_t>(total), 0.0), r(static_cast<std::size_t>(total), 0.0);
    std::copy(in.channels[0].begin(), in.channels[0].end(), l.begin());
    std::copy(in.channels[1].begin(), in.channels[1].end(), r.begin());
    AudioFile out;
    out.sampleRate = in.sampleRate;
    out.resize(2, total);
    for (int pos = 0; pos < total; pos += 1024) {
        const int n = std::min(1024, total - pos);
        const double* src[2] = {l.data() + pos, r.data() + pos};
        double* dst[2] = {out.channels[0].data() + pos, out.channels[1].data() + pos};
        engine.process(src, dst, n);
    }
    for (auto& ch : out.channels) ch.erase(ch.begin(), ch.begin() + latency);
    return out;
}

double lufs(const AudioFile& a, int from, int count) {
    analysis::LoudnessMeter m;
    m.prepare(a.sampleRate, 2);
    const double* ch[2] = {a.channels[0].data() + from, a.channels[1].data() + from};
    m.process(ch, count);
    return m.integratedLufs();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 1;
    }
    const std::string inPath = argv[1], outPath = argv[2];
    std::string style;
    bool whole = false;
    std::map<ParamId, double> forced;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--style" && i + 1 < argc) style = argv[++i];
        else if (a == "--entier") whole = true;
        else if (a == "--set" && i + 1 < argc) {
            const std::string kv = argv[++i];
            const auto eq = kv.find('=');
            const auto* spec = eq == std::string::npos ? nullptr : params::findParameterByKey(kv.substr(0, eq));
            const auto value = spec ? params::parseValue(*spec, kv.substr(eq + 1)) : std::nullopt;
            if (!value) {
                std::fprintf(stderr, "réglage inconnu : %s\n", kv.c_str());
                return 1;
            }
            forced[spec->id] = *value;
        } else {
            usage();
            return 1;
        }
    }
    AudioFile input;
    std::string error;
    if (!readAudioFile(inPath, input, error)) {
        std::fprintf(stderr, "%s : %s\n", inPath.c_str(), error.c_str());
        return 1;
    }
    if (input.numChannels() == 1) input.channels.push_back(input.channels[0]);
    input.channels.resize(2);
    const int frames = input.numFrames();

    // Le passage écouté : les 10 s les plus fortes (pas de 1 s), comme un refrain joué.
    int from = 0, count = frames;
    if (!whole) {
        const int win = std::min(frames, static_cast<int>(10.0 * input.sampleRate)), step = static_cast<int>(input.sampleRate);
        double best = -1.0;
        for (int p = 0; p + win <= frames; p += step) {
            double e = 0.0;
            for (int i = p; i < p + win; ++i) e += input.channels[0][static_cast<std::size_t>(i)] * input.channels[0][static_cast<std::size_t>(i)] +
                                                  input.channels[1][static_cast<std::size_t>(i)] * input.channels[1][static_cast<std::size_t>(i)];
            if (e > best) best = e, from = p;
        }
        count = win;
    }
    const double* l = input.channels[0].data() + from;
    const double* r = input.channels[1].data() + from;
    auto signature = analysis::analyzeSignature(l, r, count, input.sampleRate);
    signature.music = analysis::analyzeMusic(l, r, count, input.sampleRate);
    std::printf("passage écouté : %.1f s à %.1f s (%.1f LUFS)\n", from / input.sampleRate, (from + count) / input.sampleRate, lufs(input, from, count));

    std::map<ParamId, double> settings;
    for (const auto& spec : params::allParameters())
        if (params::isPersistent(spec)) settings[spec.id] = spec.defaultValue;
    const auto current = [&](ParamId id) { const auto it = settings.find(id); return it == settings.end() ? 0.0 : it->second; };
    const auto plan = ai::planAuto(signature, ai::ReferenceLibrary::builtin(), style, current);
    for (const auto& line : plan.said) std::printf("  %s\n", line.c_str());
    std::puts("réglages :");
    for (const auto& [id, v] : plan.settings) {
        settings[id] = v;
        const auto* spec = params::findParameter(id);
        if (spec && std::fabs(v - spec->defaultValue) > 1e-9) std::printf("  %-16s %s\n", std::string(spec->key).c_str(), params::formatValue(*spec, v, true).c_str());
    }
    for (const auto& [id, v] : forced) settings[id] = v;

    // Le volume mené à la cible sur le passage écouté (comme le contrôleur, ±0,4 LU).
    AudioFile out = render(input, settings);
    if (plan.targetLufs && !forced.count(ParamId::LimGain)) {
        for (int step = 0; step < 8; ++step) {
            const double err = *plan.targetLufs - lufs(out, from, count);
            if (std::fabs(err) < 0.4) break;
            settings[ParamId::LimGain] = std::clamp(settings[ParamId::LimGain] + err, 0.0, 24.0);
            out = render(input, settings);
        }
        std::printf("volume : cible %.1f LUFS, limiteur +%.1f dB, passage écouté %.1f LUFS\n", *plan.targetLufs, settings[ParamId::LimGain], lufs(out, from, count));
    }
    if (!writeWav(outPath, out, SampleFormat::Pcm24, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("écrit : %s\n", outPath.c_str());
    return 0;
}
