// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestOversampling.cpp — Demi-bandes HIIR et cascade 2x … 16x.
// =============================================================================
#include "TestFramework.h"

#include "dsp/Halfband.h"
#include "dsp/Oversampler.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <vector>

using namespace omnis::dsp;

namespace {

constexpr double kFs = 44100.0;

double amplitude(const std::vector<double>& x, double f, double fs, std::size_t start) {
    std::complex<double> acc{};
    for (std::size_t n = start; n < x.size(); ++n)
        acc += x[n] * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(n) / fs);
    return 2.0 * std::abs(acc) / static_cast<double>(x.size() - start);
}

double db(double a) { return 20.0 * std::log10(std::max(a, 1e-300)); }

/// Sinus de `seconds` passé dans montée + descente ; amplitude de sortie à f.
double roundTripGain(int stages, double f) {
    Oversampler os;
    os.prepare(512);
    os.setStages(stages);
    const int n = static_cast<int>(kFs); // 1 s : nombre entier de cycles pour f entier
    std::vector<double> x(static_cast<std::size_t>(n)), y(x.size());
    for (int i = 0; i < n; ++i) x[static_cast<std::size_t>(i)] = 0.5 * std::sin(2.0 * std::numbers::pi * f * i / kFs);
    for (int pos = 0; pos < n; pos += 512) {
        const int len = std::min(512, n - pos);
        const double* in[2] = {x.data() + pos, x.data() + pos};
        os.upsample(in, len);
        std::vector<double> dummy(static_cast<std::size_t>(len));
        double* out[2] = {y.data() + pos, dummy.data()};
        os.downsample(out, len);
    }
    return amplitude(y, f, kFs, 4410) / 0.5;
}

} // namespace

OMNIS_TEST("halfband: 120 dB / tw 0.02 -> 12 coefficients, coefficients dans ]0, 1[") {
    const auto c = designHalfband(120.0, 0.02);
    OMNIS_CHECK(c.size() == 12);
    for (double v : c) OMNIS_CHECK(v > 0.0 && v < 1.0);
    for (std::size_t s = 1; s < 4; ++s) OMNIS_CHECK(designHalfband(120.0, Oversampler::kTransitions[s]).size() <= 7);
}

OMNIS_TEST("oversampling: aller-retour plat à ±0.001 dB jusqu'à 20 kHz (2x … 16x)") {
    for (int stages = 1; stages <= 4; ++stages)
        for (double f : {50.0, 1000.0, 10000.0, 18000.0, 20000.0})
            OMNIS_CHECK_NEAR(db(roundTripGain(stages, f)), 0.0, 0.001);
}

OMNIS_TEST("oversampling: images rejetées à > 115 dB après la montée 8x") {
    Oversampler os;
    os.prepare(512);
    os.setStages(3);
    // 1.1 s ; analyse sur la dernière seconde exactement : le sinus ET ses images
    // y font un nombre entier de cycles (sinon la fuite spectrale du sinus
    // principal, ~ -96 dB, masquerait la mesure).
    const int n = 48510;
    std::vector<double> x(static_cast<std::size_t>(n)), up;
    for (int i = 0; i < n; ++i) x[static_cast<std::size_t>(i)] = 0.5 * std::sin(2.0 * std::numbers::pi * 10000.0 * i / kFs);
    for (int pos = 0; pos < n; pos += 512) {
        const int len = std::min(512, n - pos);
        const double* in[2] = {x.data() + pos, x.data() + pos};
        double* const* o = os.upsample(in, len);
        up.insert(up.end(), o[0], o[0] + len * 8);
        std::vector<double> d(static_cast<std::size_t>(len));
        double* out[2] = {d.data(), d.data()};
        os.downsample(out, len);
    }
    const double fsUp = 8.0 * kFs;
    const std::size_t start = 8 * 4410;
    OMNIS_CHECK_NEAR(db(amplitude(up, 10000.0, fsUp, start) / 0.5), 0.0, 0.001);
    for (double image : {kFs - 10000.0, kFs + 10000.0, 2 * kFs - 10000.0, 4 * kFs - 10000.0}) {
        const double level = db(amplitude(up, image, fsUp, start) / 0.5);
        std::printf("    image %.0f Hz : %.1f dB\n", image, level);
        OMNIS_CHECK(level < -115.0);
    }
}

