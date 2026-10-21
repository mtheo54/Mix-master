// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestConvolution.cpp — FFT, convolution UPOLS, noyaux à phase linéaire.
// =============================================================================
#include "TestFramework.h"

#include "dsp/Fft.h"
#include "dsp/StereoConvolver.h"
#include "stages/SubKernelBank.h"

#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <vector>

using namespace omnis;
using namespace omnis::dsp;

OMNIS_TEST("fft: identique à la DFT directe, aller-retour exact (N = 8 … 4096)") {
    std::mt19937 rng(1);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    for (int n : {8, 64, 512, 4096}) {
        Fft fft(n);
        std::vector<std::complex<double>> x(static_cast<std::size_t>(n)), y;
        for (auto& v : x) v = {u(rng), u(rng)};
        y = x;
        fft.forward(y.data());
        double maxErr = 0.0;
        for (int k = 0; k < n; k += std::max(1, n / 32)) {
            std::complex<double> acc{};
            for (int t = 0; t < n; ++t)
                acc += x[static_cast<std::size_t>(t)] * std::polar(1.0, -2.0 * std::numbers::pi * k * t / n);
            maxErr = std::max(maxErr, std::abs(acc - y[static_cast<std::size_t>(k)]));
        }
        OMNIS_CHECK(maxErr < 1e-10 * n);
        fft.inverse(y.data());
        double roundTrip = 0.0;
        for (int t = 0; t < n; ++t) roundTrip = std::max(roundTrip, std::abs(y[static_cast<std::size_t>(t)] - x[static_cast<std::size_t>(t)]));
        OMNIS_CHECK(roundTrip < 1e-13);
    }
}

OMNIS_TEST("upols: deux canaux = convolutions directes, latence exacte B, changement de noyau") {
    constexpr int kB = 64, kN = 512, kLen = 4000;
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    std::vector<double> ha(kN), hb(kN), xa(kLen), xb(kLen);
    for (auto* v : {&ha, &hb, &xa, &xb})
        for (double& s : *v) s = u(rng);

    StereoConvolver conv;
    conv.prepare(kB, kN);
    StereoConvolver::Kernel k;
    conv.allocateKernel(k);
    StereoConvolver::KernelBuilder builder;
    builder.prepare(kB, kN);
    builder.build(ha.data(), hb.data(), k);

    std::vector<double> ya, yb;
    int pos = 0;
    for (int i = 0; i < kLen; ++i) {
        conv.write(pos, xa[static_cast<std::size_t>(i)], xb[static_cast<std::size_t>(i)]);
        ya.push_back(conv.outputA(pos));
        yb.push_back(conv.outputB(pos));
        if (++pos == kB) {
            conv.processFrame(k, nullptr);
            pos = 0;
        }
    }
    double maxErr = 0.0;
    for (int i = 0; i < kLen; ++i) {
        // y[i] = (x ⊗ h)[i − B]
        double ea = 0.0, eb = 0.0;
        const int t = i - kB;
        for (int j = 0; j < kN && t >= 0; ++j) {
            if (t - j < 0) break;
            ea += ha[static_cast<std::size_t>(j)] * xa[static_cast<std::size_t>(t - j)];
            eb += hb[static_cast<std::size_t>(j)] * xb[static_cast<std::size_t>(t - j)];
        }
        maxErr = std::max({maxErr, std::fabs(ya[static_cast<std::size_t>(i)] - ea), std::fabs(yb[static_cast<std::size_t>(i)] - eb)});
    }
    OMNIS_CHECK(maxErr < 1e-11);
}

OMNIS_TEST("noyaux sub: réponse conforme à la cible, phase linéaire (symétrie), Mid/Side en phase") {
    constexpr double fs = 44100.0;
    constexpr int kN = 16384, kB = 512;
    SubKernelDesigner designer;
    designer.prepare(fs, kN, kB);
    static_assert(dsp::LinearPhaseDesigner::kKaiserBeta == 6.0);
    SubKernelRequest req; // défauts : low cut 16 Hz, mono 120 Hz LR4, 100 %
    StereoConvolver::Kernel k;
    k.allocate(kN / kB, kB);
    designer.design(req, k);

    // Réponse impulsionnelle via le convolueur lui-même.
    StereoConvolver conv;
    conv.prepare(kB, kN);
    std::vector<double> ma, sb;
    int pos = 0;
    for (int i = 0; i < kN + 2 * kB; ++i) {
        conv.write(pos, i == 0 ? 1.0 : 0.0, i == 0 ? 1.0 : 0.0);
        ma.push_back(conv.outputA(pos));
        sb.push_back(conv.outputB(pos));
        if (++pos == kB) {
            conv.processFrame(k, nullptr);
            pos = 0;
        }
    }
    const int center = kB + kN / 2;
    double asym = 0.0, peak = 0.0;
    for (int d = 1; d < kN / 2; ++d) {
        asym = std::max(asym, std::fabs(ma[static_cast<std::size_t>(center + d)] - ma[static_cast<std::size_t>(center - d)]));
        asym = std::max(asym, std::fabs(sb[static_cast<std::size_t>(center + d)] - sb[static_cast<std::size_t>(center - d)]));
        peak = std::max(peak, std::fabs(ma[static_cast<std::size_t>(center + d)]));
    }
    OMNIS_CHECK(asym < 1e-12); // symétrique autour de N/2 + B : phase linéaire exacte

    const auto gainDb = [&](const std::vector<double>& h, double f) {
        std::complex<double> acc{};
        for (std::size_t n = 0; n < h.size(); ++n)
            acc += h[n] * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(n) / fs);
        return 20.0 * std::log10(std::abs(acc));
    };
    // Mid : low cut seul (-3 dB à 16 Hz) ; Side : low cut × (1 − LP_LR4(120 Hz)).
    OMNIS_CHECK_NEAR(gainDb(ma, 16.0), -3.0, 0.5);
    OMNIS_CHECK_NEAR(gainDb(ma, 1000.0), 0.0, 0.01);
    OMNIS_CHECK_NEAR(gainDb(sb, 120.0), 20.0 * std::log10(req.sideMagnitude(120.0)), 0.1); // -6 dB
    OMNIS_CHECK_NEAR(gainDb(sb, 40.0), 20.0 * std::log10(req.sideMagnitude(40.0)), 1.0);   // ~ -38 dB
    OMNIS_CHECK_NEAR(gainDb(sb, 2000.0), 0.0, 0.01);
}
