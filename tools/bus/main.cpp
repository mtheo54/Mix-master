// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/bus/main.cpp — Ce qu'OMNIS entend dans un bus (stems/BusListener), sur
//  un fichier : comme OMNIS posé sur ce bus en RÔLE Bus, pendant la lecture.
//
//      omnis_bus DRUMS.wav [MELO.wav …]
// =============================================================================
#include "AudioRead.h"

#include "stems/BusListener.h"

#include <cstdio>
#include <string>

using namespace omnis;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::puts("Abrasion Labs // OMNIS — ce qu'il y a dans un bus\n  omnis_bus bus.wav [autre.wav …]");
        return 1;
    }
    for (int a = 1; a < argc; ++a) {
        tools::AudioFile in;
        std::string error;
        if (!tools::readAudioFile(argv[a], in, error)) {
            std::fprintf(stderr, "%s : %s\n", argv[a], error.c_str());
            continue;
        }
        if (in.numChannels() == 1) in.channels.push_back(in.channels[0]);
        stems::BusListener l;
        l.prepare(in.sampleRate);
        for (int pos = 0; pos < in.numFrames(); pos += 1024) {
            const int n = std::min(1024, in.numFrames() - pos);
            l.push(in.channels[0].data() + pos, in.channels[1].data() + pos, n);
        }
        const auto c = l.content();
        std::printf("%s (%.0f s entendues) :\n", argv[a], c.secondsHeard);
        if (c.items.empty()) std::puts("  (rien de net)");
        for (const auto& i : c.items)
            std::printf("  %-12s %-28s %3.0f %%\n", stems::familyLabel(stems::familyOf(i.element)), stems::describeItem(i).c_str(), 100.0 * i.confidence);
    }
    return 0;
}
