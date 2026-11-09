// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestMatchEq.cpp — Étage Match EQ (courbe d'intention M/S).
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "stages/MatchEqStage.h"
#include "stages/SubControlStage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <complex>
#include <numbers>
#include <vector>

using namespace omnis;
using params::ParamId;

namespace {

constexpr double kFs = 48000.0;

struct Stereo {
    std::vector<double> l, r;
};

/// Mid de fréquence fm et Side de fréquence fs (amplitudes am, as).
Stereo midSide(double fm, double am, double fs, double as, double seconds) {
    Stereo s;
    const int n = static_cast<int>(seconds * kFs);
    for (int i = 0; i < n; ++i) {
        const double m = am * std::sin(2.0 * std::numbers::pi * fm * i / kFs);
        const double d = as * std::sin(2.0 * std::numbers::pi * fs * i / kFs);
        s.l.push_back(m + d);
        s.r.push_back(m - d);
    }
    return s;
}

void run(MatchEqStage& st, Stereo& s, int block = 256) {
    const int n = static_cast<int>(s.l.size());
    for (int pos = 0; pos < n; pos += block) {
        double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
        st.process(ch, std::min(block, n - pos));
    }
}

double amplitudeAt(const std::vector<double>& x, double f) {
    const std::size_t n = static_cast<std::size_t>(kFs);
    std::complex<double> acc{0.0, 0.0};
    for (std::size_t i = x.size() - n; i < x.size(); ++i)
        acc += x[i] * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(i) / kFs);
    return 2.0 * std::abs(acc) / static_cast<double>(n);
}

double db(double a) { return 20.0 * std::log10(std::max(a, 1e-300)); }

std::vector<double> midOf(const Stereo& s) {
    std::vector<double> v(s.l.size());
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = 0.5 * (s.l[i] + s.r[i]);
    return v;
}

std::vector<double> sideOf(const Stereo& s) {
    std::vector<double> v(s.l.size());
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = 0.5 * (s.l[i] - s.r[i]);
    return v;
}

} // namespace

OMNIS_TEST("match eq: courbe Catmull-Rom (nœuds exacts, pente régulière reproduite, bornes tenues)") {
    MatchCurve c{};
    for (int k = 0; k < params::kMatchBands; ++k) c[static_cast<std::size_t>(k)] = 0.5 * k; // +1 dB / octave
    for (int k = 1; k < params::kMatchBands - 1; ++k) {
        OMNIS_CHECK_NEAR(matchCurveDb(c, params::matchBandHz(k)), 0.5 * k, 1e-9);
        // Entre deux nœuds intérieurs (le dernier intervalle voit le nœud extrême dupliqué).
        if (k < params::kMatchBands - 2)
            OMNIS_CHECK_NEAR(matchCurveDb(c, params::matchBandHz(k) * std::sqrt(std::sqrt(2.0))), 0.5 * k + 0.25, 1e-9);
    }
    OMNIS_CHECK_NEAR(matchCurveDb(c, 5.0), 0.0, 1e-12);
    OMNIS_CHECK_NEAR(matchCurveDb(c, 22000.0), 10.0, 1e-12);
}

OMNIS_TEST("match eq: courbe nulle = retard pur (null-test < 1e-12)") {
    MatchEqStage st;
    st.prepare({kFs, 256, 2});
    const int latency = st.latencySamples();
    OMNIS_CHECK(latency == SubControlStage::kernelLengthFor(kFs) / 2 + SubControlStage::blockSizeFor(kFs));
    auto noise = tools::signals::pinkNoise(kFs, 1.5, -12.0, 3, true);
    Stereo s{noise.channels[0], noise.channels[1]};
    const Stereo in = s;
    run(st, s, 333);
    double maxDiff = 0.0;
    for (std::size_t i = static_cast<std::size_t>(latency); i < s.l.size(); ++i) {
        maxDiff = std::max(maxDiff, std::fabs(s.l[i] - in.l[i - static_cast<std::size_t>(latency)]));
        maxDiff = std::max(maxDiff, std::fabs(s.r[i] - in.r[i - static_cast<std::size_t>(latency)]));
    }
    OMNIS_CHECK(maxDiff < 1e-12);
}

