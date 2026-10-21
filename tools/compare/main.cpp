// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/compare — Distorsion non linéaire ajoutée par un traitement.
//
//  Usage : omnis_compare original.wav traite.wav [autre_traite.wav ...]
//  Les fichiers traités doivent être alignés sur l'original (omnis_render
//  compense la latence). Plus la valeur est basse, plus c'est propre.
// =============================================================================
#include "AnalysisReport.h"
#include "DistortionAnalysis.h"
#include "WavFile.h"

#include <cstdio>
#include <string>

using namespace omnis::tools;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::puts("Abrasion Labs // OMNIS — mesure de distorsion\n  omnis_compare original.wav traite.wav [...]");
        return 1;
    }
    AudioFile reference;
    std::string error;
    if (!readWav(argv[1], reference, error)) {
        std::fprintf(stderr, "%s : %s\n", argv[1], error.c_str());
        return 1;
    }
    int status = 0;
    for (int i = 2; i < argc; ++i) {
        AudioFile processed;
        if (!readWav(argv[i], processed, error)) {
            std::fprintf(stderr, "%s : %s\n", argv[i], error.c_str());
            status = 1;
            continue;
        }
        const auto loud = analyze(processed);
        std::printf("%-48s I=%6.1f LUFS TP=%6.2f  ", argv[i], loud.integratedLufs, loud.truePeakDb);
        std::fputs(measureDistortion(reference, processed).toText().c_str(), stdout);
    }
    return status;
}
