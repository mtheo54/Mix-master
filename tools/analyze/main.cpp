// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/analyze — Rapport de métriques d'un ou plusieurs fichiers WAV.
//
//  Usage : omnis_analyze fichier.wav [autre.wav ...] [--json]
// =============================================================================
#include "AnalysisReport.h"
#include "WavFile.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace omnis::tools;

int main(int argc, char** argv) {
    bool asJson = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--json") asJson = true;
        else files.push_back(a);
    }
    if (files.empty()) {
        std::puts("Abrasion Labs // OMNIS — analyseur\n  omnis_analyze fichier.wav [...] [--json]");
        return 1;
    }

    int status = 0;
    for (const auto& path : files) {
        AudioFile f;
        std::string error;
        if (!readWav(path, f, error)) {
            std::fprintf(stderr, "%s : %s\n", path.c_str(), error.c_str());
            status = 1;
            continue;
        }
        const AnalysisReport report = analyze(f);
        std::fputs(asJson ? report.toJson().c_str() : report.toText(path).c_str(), stdout);
        if (!asJson) std::puts("");
    }
    return status;
}