OMNIS_TEST("match eq: nœud Mid +6 dB à 1.28 kHz, Side +3 dB à 5.12 kHz, voisins lointains intacts") {
    MatchEqStage st;
    st.prepare({kFs, 256, 2});
    st.setParameter(params::matchMidId(12), 6.0, true);  // 1280 Hz
    st.setParameter(params::matchSideId(16), 3.0, true); // 5120 Hz
    Stereo s = midSide(1280.0, 0.25, 5120.0, 0.25, 2.0);
    run(st, s);
    OMNIS_CHECK_NEAR(db(amplitudeAt(midOf(s), 1280.0) / 0.25), 6.0, 0.03);
    OMNIS_CHECK_NEAR(db(amplitudeAt(sideOf(s), 5120.0) / 0.25), 3.0, 0.03);

    // 160 Hz (6 nœuds plus bas) en Mid, 1.28 kHz en Side : non touchés.
    MatchEqStage st2;
    st2.prepare({kFs, 256, 2});
    st2.setParameter(params::matchMidId(12), 6.0, true);
    st2.setParameter(params::matchSideId(16), 3.0, true);
    Stereo t = midSide(160.0, 0.25, 1280.0, 0.25, 2.0);
    run(st2, t);
    OMNIS_CHECK_NEAR(db(amplitudeAt(midOf(t), 160.0) / 0.25), 0.0, 0.03);
    OMNIS_CHECK_NEAR(db(amplitudeAt(sideOf(t), 1280.0) / 0.25), 0.0, 0.03);
}

OMNIS_TEST("match eq: Match Amount dose la courbe (50 % de -6 dB à 40 Hz = -3 dB)") {
    MatchEqStage st;
    st.prepare({kFs, 256, 2});
    for (int k = 0; k <= 4; ++k) st.setParameter(params::matchMidId(k), -6.0, true); // plateau 20–80 Hz
    st.setParameter(ParamId::MatchAmount, 50.0, true);
    Stereo s = midSide(40.0, 0.25, 1000.0, 0.0, 3.0);
    run(st, s);
    OMNIS_CHECK_NEAR(db(amplitudeAt(midOf(s), 40.0) / 0.25), -3.0, 0.05);
}

OMNIS_TEST("match eq: changement de courbe en lecture sans discontinuité") {
    MatchEqStage st;
    st.prepare({kFs, 256, 2});
    Stereo s = midSide(1000.0, 0.25, 1000.0, 0.0, 2.0);
    const int n = static_cast<int>(s.l.size());
    double maxStep = 0.0, prev = 0.0;
    for (int pos = 0; pos < n; pos += 256) {
        if (pos == n / 2) st.setParameter(params::matchMidId(11), 6.0, false);
        double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
        st.process(ch, std::min(256, n - pos));
    }
    // Écart à la sinusoïde : pas d'à-coup supérieur à la pente maximale d'un 1 kHz à +6 dB.
    for (int i = st.latencySamples() + 1; i < n; ++i) {
        maxStep = std::max(maxStep, std::fabs(s.l[static_cast<std::size_t>(i)] - prev));
        prev = s.l[static_cast<std::size_t>(i)];
    }
    OMNIS_CHECK(maxStep < 0.5 * 2.0 * std::numbers::pi * 1000.0 / kFs * 1.01);
}

OMNIS_TEST("match eq: tilt +3 dB (pivot 640 Hz) : -2.4 dB à 40 Hz, 0 à 640 Hz, +2.4 dB à 10 kHz, Mid et Side, non dosé") {
    // Nœud k : T·(k − 10)/10 ; 40 Hz = nœud 2, 10.24 kHz = nœud 18.
    const auto measure = [](double fm, double fs, double amount) {
        MatchEqStage st;
        st.prepare({kFs, 256, 2});
        st.setParameter(ParamId::MatchTilt, 3.0, true);
        st.setParameter(ParamId::MatchAmount, amount, true); // le tilt est un réglage à la main
        Stereo s = midSide(fm, 0.25, fs, 0.25, 3.0);
        run(st, s);
        return std::pair{db(amplitudeAt(midOf(s), fm) / 0.25), db(amplitudeAt(sideOf(s), fs) / 0.25)};
    };
    const auto [low, pivot] = measure(40.0, 640.0, 100.0);
    OMNIS_CHECK_NEAR(low, -2.4, 0.05);
    OMNIS_CHECK_NEAR(pivot, 0.0, 0.03);
    const auto [mid10k, side10k] = measure(10240.0, 10240.0, 0.0);
    OMNIS_CHECK_NEAR(mid10k, 2.4, 0.03);
    OMNIS_CHECK_NEAR(side10k, 2.4, 0.03);
    // Au-delà de 20.5 kHz, la pente s'arrête : pas d'aigu qui grimpe jusqu'à Nyquist.
    MatchKernelRequest r;
    r.tiltDb = 3.0;
    OMNIS_CHECK_NEAR(r.tiltAt(23000.0), 3.0, 1e-12);
    OMNIS_CHECK_NEAR(r.tiltAt(10.0), -3.0, 1e-12);
}

