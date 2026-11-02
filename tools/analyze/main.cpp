// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/analyze — Rapport de métriques d'un ou plusieurs fichiers WAV.
//
//  Usage : omnis_analyze fichier.wav [autre.wav ...] [--bands] [--json]
//  --bands : énergie, corrélation, ratio S/M et crête par bande (LR8).
// =============================================================================
#include "ai/MixDiagnosis.h"
#include "analysis/MusicDescriptors.h"
#include "analysis/SpectralSignature.h"
#include "AudioRead.h"
#include "AnalysisReport.h"
#include "WavFile.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace omnis::tools;

int main(int argc, char** argv) {
    bool asJson = false, withBands = false, diagnostic = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--json") asJson = true;
        else if (a == "--bands") withBands = true;
        else if (a == "--diagnostic") diagnostic = true;
        else files.push_back(a);
    }
    if (files.empty()) {
        std::puts("Abrasion Labs // OMNIS — analyseur\n  omnis_analyze fichier.wav [...] [--bands] [--json] [--diagnostic]");
        return 1;
    }

    int status = 0;
    for (const auto& path : files) {
        AudioFile f;
        std::string error;
        if (!readAudioFile(path, f, error)) {
            std::fprintf(stderr, "%s : %s\n", path.c_str(), error.c_str());
            status = 1;
            continue;
        }
        if (diagnostic) {
            // Le diagnostic du mix (comme dans la console), comparé à la moyenne de la base.
            if (f.numChannels() == 1) f.channels.push_back(f.channels[0]);
            auto sig = omnis::analysis::analyzeSignature(f.channels[0].data(), f.channels[1].data(), f.numFrames(), f.sampleRate);
            sig.music = omnis::analysis::analyzeMusic(f.channels[0].data(), f.channels[1].data(), f.numFrames(), f.sampleRate);
            std::printf("== %s\n%s\n", path.c_str(), omnis::ai::formatDiagnosis(omnis::ai::diagnoseMix(sig, nullptr)).c_str());
            continue;
        }
        const AnalysisReport report = analyze(f, withBands);
        std::fputs(asJson ? report.toJson().c_str() : report.toText(path).c_str(), stdout);
        if (!asJson) std::puts("");
    }
    return status;
}
