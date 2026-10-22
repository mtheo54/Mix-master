// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestMultiband.cpp — Dynamique multibande.
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "stages/MultibandStage.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using namespace omnis;
using params::MbControl;
using params::ParamId;

namespace {

constexpr double kFs = 48000.0;

struct Stereo {
    std::vector<double> l, r;
};

Stereo tones(std::initializer_list<std::pair<double, double>> parts, double seconds) {
    Stereo s;
    const int n = static_cast<int>(seconds * kFs);
    s.l.assign(static_cast<std::size_t>(n), 0.0);
    for (const auto& [f, a] : parts)
        for (int i = 0; i < n; ++i) s.l[static_cast<std::size_t>(i)] += a * std::sin(2.0 * std::numbers::pi * f * i / kFs);
    s.r = s.l;
    return s;
}

void run(MultibandStage& st, Stereo& s, int block = 256) {
    const int n = static_cast<int>(s.l.size());
    for (int pos = 0; pos < n; pos += block) {
        double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
        st.process(ch, std::min(block, n - pos));
    }
}

double amplitudeAt(const std::vector<double>& x, double f) {
    const std::size_t n = static_cast<std::size_t>(kFs / 2); // 0.5 s, cycles entiers pour f multiples de 2 Hz
    std::complex<double> acc{0.0, 0.0};
    for (std::size_t i = x.size() - n; i < x.size(); ++i)
        acc += x[i] * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(i) / kFs);
    return 2.0 * std::abs(acc) / static_cast<double>(n);
}

double db(double a) { return 20.0 * std::log10(std::max(a, 1e-300)); }

void prepared(MultibandStage& st) { st.prepare({kFs, 256, 2}); }

} // namespace

OMNIS_TEST("multibande: réglages neutres = retard pur (null-test < 1e-12)") {
    MultibandStage st;
    prepared(st);
    auto noise = tools::signals::pinkNoise(kFs, 1.0, -12.0, 9, true);
    Stereo s{noise.channels[0], noise.channels[1]};
    const Stereo in = s;
    run(st, s, 333);
    const auto lat = static_cast<std::size_t>(st.latencySamples());
    double maxDiff = 0.0;
    for (std::size_t i = lat; i < s.l.size(); ++i) {
        maxDiff = std::max(maxDiff, std::fabs(s.l[i] - in.l[i - lat]));
        maxDiff = std::max(maxDiff, std::fabs(s.r[i] - in.r[i - lat]));
    }
    OMNIS_CHECK(maxDiff < 1e-12);
    OMNIS_CHECK(st.latencySamples() == MultibandStage::kernelLengthFor(kFs) / 2 + MultibandStage::kernelLengthFor(kFs) / 32 + 480 + 2400);
}

OMNIS_TEST("multibande: compensation d'une bande = sa réponse Linkwitz-Riley (±0.03 dB)") {
    MultibandStage st;
    st.setParameter(params::mbId(2, MbControl::Makeup), 6.0, true); // médium 800 Hz – 5 kHz
    prepared(st);
    Stereo s = tones({{50.0, 0.1}, {2000.0, 0.1}, {12000.0, 0.1}}, 2.0);
    run(st, s);
    // Les bandes 24 dB/oct se chevauchent : à 2 kHz, 95 % du signal est dans la
    // bande médium. Gain attendu = 1 + (2 − 1) · |B2(f)|.
    const MbKernelRequest band2{120.0, 800.0, 5000.0, 2};
    for (double f : {50.0, 2000.0, 12000.0})
        OMNIS_CHECK_NEAR(db(amplitudeAt(s.l, f) / 0.1), db(1.0 + (std::pow(10.0, 6.0 / 20.0) - 1.0) * band2.magnitude(f)), 0.03);
}

OMNIS_TEST("multibande: courbe statique (30 Hz à -10 dBFS RMS, seuil -20, ratio 4 -> -7.5 dB)") {
    MultibandStage st;
    st.setParameter(params::mbId(0, MbControl::Threshold), -20.0, true);
    st.setParameter(params::mbId(0, MbControl::Ratio), 4.0, true);
    prepared(st);
    const double a = std::sqrt(2.0) * std::pow(10.0, -10.0 / 20.0); // -10 dBFS RMS
    Stereo s = tones({{30.0, a}}, 3.0); // au cœur de la bande sub (99.6 %)
    run(st, s);
    OMNIS_CHECK_NEAR(db(amplitudeAt(s.l, 30.0) / a), -7.5, 0.2);
}

OMNIS_TEST("multibande: 808 tenue proprement (harmoniques < -50 dB) et voix intacte") {
    MultibandStage st;
    st.setParameter(params::mbId(0, MbControl::Threshold), -24.0, true);
    st.setParameter(params::mbId(0, MbControl::Ratio), 4.0, true);
    prepared(st);
    const double a = std::sqrt(2.0) * std::pow(10.0, -10.0 / 20.0);
    Stereo s = tones({{50.0, a}, {1000.0, 0.05}}, 3.0);
    run(st, s);
    const double fundamental = amplitudeAt(s.l, 50.0);
    for (double h : {100.0, 150.0, 200.0, 250.0}) OMNIS_CHECK(db(amplitudeAt(s.l, h) / fundamental) < -50.0);
    OMNIS_CHECK(db(fundamental / a) < -9.0);                         // compressée
    OMNIS_CHECK_NEAR(db(amplitudeAt(s.l, 1000.0) / 0.05), 0.0, 0.1); // la voix ne bouge pas
}

OMNIS_TEST("multibande: anticipation — un coup de 808 ne dépasse pas sa valeur tenue") {
    MultibandStage st;
    st.setParameter(params::mbId(0, MbControl::Threshold), -24.0, true);
    st.setParameter(params::mbId(0, MbControl::Ratio), 6.0, true);
    st.setParameter(params::mbId(0, MbControl::Attack), 10.0, true);
    prepared(st);
    const double a = 0.5;
    Stereo s;
    const int n = static_cast<int>(2.0 * kFs);
    for (int i = 0; i < n; ++i) {
        const double v = i >= n / 2 ? a * std::sin(2.0 * std::numbers::pi * 50.0 * (i - n / 2) / kFs) : 0.0;
        s.l.push_back(v);
    }
    s.r = s.l;
    run(st, s);
    const auto lat = static_cast<std::size_t>(st.latencySamples());
    double onsetPeak = 0.0, steadyPeak = 0.0;
    const std::size_t onset = static_cast<std::size_t>(n / 2) + lat;
    for (std::size_t i = onset; i < onset + static_cast<std::size_t>(0.05 * kFs); ++i) onsetPeak = std::max(onsetPeak, std::fabs(s.l[i]));
    for (std::size_t i = s.l.size() - static_cast<std::size_t>(0.1 * kFs); i < s.l.size(); ++i) steadyPeak = std::max(steadyPeak, std::fabs(s.l[i]));
    OMNIS_CHECK(db(onsetPeak / steadyPeak) < 1.5);
}
