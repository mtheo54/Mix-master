// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestTruePeak.cpp — Crête vraie (BS.1770-4 Annexe 2).
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "analysis/TruePeakMeter.h"
#include "core/Decibels.h"

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using namespace omnis;
using namespace omnis::analysis;

namespace {

double measureTruePeakDb(const tools::AudioFile& f) {
    TruePeakMeter m;
    m.prepare(2);
    const double* ptrs[2] = {f.channels[0].data(), f.channels[1].data()};
    m.process(ptrs, f.numFrames());
    return gainToDb(m.truePeakMax());
}

/// Gain d'une phase de l'interpolateur à la fréquence normalisée `f` (cycles/échantillon d'entrée).
double phaseGainDb(const std::array<double, TruePeakMeter::kTapsPerPhase>& h, double f) {
    std::complex<double> acc{0.0, 0.0};
    for (int k = 0; k < TruePeakMeter::kTapsPerPhase; ++k)
        acc += h[static_cast<std::size_t>(k)] * std::polar(1.0, -2.0 * std::numbers::pi * f * k);
    return 20.0 * std::log10(std::abs(acc));
}

} // namespace

OMNIS_TEST("true peak: sinus fs/4 à 45° -> sample peak -3.01, true peak 0.00 dBTP") {
    const auto f = tools::signals::intersamplePeak(48000.0, 1.0, 0.0);
    TruePeakMeter m;
    m.prepare(2);
    const double* ptrs[2] = {f.channels[0].data(), f.channels[1].data()};
    m.process(ptrs, f.numFrames());
    OMNIS_CHECK_NEAR(gainToDb(m.samplePeakMax()), -3.0103, 0.001);
    OMNIS_CHECK_NEAR(gainToDb(m.truePeakMax()), 0.0, 0.05);
}

OMNIS_TEST("true peak: sinus 997 Hz 0 dBFS -> 0 dBTP, jamais sous le sample peak") {
    tools::AudioFile f;
    f.sampleRate = 48000.0;
    f.resize(2, 48000);
    for (int i = 0; i < 48000; ++i) {
        const double v = std::sin(2.0 * std::numbers::pi * 997.0 * i / 48000.0 + 0.3);
        f.channels[0][static_cast<std::size_t>(i)] = v;
        f.channels[1][static_cast<std::size_t>(i)] = 0.5 * v;
    }
    OMNIS_CHECK_NEAR(measureTruePeakDb(f), 0.0, 0.01);
}

OMNIS_TEST("true peak: phase 0 = identité exacte") {
    TruePeakMeter m;
    const auto& c = m.coefficients()[0];
    for (int k = 0; k < TruePeakMeter::kTapsPerPhase; ++k)
        OMNIS_CHECK(c[static_cast<std::size_t>(k)] == (k == TruePeakMeter::kHalfLength ? 1.0 : 0.0));
}

OMNIS_TEST("true peak: interpolateur plat (+/-0.01 dB) jusqu'à 0.45 fs") {
    TruePeakMeter m;
    double worst = 0.0;
    for (int p = 1; p < TruePeakMeter::kOversampling; ++p)
        for (double f = 0.0; f <= 0.45; f += 0.0025)
            worst = std::max(worst, std::fabs(phaseGainDb(m.coefficients()[static_cast<std::size_t>(p)], f)));
    OMNIS_CHECK(worst < 0.01);
}

OMNIS_TEST("true peak: crêtes inter-échantillons d'un signal transitoire >= sample peak") {
    const auto f = tools::signals::drumLoop(44100.0, 2);
    TruePeakMeter m;
    m.prepare(2);
    const double* ptrs[2] = {f.channels[0].data(), f.channels[1].data()};
    m.process(ptrs, f.numFrames());
    OMNIS_CHECK(m.truePeakMax() >= m.samplePeakMax());
    OMNIS_CHECK_NEAR(gainToDb(m.samplePeakMax()), -1.0, 1e-6);
}
