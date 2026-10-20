// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/render — Rendu hors ligne d'un WAV à travers le moteur OMNIS.
//
//  Exactement le même omnis::Engine que dans le plug-in. La latence de la
//  chaîne est compensée : la sortie est alignée échantillon par échantillon
//  sur l'entrée (null-test possible avec n'importe quel éditeur).
//
//  Usage :
//    omnis_render in.wav out.wav [--set cle=valeur ...] [--block 512]
//                 [--random-blocks] [--format f32|f64|pcm24] [--report]
//    omnis_render --params      (liste des clés de paramètres)
//
//  Exemple :
//    omnis_render mix.wav master.wav --set input_gain=+3dB --set monitor=Side
// =============================================================================
#include "AnalysisReport.h"
#include "WavFile.h"

#include "engine/Engine.h"
#include "params/Parameters.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace omnis;
using namespace omnis::tools;

namespace {

void usage() {
    std::puts("Abrasion Labs // OMNIS — rendu hors ligne\n"
              "  omnis_render in.wav out.wav [--set cle=valeur ...] [--block 512] [--random-blocks]\n"
              "               [--format f32|f64|pcm24|pcm16] [--report]\n"
              "  omnis_render --params");
}

void listParameters() {
    for (const auto& p : params::allParameters()) {
        if (!params::isPersistent(p)) continue;
        std::printf("  %-14s %-14s [%s .. %s] defaut %s", std::string(p.key).c_str(), std::string(p.name).c_str(),
                    params::formatValue(p, p.minValue, true).c_str(), params::formatValue(p, p.maxValue, true).c_str(),
                    params::formatValue(p, p.defaultValue, true).c_str());
        if (!p.choices.empty() && p.kind == params::ParamKind::Choice) {
            std::printf("  {");
            for (std::size_t i = 0; i < p.choices.size(); ++i)
                std::printf("%s%s", i ? ", " : "", std::string(p.choices[i]).c_str());
            std::printf("}");
        }
        std::printf("\n");
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--params") {
        listParameters();
        return 0;
    }
    if (argc < 3) {
        usage();
        return 1;
    }
    const std::string inPath = argv[1];
    const std::string outPath = argv[2];
    int blockSize = 512;
    bool randomBlocks = false;
    bool report = false;
    SampleFormat format = SampleFormat::Float32;
    std::vector<std::pair<const params::ParamSpec*, double>> settings;

    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--set" && i + 1 < argc) {
            const std::string kv = argv[++i];
            const auto eq = kv.find('=');
            const auto* spec = eq == std::string::npos ? nullptr : params::findParameterByKey(kv.substr(0, eq));
            if (!spec || !params::isPersistent(*spec)) {
                std::fprintf(stderr, "parametre inconnu : %s (voir --params)\n", kv.c_str());
                return 1;
            }
            const auto value = params::parseValue(*spec, kv.substr(eq + 1));
            if (!value) {
                std::fprintf(stderr, "valeur invalide : %s\n", kv.c_str());
                return 1;
            }
            settings.emplace_back(spec, *value);
        } else if (a == "--block" && i + 1 < argc) {
            blockSize = std::max(1, std::atoi(argv[++i]));
        } else if (a == "--random-blocks") {
            randomBlocks = true;
        } else if (a == "--report") {
            report = true;
        } else if (a == "--format" && i + 1 < argc) {
            if (!parseSampleFormat(argv[++i], format)) {
                std::fprintf(stderr, "format inconnu : %s\n", argv[i]);
                return 1;
            }
        } else {
            std::fprintf(stderr, "argument inconnu : %s\n", a.c_str());
            return 1;
        }
    }

    AudioFile input;
    std::string error;
    if (!readWav(inPath, input, error)) {
        std::fprintf(stderr, "%s : %s\n", inPath.c_str(), error.c_str());
        return 1;
    }
    if (input.numChannels() == 1) input.channels.push_back(input.channels[0]); // mono -> stéréo
    if (input.numChannels() > 2) input.channels.resize(2);

    Engine engine;
    engine.prepare(input.sampleRate, blockSize);
    for (const auto& [spec, value] : settings) engine.setParameterImmediate(spec->id, value);

    const int latency = engine.latencySamples();
    const int frames = input.numFrames();
    const int total = frames + latency; // on pousse « latency » zéros pour vider la chaîne

    AudioFile rendered;
    rendered.sampleRate = input.sampleRate;
    rendered.resize(2, total);
    std::vector<double> inL(static_cast<std::size_t>(total), 0.0), inR(static_cast<std::size_t>(total), 0.0);
    std::copy(input.channels[0].begin(), input.channels[0].end(), inL.begin());
    std::copy(input.channels[1].begin(), input.channels[1].end(), inR.begin());

    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> blockDist(1, blockSize);
    for (int pos = 0; pos < total;) {
        const int n = std::min(total - pos, randomBlocks ? blockDist(rng) : blockSize);
        const double* in[2] = {inL.data() + pos, inR.data() + pos};
        double* out[2] = {rendered.channels[0].data() + pos, rendered.channels[1].data() + pos};
        engine.process(in, out, n);
        pos += n;
    }

    AudioFile output;
    output.sampleRate = input.sampleRate;
    output.resize(2, frames);
    for (int ch = 0; ch < 2; ++ch)
        std::copy(rendered.channels[static_cast<std::size_t>(ch)].begin() + latency,
                  rendered.channels[static_cast<std::size_t>(ch)].end(), output.channels[static_cast<std::size_t>(ch)].begin());

    if (!writeWav(outPath, output, format, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("rendu : %s -> %s (%d echantillons, latence compensee %d)\n", inPath.c_str(), outPath.c_str(), frames,
                latency);

    if (report) {
        std::fputs(analyze(input).toText("ENTREE " + inPath).c_str(), stdout);
        std::puts("");
        std::fputs(analyze(output).toText("SORTIE " + outPath).c_str(), stdout);
    }
    return 0;
}