// -----------------------------------------------------------------------------
//  Caractère Analogique : phase minimale, cloches lissées sur ±1/3 d'octave.
// -----------------------------------------------------------------------------
namespace {

/// Énergie avant le transitoire / énergie totale, pour une impulsion à travers un grave poussé.
double preEcho(bool analog, int* latency = nullptr) {
    MatchEqStage st;
    st.prepare({kFs, 256, 2});
    st.setParameter(ParamId::MatchCharacter, analog ? 1.0 : 0.0, true);
    for (int k : {2, 3, 4}) st.setParameter(params::matchMidId(k), 6.0, true); // 40 – 80 Hz +6 dB
    Stereo s{std::vector<double>(96000, 0.0), std::vector<double>(96000, 0.0)};
    s.l[4000] = s.r[4000] = 0.5;
    run(st, s);
    const auto at = static_cast<std::size_t>(4000 + st.latencySamples());
    if (latency) *latency = st.latencySamples();
    double pre = 0.0, total = 0.0;
    for (std::size_t i = 0; i < s.l.size(); ++i) {
        total += s.l[i] * s.l[i];
        if (i + 4 < at) pre += s.l[i] * s.l[i];
    }
    return pre / total;
}

} // namespace

OMNIS_TEST("match eq Analogique: aucun pré-écho avant un transitoire (Propre en a), latence identique") {
    int latLinear = 0, latAnalog = 0;
    const double linear = preEcho(false, &latLinear), analog = preEcho(true, &latAnalog);
    std::printf("      énergie avant le coup : Propre %.2e, Analogique %.2e\n", linear, analog);
    OMNIS_CHECK(linear > 1e-4);
    OMNIS_CHECK(analog < 1e-7);
    OMNIS_CHECK(latLinear == latAnalog);
}

OMNIS_TEST("match eq Analogique: courbe nulle = retard pur (1e-9) ; cloches plus larges, plateau tenu") {
    {
        MatchEqStage st;
        st.prepare({kFs, 256, 2});
        st.setParameter(ParamId::MatchCharacter, 1.0, true);
        Stereo s = midSide(220.0, 0.3, 3000.0, 0.2, 1.0);
        const Stereo in = s;
        run(st, s);
        const auto lat = static_cast<std::size_t>(st.latencySamples());
        double err = 0.0;
        for (std::size_t i = lat; i < s.l.size(); ++i) err = std::max(err, std::fabs(s.l[i] - in.l[i - lat]));
        std::printf("      courbe nulle : écart %.2e\n", err);
        OMNIS_CHECK(err < 1e-9);
    }
    const auto gainAt = [](bool analog, const std::vector<int>& nodes, double f) {
        MatchEqStage st;
        st.prepare({kFs, 256, 2});
        st.setParameter(ParamId::MatchCharacter, analog ? 1.0 : 0.0, true);
        for (int k : nodes) st.setParameter(params::matchMidId(k), 6.0, true);
        Stereo s = midSide(f, 0.25, f, 0.0, 2.0);
        run(st, s);
        return db(amplitudeAt(midOf(s), f) / 0.25);
    };
    // Un seul nœud (+6 dB à 1,28 kHz) : la cloche s'élargit et s'adoucit ; ses voisins montent.
    const double peakLinear = gainAt(false, {12}, 1280.0), peakAnalog = gainAt(true, {12}, 1280.0);
    const double sideLinear = gainAt(false, {12}, 1810.0), sideAnalog = gainAt(true, {12}, 1810.0);
    std::printf("      1,28 kHz : Propre %.2f dB, Analogique %.2f dB ; 1,81 kHz : %.2f / %.2f dB\n", peakLinear, peakAnalog, sideLinear, sideAnalog);
    OMNIS_CHECK(peakAnalog > 3.0 && peakAnalog < peakLinear - 0.3);
    OMNIS_CHECK(sideAnalog > sideLinear + 0.3);
    // Un plateau large (5 nœuds) est tenu à son centre, loin, rien.
    OMNIS_CHECK(std::fabs(gainAt(true, {10, 11, 12, 13, 14}, 1280.0) - 6.0) < 0.3);
    OMNIS_CHECK(std::fabs(gainAt(true, {10, 11, 12, 13, 14}, 80.0)) < 0.1);
}
