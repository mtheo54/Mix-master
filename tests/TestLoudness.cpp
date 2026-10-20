// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestLoudness.cpp — Conformité ITU-R BS.1770-4 / EBU Tech 3341 & 3342.
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "analysis/KWeightingFilter.h"
#include "analysis/LoudnessMeter.h"

#include <cmath>
#include <numbers>
#include <vector>

using namespace omnis;
using namespace omnis::analysis;

namespace {

void feed(LoudnessMeter& meter, const tools::AudioFile& f, int block = 1024) {
    for (int pos = 0; pos < f.numFrames(); pos += block) {
        const int n = std::min(block, f.numFrames() - pos);
        const double* ptrs[2] = {f.channels[0].data() + pos, f.channels[1].data() + pos};
        meter.process(ptrs, n);
    }
}

tools::AudioFile sine(double fs, double freq, double peakDb, double seconds) {
    tools::AudioFile f;
    f.sampleRate = fs;
    const int n = static_cast<int>(fs * seconds);
    f.resize(2, n);
    const double a = std::pow(10.0, peakDb / 20.0);
    for (int i = 0; i < n; ++i) {
        const double v = a * std::sin(2.0 * std::numbers::pi * freq * i / fs);
        f.channels[0][static_cast<std::size_t>(i)] = v;
        f.channels[1][static_cast<std::size_t>(i)] = v;
    }
    return f;
}

} // namespace

OMNIS_TEST("k-weighting: coefficients 48 kHz = Tables 1 et 2 de BS.1770-4") {
    const auto s = kWeightingShelf(48000.0);
    OMNIS_CHECK_NEAR(s.b0, 1.53512485958697, 1e-10);
    OMNIS_CHECK_NEAR(s.b1, -2.69169618940638, 1e-10);
    OMNIS_CHECK_NEAR(s.b2, 1.19839281085285, 1e-10);
    OMNIS_CHECK_NEAR(s.a1, -1.69065929318241, 1e-10);
    OMNIS_CHECK_NEAR(s.a2, 0.73248077421585, 1e-10);
    const auto h = kWeightingHighPass(48000.0);
    OMNIS_CHECK_NEAR(h.a1, -1.99004745483398, 1e-10);
    OMNIS_CHECK_NEAR(h.a2, 0.99007225036621, 1e-10);
}

OMNIS_TEST("ebu3341: cas 1 et 2 (M, S, I) à 48 et 44.1 kHz") {
    for (double fs : {48000.0, 44100.0}) {
        for (int c : {1, 2}) {
            const double expected = c == 1 ? -23.0 : -33.0;
            LoudnessMeter m;
            m.prepare(fs, 2);
            feed(m, tools::signals::ebu3341(fs, c));
            OMNIS_CHECK_NEAR(m.momentaryLufs(), expected, 0.1);
            OMNIS_CHECK_NEAR(m.shortTermLufs(), expected, 0.1);
            OMNIS_CHECK_NEAR(m.integratedLufs(), expected, 0.1);
        }
    }
}

OMNIS_TEST("ebu3341: cas 3, 4, 5 (portes absolue et relative)") {
    for (double fs : {48000.0, 96000.0}) {
        for (int c : {3, 4, 5}) {
            LoudnessMeter m;
            m.prepare(fs, 2);
            feed(m, tools::signals::ebu3341(fs, c), 997);
            OMNIS_CHECK_NEAR(m.integratedLufs(), -23.0, 0.1);
        }
    }
}

OMNIS_TEST("loudness: indépendance vis-à-vis du découpage en blocs") {
    const auto f = tools::signals::pinkNoise(48000.0, 8.0, -18.0);
    LoudnessMeter a, b;
    a.prepare(48000.0, 2);
    b.prepare(48000.0, 2);
    feed(a, f, 4800);
    feed(b, f, 37);
    OMNIS_CHECK_NEAR(a.integratedLufs(), b.integratedLufs(), 1e-9);
    OMNIS_CHECK_NEAR(a.shortTermLufs(), b.shortTermLufs(), 1e-9);
}

OMNIS_TEST("loudness: silence -> -inf, aucune mesure intégrée") {
    LoudnessMeter m;
    m.prepare(48000.0, 2);
    std::vector<double> z(48000 * 2, 0.0);
    const double* ptrs[2] = {z.data(), z.data()};
    m.process(ptrs, static_cast<int>(z.size()));
    OMNIS_CHECK(std::isinf(m.integratedLufs()) && m.integratedLufs() < 0);
}

OMNIS_TEST("ebu3342: LRA d'un signal stationnaire ~ 0, d'un signal à deux niveaux ~ écart") {
    {
        LoudnessMeter m;
        m.prepare(48000.0, 2);
        feed(m, sine(48000.0, 1000.0, -20.0, 20.0));
        OMNIS_CHECK_NEAR(m.loudnessRangeLu(), 0.0, 0.1);
    }
    {
        // EBU Tech 3342 cas 1 : -20 dBFS (20 s) puis -30 dBFS (20 s) -> LRA = 10 ± 1 LU
        auto a = sine(48000.0, 1000.0, -20.0, 20.0);
        const auto b = sine(48000.0, 1000.0, -30.0, 20.0);
        for (int ch = 0; ch < 2; ++ch)
            a.channels[static_cast<std::size_t>(ch)].insert(a.channels[static_cast<std::size_t>(ch)].end(),
                                                            b.channels[static_cast<std::size_t>(ch)].begin(),
                                                            b.channels[static_cast<std::size_t>(ch)].end());
        LoudnessMeter m;
        m.prepare(48000.0, 2);
        feed(m, a);
        OMNIS_CHECK_NEAR(m.loudnessRangeLu(), 10.0, 1.0);
    }
}
