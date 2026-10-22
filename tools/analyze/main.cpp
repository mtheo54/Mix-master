// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/analyze — Rapport de métriques d'un ou plusieurs fichiers WAV.
//
//  Usage : omnis_analyze fichier.wav [autre.wav ...] [--bands] [--json]
//  --bands : énergie, corrélation, ratio S/M et crête par bande (LR8).
// =============================================================================
#include "AudioRead.h"
#include "AnalysisReport.h"
#include "WavFile.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace omnis::tools;

int main(int argc, char** argv) {
    bool asJson = false, withBands = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--json") asJson = true;
        else if (a == "--bands") withBands = true;
        else files.push_back(a);
    }
    if (files.empty()) {
        std::puts("Abrasion Labs // OMNIS — analyseur\n  omnis_analyze fichier.wav [...] [--bands] [--json]");
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
        const AnalysisReport report = analyze(f, withBands);
        std::fputs(asJson ? report.toJson().c_str() : report.toText(path).c_str(), stdout);
        if (!asJson) std::puts("");
    }
    return status;
}
