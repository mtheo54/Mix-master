// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestDynamics.cpp — Courbes ADAA, clipper suréchantillonné, limiteur.
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "analysis/TruePeakMeter.h"
#include "core/Decibels.h"
#include "dsp/Waveshaper.h"
#include "stages/ClipperStage.h"
#include "stages/LimiterStage.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <vector>

using namespace omnis;
using params::ParamId;

namespace {

constexpr double kFs = 44100.0;

struct Stereo {
    std::vector<double> l, r;
};

Stereo sine(double f, double amplitude, double seconds) {
    Stereo s;
    const int n = static_cast<int>(seconds * kFs);
    for (int i = 0; i < n; ++i) {
        const double v = amplitude * std::sin(2.0 * std::numbers::pi * f * i / kFs);
        s.l.push_back(v);
        s.r.push_back(v);
    }
    return s;
}

template <typename StageT>
void run(StageT& st, Stereo& s, int block = 256) {
    const int n = static_cast<int>(s.l.size());
    for (int pos = 0; pos < n; pos += block) {
        double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
        st.process(ch, std::min(block, n - pos));
    }
}

std::complex<double> dft(const std::vector<double>& x, double f, std::size_t start, std::size_t length) {
    std::complex<double> acc{};
    for (std::size_t n = start; n < start + length; ++n)
        acc += x[n] * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(n) / kFs);
    return acc;
}

/// Puissance hors harmoniques de f0 (repliement + bruit) relative à la puissance
/// totale, sur une seconde exacte (f0 entier -> harmoniques sur des bins entiers).
double aliasingDb(const std::vector<double>& x, double f0, std::size_t start) {
    const std::size_t n = static_cast<std::size_t>(kFs);
    double total = 0.0;
    for (std::size_t i = start; i < start + n; ++i) total += x[i] * x[i];
    double harmonic = std::norm(dft(x, 0.0, start, n)) / static_cast<double>(n);
    for (double f = f0; f < kFs / 2; f += f0) harmonic += 2.0 * std::norm(dft(x, f, start, n)) / static_cast<double>(n);
    return 10.0 * std::log10(std::max(total - harmonic, 1e-300) / total);
}

double peakOf(const Stereo& s, std::size_t start = 0) {
    double p = 0.0;
    for (std::size_t i = start; i < s.l.size(); ++i) p = std::max({p, std::fabs(s.l[i]), std::fabs(s.r[i])});
    return p;
}

double truePeakOf(const Stereo& s) {
    analysis::TruePeakMeter m;
    m.prepare(2);
    const double* ch[2] = {s.l.data(), s.r.data()};
    m.process(ch, static_cast<int>(s.l.size()));
    std::vector<double> z(64, 0.0);
    const double* zz[2] = {z.data(), z.data()};
    m.process(zz, 64);
    return m.truePeakMax();
}

} // namespace

OMNIS_TEST("courbes: pente 1 à l'origine, bornées, antidérivées cohérentes (dF/du = f)") {
    using namespace dsp::shapes;
    for (double u = -4.0; u <= 4.0; u += 0.01) {
        const double h = 1e-6;
        OMNIS_CHECK_NEAR((hardIntegral(u + h) - hardIntegral(u - h)) / (2 * h), hard(u), 1e-5);
        OMNIS_CHECK_NEAR((softKneeIntegral(u + h, 0.5) - softKneeIntegral(u - h, 0.5)) / (2 * h), softKnee(u, 0.5), 1e-5);
        OMNIS_CHECK_NEAR((diodeIntegral(u + h) - diodeIntegral(u - h)) / (2 * h), diode(u), 1e-5);
        OMNIS_CHECK(std::fabs(softKnee(u, 0.5)) <= 1.0 && std::fabs(diode(u)) <= 1.0);
    }
    OMNIS_CHECK_NEAR((diode(1e-6) - diode(-1e-6)) / 2e-6, 1.0, 1e-5);
    OMNIS_CHECK_NEAR(softKnee(0.3, 0.5), 0.3, 1e-15); // zone linéaire sous 1 − k
}

OMNIS_TEST("clipper: transparent sous le genou (-12 dBFS : gain 0 dB, distorsion < -100 dB)") {
    ClipperStage st;
    st.prepare({kFs, 256, 2});
    Stereo s = sine(1000.0, 0.25, 1.5);
    run(st, s);
    const std::size_t start = 22050;
    const double gain = 2.0 * std::abs(dft(s.l, 1000.0, start, 44100)) / 44100.0 / 0.25;
    OMNIS_CHECK_NEAR(20.0 * std::log10(gain), 0.0, 0.01);
    OMNIS_CHECK(aliasingDb(s.l, 1000.0, start) < -100.0);
}

OMNIS_TEST("clipper: repliement d'un écrêtage dur sévère selon le suréchantillonnage") {
    // Sinus 5 kHz à -6 dBFS, drive +12 dB : écrêtage dur de 6 dB.
    double previous = 0.0;
    for (int os : {0, 1, 3, 4}) {
        ClipperStage st;
        st.setParameter(ParamId::ClipMode, 0.0, true);
        st.setParameter(ParamId::ClipDrive, 12.0, true);
        st.setParameter(ParamId::ClipOversampling, os, true);
        st.prepare({kFs, 256, 2});
        Stereo s = sine(5000.0, 0.5, 1.5);
        run(st, s);
        const double a = aliasingDb(s.l, 5000.0, 22050);
        std::printf("    hard clip 6 dB, %2dx : produits non harmoniques = %.1f dB\n", 1 << os, a);
        if (os > 0) OMNIS_CHECK(a < previous); // chaque doublement améliore
        previous = a;
        if (os == 3) OMNIS_CHECK(a < -80.0);
    }
}

