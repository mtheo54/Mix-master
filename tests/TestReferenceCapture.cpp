// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestReferenceCapture.cpp — Écoute d'une référence pendant la lecture.
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "analysis/SpectralSignature.h"
#include "engine/ReferenceCapture.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace omnis;

namespace {

constexpr double kFs = 48000.0;

/// Joue `ref` et `prod` comme un DAW (blocs de 256) sans jamais saturer les files.
void play(ReferenceCapture& c, const tools::AudioFile& ref, const tools::AudioFile& prod) {
    const int n = ref.numFrames();
    for (int pos = 0; pos < n; pos += 256) {
        while (c.pending() > static_cast<std::size_t>(kFs)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const int len = std::min(256, n - pos);
        c.push(ref.channels[0].data() + pos, ref.channels[1].data() + pos, prod.channels[0].data() + pos, prod.channels[1].data() + pos, len);
    }
}

} // namespace

OMNIS_TEST("capture: signature de la référence jouée = signature du fichier (±0.01 dB), prod mesurée en même temps") {
    auto ref = tools::signals::pinkNoise(kFs, 12.0, -14.0, 21, true);
    auto prod = tools::signals::pinkNoise(kFs, 12.0, -24.0, 22, true);
    std::atomic<int> done{0};
    ReferenceCapture::Result got;
    ReferenceCapture c;
    c.setCompletionHandler([&](const ReferenceCapture::Result& r) {
        got = r;
        done.store(1);
    });
    c.prepare(kFs);
    c.setCapturing(true);
    play(c, ref, prod);
    c.setCapturing(false);
    for (int i = 0; i < 2000 && !done.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    OMNIS_CHECK(done.load() == 1);
    OMNIS_CHECK(c.droppedSamples() == 0);

    const auto expected = analysis::analyzeSignature(ref.channels[0].data(), ref.channels[1].data(), ref.numFrames(), kFs);
    for (int k = 0; k < params::kMatchBands; ++k)
        OMNIS_CHECK_NEAR(got.reference.midDb[static_cast<std::size_t>(k)], expected.midDb[static_cast<std::size_t>(k)], 0.01);
    OMNIS_CHECK_NEAR(got.reference.integratedLufs, expected.integratedLufs, 0.01);
    OMNIS_CHECK_NEAR(got.referenceSeconds, 12.0, 0.1);
    OMNIS_CHECK(got.prod.integratedLufs < got.reference.integratedLufs - 8.0); // la prod, 10 dB plus bas
    ReferenceCapture::Result last;
    OMNIS_CHECK(c.lastResult(last));
}

OMNIS_TEST("capture: rien n'est poussé hors écoute, et une file pleine perd des échantillons sans bloquer") {
    ReferenceCapture c;
    c.prepare(kFs, 0.05); // 50 ms de file
    std::vector<float> l(48000, 0.1f);
    c.push(l.data(), l.data(), l.data(), l.data(), 48000); // pas d'écoute : ignoré
    OMNIS_CHECK(c.pending() == 0);
    c.setCapturing(true);
    const auto t0 = std::chrono::steady_clock::now();
    c.push(l.data(), l.data(), l.data(), l.data(), 48000); // 1 s d'un coup dans 50 ms de file
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    OMNIS_CHECK(ms < 50.0);
    OMNIS_CHECK(c.droppedSamples() > 0);
    c.setCapturing(false);
}
