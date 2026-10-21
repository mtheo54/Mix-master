// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestFilters.cpp — SVF TPT et crossovers Linkwitz-Riley.
// =============================================================================
#include "TestFramework.h"

#include "dsp/Crossover.h"
#include "dsp/SvfFilter.h"

#include <cmath>
#include <complex>
#include <functional>
#include <numbers>
#include <random>
#include <vector>

using namespace omnis::dsp;

namespace {

constexpr double kFs = 48000.0;
constexpr int kIrLength = 1 << 17; // ~2.7 s : laisse décroître les pôles à 10 Hz

std::vector<double> impulseResponse(const std::function<double(double)>& f, int length = kIrLength) {
    std::vector<double> h(static_cast<std::size_t>(length));
    for (int i = 0; i < length; ++i) h[static_cast<std::size_t>(i)] = f(i == 0 ? 1.0 : 0.0);
    return h;
}

std::complex<double> response(const std::vector<double>& h, double frequency, double fs = kFs) {
    std::complex<double> acc{0.0, 0.0};
    const std::complex<double> step = std::polar(1.0, -2.0 * std::numbers::pi * frequency / fs);
    std::complex<double> w{1.0, 0.0};
    for (std::size_t n = 0; n < h.size(); ++n) {
        acc += h[n] * w;
        w *= step;
        if ((n & 1023u) == 1023u) w /= std::abs(w);
    }
    return acc;
}

double gainDb(const std::vector<double>& h, double frequency) { return 20.0 * std::log10(std::abs(response(h, frequency))); }

std::vector<double> svfIr(SvfType type, double fc, double q, double gainDb = 0.0) {
    const auto c = SvfCoefficients::make(type, kFs, fc, q, gainDb);
    SvfState s;
    return impulseResponse([&](double x) { return s.process(c, x); });
}

const std::vector<double> kTestFrequencies{10, 20, 31.5, 50, 80, 120, 200, 500, 1000, 3000, 8000, 15000, 22000};

} // namespace

OMNIS_TEST("svf: passe-bas / passe-haut Butterworth = -3.01 dB à fc, pentes 12 dB/oct") {
    const double q = butterworthQ(2, 0);
    for (double fc : {30.0, 1000.0, 12000.0}) {
        const auto lp = svfIr(SvfType::LowPass, fc, q);
        const auto hp = svfIr(SvfType::HighPass, fc, q);
        OMNIS_CHECK_NEAR(gainDb(lp, fc), -3.0103, 0.005);
        OMNIS_CHECK_NEAR(gainDb(hp, fc), -3.0103, 0.005);
        OMNIS_CHECK_NEAR(gainDb(lp, fc / 8.0), 0.0, 0.01);
        // Attendu analogique évalué à la fréquence pré-distordue par la
        // bilinéaire : w = tan(πf/fs) / tan(πfc/fs) ; |HP| = w² / sqrt(1 + w⁴).
        const double w = std::tan(std::numbers::pi * fc / 8.0 / kFs) / std::tan(std::numbers::pi * fc / kFs);
        OMNIS_CHECK_NEAR(gainDb(hp, fc / 8.0), 20.0 * std::log10(w * w / std::sqrt(1.0 + w * w * w * w)), 0.01);
    }
}

OMNIS_TEST("svf: cloche, plateaux, passe-tout, réjecteur") {
    OMNIS_CHECK_NEAR(gainDb(svfIr(SvfType::Bell, 1000.0, 2.0, 6.0), 1000.0), 6.0, 0.005);
    OMNIS_CHECK_NEAR(gainDb(svfIr(SvfType::Bell, 1000.0, 2.0, -9.0), 1000.0), -9.0, 0.005);
    OMNIS_CHECK_NEAR(gainDb(svfIr(SvfType::Bell, 1000.0, 2.0, 6.0), 20.0), 0.0, 0.01);

    const auto ls = svfIr(SvfType::LowShelf, 100.0, 0.7071, 6.0);
    OMNIS_CHECK_NEAR(gainDb(ls, 10.0), 6.0, 0.02);
    OMNIS_CHECK_NEAR(gainDb(ls, 100.0), 3.0, 0.01); // mi-gain à fc
    OMNIS_CHECK_NEAR(gainDb(ls, 5000.0), 0.0, 0.01);

    const auto hs = svfIr(SvfType::HighShelf, 4000.0, 0.7071, -6.0);
    OMNIS_CHECK_NEAR(gainDb(hs, 20000.0), -6.0, 0.05);
    OMNIS_CHECK_NEAR(gainDb(hs, 4000.0), -3.0, 0.01);
    OMNIS_CHECK_NEAR(gainDb(hs, 100.0), 0.0, 0.01);

    const auto ap = svfIr(SvfType::AllPass, 250.0, 0.7071);
    for (double f : kTestFrequencies) OMNIS_CHECK_NEAR(gainDb(ap, f), 0.0, 1e-6);

    OMNIS_CHECK(gainDb(svfIr(SvfType::Notch, 1000.0, 1.0), 1000.0) < -80.0);
}