OMNIS_TEST("clipper: plafond respecté (dépassement < 0.2 dB) et DC retiré (Diode)") {
    for (int mode : {0, 1, 2}) {
        ClipperStage st;
        st.setParameter(ParamId::ClipMode, mode, true);
        st.setParameter(ParamId::ClipDrive, 12.0, true);
        st.setParameter(ParamId::ClipCeiling, -3.0, true);
        st.prepare({kFs, 256, 2});
        Stereo s = sine(100.0, 0.5, 3.0);
        run(st, s);
        const double peakDb = gainToDb(peakOf(s, 44100));
        std::printf("    mode %d : crête de sortie %.2f dBFS (plafond -3)\n", mode, peakDb);
        // Hard : Gibbs du signal écrêté re-filtré (+0.05 dB mesuré). Diode : le
        // retrait du DC de la forme asymétrique abaisse la crête positive (~-1 dB).
        OMNIS_CHECK(peakDb < -3.0 + 0.2 && peakDb > -3.0 - (mode == 2 ? 1.5 : 0.2));
        double mean = 0.0;
        for (std::size_t i = 88200; i < s.l.size(); ++i) mean += s.l[i];
        mean /= static_cast<double>(s.l.size() - 88200);
        OMNIS_CHECK(std::fabs(mean) < 1e-3);
        if (mode == 2) {
            // Asymétrie -> 2e harmonique présente (couleur « diode »).
            const double h2 = 2.0 * std::abs(dft(s.l, 200.0, 88200, 44100)) / 44100.0;
            OMNIS_CHECK(gainToDb(h2) > -40.0);
        }
    }
}

OMNIS_TEST("limiteur sample peak: aucun échantillon au-dessus du plafond (+12 dB sur la boucle)") {
    LimiterStage st;
    st.setParameter(ParamId::LimMode, 1.0, true);
    st.setParameter(ParamId::LimGain, 12.0, true);
    st.setParameter(ParamId::LimCeiling, -1.0, true);
    st.prepare({kFs, 256, 2});
    const auto drums = tools::signals::drumLoop(kFs, 4);
    Stereo s{drums.channels[0], drums.channels[1]};
    run(st, s, 199);
    OMNIS_CHECK(gainToDb(peakOf(s)) <= -1.0 + 1e-9);
    OMNIS_CHECK(gainToDb(peakOf(s)) > -1.5); // il limite vraiment jusqu'au plafond
}

OMNIS_TEST("limiteur true peak: crête vraie de sortie sous le plafond (boucle, ISP, bruit, +12 dB)") {
    std::vector<tools::AudioFile> sources{tools::signals::drumLoop(kFs, 4), tools::signals::intersamplePeak(kFs, 3.0, -6.0),
                                          tools::signals::pinkNoise(kFs, 5.0, -18.0)};
    for (const auto& src : sources) {
        LimiterStage st;
        st.setParameter(ParamId::LimGain, 12.0, true);
        st.setParameter(ParamId::LimCeiling, -1.0, true);
        st.prepare({kFs, 256, 2});
        Stereo s{src.channels[0], src.channels[1]};
        run(st, s, 256);
        const double tp = gainToDb(truePeakOf(s));
        std::printf("    true peak de sortie : %.3f dBTP (plafond -1.000)\n", tp);
        OMNIS_CHECK(tp <= -1.0 + 0.01);
    }
}

OMNIS_TEST("limiteur: transparent sous le seuil (sortie = entrée retardée, bit à bit)") {
    LimiterStage st;
    st.prepare({kFs, 256, 2});
    Stereo s = sine(440.0, 0.3, 1.0);
    const Stereo ref = s;
    run(st, s, 100);
    const auto lat = static_cast<std::size_t>(st.latencySamples());
    bool exact = true;
    for (std::size_t i = lat; i < s.l.size(); ++i)
        if (s.l[i] != ref.l[i - lat] || s.r[i] != ref.r[i - lat]) exact = false;
    OMNIS_CHECK(exact);
    OMNIS_CHECK(lat == static_cast<std::size_t>(std::lround(0.005 * kFs)) + analysis::TruePeakMeter::kHalfLength);
}

OMNIS_TEST("limiteur: relâchement exponentiel (63 % de remontée en ~release)") {
    LimiterStage st;
    st.setParameter(ParamId::LimMode, 1.0, true);
    st.setParameter(ParamId::LimCeiling, -6.0, true);
    st.setParameter(ParamId::LimRelease, 100.0, true);
    st.prepare({kFs, 256, 2});
    // Impulsion de 0 dBFS puis signal constant faible : on suit le gain.
    Stereo s{std::vector<double>(44100, 0.01), std::vector<double>(44100, 0.01)};
    s.l[1000] = s.r[1000] = 1.0;
    run(st, s, 64);
    const auto lat = static_cast<std::size_t>(st.latencySamples());
    const double g0 = s.l[1000 + lat] / 1.0;  // gain à la crête ≈ -6 dB
    const double g100 = s.l[1000 + lat + 4410] / 0.01; // 100 ms après
    OMNIS_CHECK(gainToDb(g0) <= -6.0 + 1e-9);
    // Remontée de -6 dB vers 0 dB : après τ, gain ≈ 0.5 + 0.63·0.5 (linéaire).
    OMNIS_CHECK_NEAR(g100, 0.501 + (1.0 - 0.501) * (1.0 - std::exp(-1.0)), 0.03);
}