OMNIS_TEST("oversampling: contenu ultrasonore rejeté à > 115 dB à la descente (anti-repliement)") {
    // Sinus à 30 kHz généré au taux 8x : sans filtre, il se replierait à 14.1 kHz.
    Oversampler os;
    os.prepare(512);
    os.setStages(3);
    const int n = 44100;
    std::vector<double> y(static_cast<std::size_t>(n)), zeros(512, 0.0), dummy(512);
    std::size_t k = 0;
    for (int pos = 0; pos < n; pos += 512) {
        const int len = std::min(512, n - pos);
        const double* in[2] = {zeros.data(), zeros.data()};
        double* const* o = os.upsample(in, len);
        for (int i = 0; i < len * 8; ++i, ++k)
            o[0][i] = o[1][i] = 0.5 * std::sin(2.0 * std::numbers::pi * 30000.0 * static_cast<double>(k) / (8.0 * kFs));
        double* out[2] = {y.data() + pos, dummy.data()};
        os.downsample(out, len);
    }
    OMNIS_CHECK(db(amplitude(y, 14100.0, kFs, 4410) / 0.5) < -115.0);
}

OMNIS_TEST("oversampling: retard de phase à 1 kHz faible (< 8 échantillons) et 1x = identité") {
    for (int s = 1; s <= 4; ++s) {
        const double d = Oversampler::phaseDelaySamples(s, kFs);
        std::printf("    %2dx : retard de phase à 1 kHz = %.3f échantillons\n", 1 << s, d);
        OMNIS_CHECK(d > 0.0 && d < 8.0);
    }
    OMNIS_CHECK(Oversampler::phaseDelaySamples(0, kFs) == 0.0);
}

OMNIS_TEST("halfband: traitement par bloc identique au bit près au traitement échantillon par échantillon") {
    // Chaque étage du suréchantillonneur (6/6, 4/3, 3/3, 3/2 cellules) et une taille hors liste.
    std::vector<double> transitions(omnis::dsp::Oversampler::kTransitions.begin(), omnis::dsp::Oversampler::kTransitions.end());
    transitions.push_back(0.05);
    unsigned seed = 12345u;
    const auto noise = [&] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<double>(seed >> 8) / 8388608.0 - 1.0;
    };
    for (double transition : transitions) {
        const auto coefs = omnis::dsp::designHalfband(omnis::dsp::Oversampler::kAttenuationDb, transition);
        omnis::dsp::Halfband2x a, b;
        a.setCoefficients(coefs);
        b.setCoefficients(coefs);
        for (int block : {1, 7, 64, 333}) {
            std::vector<double> in(static_cast<std::size_t>(block)), up(2 * in.size()), upRef(up.size()), down(in.size()), downRef(in.size());
            for (auto& v : in) v = noise();
            a.upsampleBlock(in.data(), up.data(), block);
            for (int i = 0; i < block; ++i) b.upsample(in[static_cast<std::size_t>(i)], upRef[2 * static_cast<std::size_t>(i)], upRef[2 * static_cast<std::size_t>(i) + 1]);
            OMNIS_CHECK(up == upRef);
            a.downsampleBlock(up.data(), down.data(), block);
            for (int i = 0; i < block; ++i) downRef[static_cast<std::size_t>(i)] = b.downsample(upRef[2 * static_cast<std::size_t>(i)], upRef[2 * static_cast<std::size_t>(i) + 1]);
            OMNIS_CHECK(down == downRef);
        }
    }
}