OMNIS_TEST("svf: stable sous modulation brutale de fc et Q (bruit, 1 s)") {
    SvfState s;
    std::mt19937 rng(9);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    double peak = 0.0;
    for (int i = 0; i < 48000; ++i) {
        // fc saute entre 20 Hz et 20 kHz à chaque échantillon, Q jusqu'à 20.
        const double fc = (i % 2) ? 20.0 : 20000.0;
        const auto c = SvfCoefficients::make(SvfType::LowPass, kFs, fc, 0.5 + 19.5 * ((i / 7) % 2));
        const double y = s.process(c, 0.5 * u(rng));
        peak = std::max(peak, std::fabs(y));
    }
    OMNIS_CHECK(std::isfinite(peak) && peak < 50.0);
}

OMNIS_TEST("linkwitz-riley: -6.02 dB par voie à fc, somme = passe-tout exact (LR4, LR8)") {
    for (CrossoverSlope slope : {CrossoverSlope::LR4, CrossoverSlope::LR8}) {
        for (double fc : {40.0, 120.0, 2000.0}) {
            LinkwitzRileyCrossover<1> xo;
            xo.setup(kFs, fc, slope);
            LinkwitzRileyAllpass<1> ap;
            ap.setup(kFs, fc, slope);
            std::vector<double> lo, hi, sum, all;
            for (int i = 0; i < kIrLength; ++i) {
                const double x = i == 0 ? 1.0 : 0.0;
                double l, h;
                xo.process(0, x, l, h);
                lo.push_back(l);
                hi.push_back(h);
                sum.push_back(l + h);
                all.push_back(ap.process(0, x));
            }
            OMNIS_CHECK_NEAR(gainDb(lo, fc), -6.0206, 0.005);
            OMNIS_CHECK_NEAR(gainDb(hi, fc), -6.0206, 0.005);
            double maxDiff = 0.0;
            for (std::size_t i = 0; i < sum.size(); ++i) maxDiff = std::max(maxDiff, std::fabs(sum[i] - all[i]));
            OMNIS_CHECK(maxDiff < 1e-12);
            for (double f : kTestFrequencies) OMNIS_CHECK_NEAR(gainDb(sum, f), 0.0, 1e-6);
            // Pente asymptotique : 24 ou 48 dB/oct, mesurée 3 octaves sous fc.
            const double expected = slope == CrossoverSlope::LR4 ? -72.2 : -144.5;
            OMNIS_CHECK_NEAR(gainDb(hi, fc / 8.0), expected, 0.6);
        }
    }
}

OMNIS_TEST("crossover network: 6 bandes, somme plate à ±1e-6 dB, bandes isolées") {
    const double freqs[] = {60.0, 120.0, 250.0, 2000.0, 6000.0};
    for (CrossoverSlope slope : {CrossoverSlope::LR4, CrossoverSlope::LR8}) {
        CrossoverNetwork<1> net;
        net.setup(kFs, freqs, 6, slope);
        std::vector<std::vector<double>> bands(6);
        std::vector<double> sum;
        double b[CrossoverNetwork<1>::kMaxBands];
        for (int i = 0; i < kIrLength; ++i) {
            net.process(0, i == 0 ? 1.0 : 0.0, b);
            double s = 0.0;
            for (int k = 0; k < 6; ++k) {
                bands[static_cast<std::size_t>(k)].push_back(b[k]);
                s += b[k];
            }
            sum.push_back(s);
        }
        for (double f : kTestFrequencies) OMNIS_CHECK_NEAR(gainDb(sum, f), 0.0, 1e-6);
        // Bande 120–250 Hz à son centre géométrique (173.2 Hz), valeur théorique :
        //   LR4 : |LP| = 1/(1+w⁴), |HP| = w⁴/(1+w⁴) -> -1.80 -1.80 -0.12 (HP 60 Hz) = -3.73 dB
        //   LR8 : mêmes formules en w⁸               -> -0.45 -0.45 -0.00            = -0.90 dB
        OMNIS_CHECK(gainDb(bands[0], 20.0) > -0.5);
        OMNIS_CHECK_NEAR(gainDb(bands[2], std::sqrt(120.0 * 250.0)), slope == CrossoverSlope::LR4 ? -3.726 : -0.900, 0.01);
        OMNIS_CHECK(gainDb(bands[5], 15000.0) > -0.5);
        // Fuite d'une bande loin de sa zone.
        OMNIS_CHECK(gainDb(bands[0], 2000.0) < -60.0);
    }
}
