// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/testgen — Générateur du kit de signaux de test.
//
//  Usage :
//    omnis_testgen list
//    omnis_testgen <signal> -o fichier.wav [--sr 48000] [--format f32]
//    omnis_testgen kit <dossier> [--sr 48000] [--format f32]
//
//  Le mode « kit » écrit tous les signaux dans un dossier, avec un MANIFEST.txt
//  décrivant chaque fichier et la mesure attendue.
// =============================================================================
#include "TestSignals.h"
#include "WavFile.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace omnis::tools;

namespace {

void usage() {
    std::puts("Abrasion Labs // OMNIS — generateur de signaux de test\n"
              "\n"
              "  omnis_testgen list\n"
              "  omnis_testgen <signal> -o out.wav [--sr 48000] [--format f32|f64|pcm24|pcm16]\n"
              "  omnis_testgen kit <dossier> [--sr 48000] [--format f32]\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 1;
    }
    const std::string command = argv[1];
    std::string output;
    double sampleRate = 48000.0;
    SampleFormat format = SampleFormat::Float32;

    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "-o" || a == "--out") && i + 1 < argc) output = argv[++i];
        else if (a == "--sr" && i + 1 < argc) sampleRate = std::atof(argv[++i]);
        else if (a == "--format" && i + 1 < argc) {
            if (!parseSampleFormat(argv[++i], format)) {
                std::fprintf(stderr, "format inconnu : %s\n", argv[i]);
                return 1;
            }
        } else if (command == "kit" && output.empty()) output = a;
        else {
            std::fprintf(stderr, "argument inconnu : %s\n", a.c_str());
            return 1;
        }
    }
    if (sampleRate < 8000.0 || sampleRate > 768000.0) {
        std::fprintf(stderr, "frequence d'echantillonnage invalide\n");
        return 1;
    }

    if (command == "list") {
        for (const auto& name : signals::availableSignals()) {
            AudioFile f;
            std::string description;
            signals::generate(name, 8000.0, f, description);
            std::printf("  %-14s %s\n", name.c_str(), description.c_str());
        }
        return 0;
    }

    std::string error;
    if (command == "kit") {
        if (output.empty()) {
            usage();
            return 1;
        }
        std::filesystem::create_directories(output);
        std::ofstream manifest(std::filesystem::path(output) / "MANIFEST.txt");
        manifest << "Abrasion Labs // OMNIS — kit de signaux de test (" << sampleRate << " Hz)\n\n";
        for (const auto& name : signals::availableSignals()) {
            AudioFile f;
            std::string description;
            signals::generate(name, sampleRate, f, description);
            const auto path = (std::filesystem::path(output) / (name + ".wav")).string();
            // Le filtre inverse est toujours écrit en 64 bits : sa dynamique
            // (+6 dB/oct sur 10 octaves) mérite la pleine précision.
            const SampleFormat fileFormat = name == "sweep-inverse" ? SampleFormat::Float64 : format;
            if (!writeWav(path, f, fileFormat, error)) {
                std::fprintf(stderr, "%s : %s\n", name.c_str(), error.c_str());
                return 1;
            }
            manifest << name << ".wav\n    " << description << "\n";
            std::printf("ecrit %s\n", path.c_str());
        }
        return 0;
    }

    AudioFile f;
    std::string description;
    if (!signals::generate(command, sampleRate, f, description)) {
        std::fprintf(stderr, "signal inconnu : %s (voir 'omnis_testgen list')\n", command.c_str());
        return 1;
    }
    if (output.empty()) output = command + ".wav";
    if (!writeWav(output, f, format, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("%s -> %s\n", description.c_str(), output.c_str());
    return 0;
}
