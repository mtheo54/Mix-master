// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestInflatorImager.cpp — Étages 3 (Inflator) et 4 (Imager).
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "stages/ImagerStage.h"
#include "stages/InflatorStage.h"

#include <algorithm>
#include <cmath>
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

/// Mid de fréquence fm (amplitude am) et Side de fréquence fs (amplitude as).
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

template <typename StageT>
void run(StageT& st, Stereo& s, int block = 256) {
    const int n = static_cast<int>(s.l.size());
    for (int pos = 0; pos < n; pos += block) {
        double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
        st.process(ch, std::min(block, n - pos));
    }
}

/// Amplitude de la composante f sur la dernière seconde.
double amplitudeAt(const std::vector<double>& x, double f) {
    const std::size_t n = static_cast<std::size_t>(kFs);
    std::complex<double> acc{0.0, 0.0};
    for (std::size_t i = x.size() - n; i < x.size(); ++i)
        acc += x[i] * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(i) / kFs);
    return 2.0 * std::abs(acc) / static_cast<double>(n);
}

double meanOfLastSecond(const std::vector<double>& x) {
    const std::size_t n = static_cast<std::size_t>(kFs);
    double s = 0.0;
    for (std::size_t i = x.size() - n; i < x.size(); ++i) s += x[i];
    return s / static_cast<double>(n);
}

double rmsOfLastSecond(const std::vector<double>& x) {
    const std::size_t n = static_cast<std::size_t>(kFs);
    double s = 0.0;
    for (std::size_t i = x.size() - n; i < x.size(); ++i) s += x[i] * x[i];
    return std::sqrt(s / static_cast<double>(n));
}

double peakOfLastSecond(const std::vector<double>& x) {
    double p = 0.0;
    for (std::size_t i = x.size() - static_cast<std::size_t>(kFs); i < x.size(); ++i) p = std::max(p, std::fabs(x[i]));
    return p;
}

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

double db(double a) { return 20.0 * std::log10(std::max(a, 1e-300)); }

} // namespace

// -----------------------------------------------------------------------------
//  Inflator
// -----------------------------------------------------------------------------
OMNIS_TEST("inflator: courbe (g(0) = 0, g(1) = 1 sans cassure, pente à l'origine m/(m-1), impaire)") {
    for (double curve : {-50.0, 0.0, 50.0}) {
        const double m = InflatorCurve::exponentFor(curve);
        OMNIS_CHECK_NEAR(InflatorCurve::shape(0.0, m), 0.0, 1e-15);
        OMNIS_CHECK_NEAR(InflatorCurve::shape(1.0, m), 1.0, 1e-12);
        OMNIS_CHECK_NEAR(InflatorCurve::slope(1.0 - 1e-9, m), 0.0, 1e-6);
        OMNIS_CHECK_NEAR(InflatorCurve::slope(0.0, m), m / (m - 1.0), 1e-12);
        OMNIS_CHECK_NEAR(InflatorCurve::shape(-0.4, m), -InflatorCurve::shape(0.4, m), 1e-15);
        OMNIS_CHECK(InflatorCurve::shape(2.0, m) == 1.0); // au-delà : plafond
    }
    OMNIS_CHECK_NEAR(InflatorCurve::exponentFor(-50.0), 2.0, 1e-12);
    OMNIS_CHECK_NEAR(InflatorCurve::exponentFor(50.0), 6.0, 1e-12);
}

OMNIS_TEST("inflator: réglages par défaut = entrée retardée, au bit près") {
    InflatorStage st;
    st.prepare({kFs, 256, 2});
    const auto noise = tools::signals::pinkNoise(kFs, 1.0, -12.0, 3, true);
    Stereo s{noise.channels[0], noise.channels[1]};
    run(st, s, 200);
    const auto lat = static_cast<std::size_t>(st.latencySamples());
    bool exact = true;
    for (std::size_t i = lat; i < s.l.size(); ++i)
        if (s.l[i] != noise.channels[0][i - lat] || s.r[i] != noise.channels[1][i - lat]) exact = false;
    OMNIS_CHECK(exact);
}

OMNIS_TEST("inflator: densité sans crête plus haute (mix 100 %, courbe par défaut = théorie)") {
    // Gain RMS attendu d'un sinus passé par g (m = 4), intégré numériquement :
    // +0.92 dB à pleine échelle (la crête reste à 1), +2.50 dB à -20 dBFS
    // (pente 4/3 à l'origine) : les passages faibles gagnent le plus.
    const auto gainFor = [](double amplitude) {
        InflatorStage st;
        st.prepare({kFs, 256, 2});
        st.setParameter(ParamId::InflHighEffect, 100.0, true);
        Stereo s = midSide(1000.0, amplitude, 1000.0, 0.0, 2.0);
        run(st, s);
        return std::pair{db(rmsOfLastSecond(s.l) / (amplitude / std::sqrt(2.0))), peakOfLastSecond(s.l) / amplitude};
    };
    const auto [loud, loudPeak] = gainFor(1.0);
    OMNIS_CHECK_NEAR(loud, 0.916, 0.01);
    OMNIS_CHECK(loudPeak < 1.02); // crête toujours à la pleine échelle
    OMNIS_CHECK_NEAR(gainFor(0.1).first, 2.497, 0.01);

    InflatorStage st;
    st.prepare({kFs, 256, 2});
    st.setParameter(ParamId::InflHighEffect, 100.0, true);
    Stereo s = midSide(1000.0, 1.0, 1000.0, 0.0, 2.0);
    run(st, s);
    // Courbe impaire : harmoniques impaires seulement.
    const double h1 = amplitudeAt(s.l, 1000.0);
    OMNIS_CHECK(db(amplitudeAt(s.l, 3000.0) / h1) > -40.0);
    OMNIS_CHECK(db(amplitudeAt(s.l, 2000.0) / h1) < -90.0);
}

OMNIS_TEST("inflator: chaleur = harmoniques paires, sans continu ; 4x : pas de repliement") {
    InflatorStage st;
    st.prepare({kFs, 256, 2});
    st.setParameter(ParamId::InflHighEffect, 100.0, true);
    st.setParameter(ParamId::InflWarmth, 100.0, true);
    Stereo s = midSide(1000.0, 0.8, 1000.0, 0.0, 2.0);
    run(st, s);
    const double h1 = amplitudeAt(s.l, 1000.0);
    OMNIS_CHECK(db(amplitudeAt(s.l, 2000.0) / h1) > -45.0);
    OMNIS_CHECK(std::fabs(meanOfLastSecond(s.l)) < 1e-4);

    // 9 kHz à pleine échelle : sa 5e harmonique (45 kHz) se replierait à 3 kHz sans suréchantillonnage.
    InflatorStage hot;
    hot.prepare({kFs, 256, 2});
    hot.setParameter(ParamId::InflHighEffect, 100.0, true);
    hot.setParameter(ParamId::InflCurve, 50.0, true);
    Stereo t = midSide(9000.0, 1.0, 9000.0, 0.0, 2.0);
    run(hot, t);
    OMNIS_CHECK(db(amplitudeAt(t.l, 3000.0) / amplitudeAt(t.l, 9000.0)) < -70.0);
}

OMNIS_TEST("inflator: « Inflate 808 » ne touche que le bas (50 Hz gonflé, 1 kHz intact)") {
    const auto gainAt = [](double f) {
        InflatorStage st;
        st.prepare({kFs, 256, 2});
        st.setParameter(ParamId::InflLowEffect, 100.0, true);
        Stereo s = midSide(f, 0.25, f, 0.0, 2.0);
        run(st, s);
        return db(amplitudeAt(s.l, f) / 0.25);
    };
    OMNIS_CHECK(gainAt(50.0) > 1.5);                // densité de la 808
    OMNIS_CHECK(std::fabs(gainAt(1000.0)) < 0.02); // le mix au-dessus du split n'est pas touché
}

OMNIS_TEST("inflator caractères: courbes continues, f(0) = 0, pente 1 au repos (Bande, Lampe, Transistor)") {
    for (int c = 1; c <= 3; ++c)
        for (double m : {2.0, 4.0, 6.0}) {
            OMNIS_CHECK(InflatorCurve::shape(c, 0.0, m) == 0.0);
            OMNIS_CHECK(std::fabs(InflatorCurve::slope(c, 0.0, m) - 1.0) < 1e-12);
            // Dérivée numérique = pente annoncée, et la courbe ne dépasse jamais l'identité.
            for (double x = -1.5; x <= 1.5; x += 0.05) {
                const double h = 1e-6;
                const double num = (InflatorCurve::shape(c, x + h, m) - InflatorCurve::shape(c, x - h, m)) / (2 * h);
                OMNIS_CHECK(std::fabs(num - InflatorCurve::slope(c, x, m)) < 1e-5);
                OMNIS_CHECK(std::fabs(InflatorCurve::shape(c, x, m)) <= std::fabs(x) + 1e-12);
            }
        }
    // Bande et Transistor impairs ; Lampe asymétrique (plus douce en négatif).
    OMNIS_CHECK(std::fabs(InflatorCurve::shape(1, 0.7, 4.0) + InflatorCurve::shape(1, -0.7, 4.0)) < 1e-12);
    OMNIS_CHECK(std::fabs(InflatorCurve::shape(3, 0.7, 4.0) + InflatorCurve::shape(3, -0.7, 4.0)) < 1e-12);
    OMNIS_CHECK(std::fabs(InflatorCurve::shape(2, -0.7, 4.0)) > std::fabs(InflatorCurve::shape(2, 0.7, 4.0)) + 0.01);
    // Inflate (0) : la courbe d'origine, inchangée.
    OMNIS_CHECK(InflatorCurve::shape(0, 0.5, 3.0) == InflatorCurve::shape(0.5, 3.0));
}

OMNIS_TEST("inflator caractères: Bande / Transistor = harmoniques impaires, Lampe = paires ; crête jamais plus haute ; à 0 % rien") {
    struct Measure {
        double h2, h3, h5, peak;
    };
    const auto measure = [](int character, double effect) {
        InflatorStage st;
        st.prepare({kFs, 256, 2});
        st.setParameter(ParamId::InflCharacter, character, true);
        st.setParameter(ParamId::InflHighEffect, effect, true);
        Stereo s = midSide(1000.0, 0.8, 1000.0, 0.0, 2.0);
        run(st, s);
        const double h1 = amplitudeAt(s.l, 1000.0);
        return Measure{db(amplitudeAt(s.l, 2000.0) / h1), db(amplitudeAt(s.l, 3000.0) / h1), db(amplitudeAt(s.l, 5000.0) / h1),
                       peakOfLastSecond(s.l)};
    };
    const char* names[] = {"Inflate", "Bande", "Lampe", "Transistor"};
    Measure m[4];
    for (int c = 0; c < 4; ++c) {
        m[c] = measure(c, 100.0);
        std::printf("      %-10s H2 %6.1f dB  H3 %6.1f dB  H5 %6.1f dB  crête %.3f\n", names[c], m[c].h2, m[c].h3, m[c].h5, m[c].peak);
        // Bande, Lampe, Transistor tassent la crête (0,8) ; Inflate la densifie sans passer 0 dBFS.
        OMNIS_CHECK(m[c].peak <= (c == 0 ? 1.0 : 0.8 * 1.0001));
    }
    OMNIS_CHECK(m[1].h3 > -40.0 && m[1].h2 < -80.0);              // Bande : impaires
    OMNIS_CHECK(m[3].h2 < -80.0 && m[3].h5 - m[3].h3 > m[1].h5 - m[1].h3 + 10.0); // Transistor : impaires hautes
    OMNIS_CHECK(m[2].h2 > -40.0 && m[2].h2 > m[2].h3);            // Lampe : paires dominantes
    // Effet à 0 % : entrée retardée, quel que soit le caractère.
    for (int c = 1; c < 4; ++c) {
        InflatorStage st;
        st.prepare({kFs, 256, 2});
        st.setParameter(ParamId::InflCharacter, c, true);
        Stereo s = midSide(1000.0, 0.8, 1000.0, 0.0, 0.5);
        const Stereo in = s;
        run(st, s);
        const auto lat = static_cast<std::size_t>(st.latencySamples());
        double diff = 0.0;
        for (std::size_t i = lat; i < s.l.size(); ++i) diff = std::max(diff, std::fabs(s.l[i] - in.l[i - lat]));
        OMNIS_CHECK(diff == 0.0);
    }
}

OMNIS_TEST("inflator caractères: changer de caractère en lecture ne claque pas (fondu de 10 ms)") {
    InflatorStage st;
    st.prepare({kFs, 256, 2});
    st.setParameter(ParamId::InflHighEffect, 100.0, true);
    st.setParameter(ParamId::InflDrive, 6.0, true);
    Stereo s = midSide(220.0, 0.7, 220.0, 0.0, 1.0);
    // Bloc par bloc, le caractère change toutes les 0,1 s (automation brutale).
    const int block = 256;
    std::vector<double> l(block), r(block);
    double maxStep = 0.0, prev = 0.0;
    const int n = static_cast<int>(s.l.size());
    for (int pos = 0, b = 0; pos + block <= n; pos += block, ++b) {
        if (b % 19 == 0) st.setParameter(ParamId::InflCharacter, (b / 19) % 4, false);
        std::copy(s.l.begin() + pos, s.l.begin() + pos + block, l.begin());
        std::copy(s.r.begin() + pos, s.r.begin() + pos + block, r.begin());
        double* ch[2] = {l.data(), r.data()};
        st.process(ch, block);
        for (int i = 0; i < block; ++i) {
            if (pos + i > st.latencySamples() + 10) maxStep = std::max(maxStep, std::fabs(l[static_cast<std::size_t>(i)] - prev));
            prev = l[static_cast<std::size_t>(i)];
        }
    }
    // Un sinus de 220 Hz à 0,7 bouge d'au plus 2π·220/48000·0,7 ≈ 0,02 par échantillon ; saturé un peu plus.
    std::printf("      plus grand saut entre deux échantillons : %.4f\n", maxStep);
    OMNIS_CHECK(maxStep < 0.05);
}

// -----------------------------------------------------------------------------
//  Imager
// -----------------------------------------------------------------------------
OMNIS_TEST("imager: largeurs à 100 % = entrée retardée (1e-12)") {
    ImagerStage st;
    st.prepare({kFs, 256, 2});
    const auto noise = tools::signals::pinkNoise(kFs, 1.0, -12.0, 4, true);
    Stereo s{noise.channels[0], noise.channels[1]};
    run(st, s, 200);
    const auto lat = static_cast<std::size_t>(st.latencySamples());
    double maxDiff = 0.0;
    for (std::size_t i = lat; i < s.l.size(); ++i) {
        maxDiff = std::max(maxDiff, std::fabs(s.l[i] - noise.channels[0][i - lat]));
        maxDiff = std::max(maxDiff, std::fabs(s.r[i] - noise.channels[1][i - lat]));
    }
    OMNIS_CHECK(maxDiff < 1e-12);
}

OMNIS_TEST("imager: aigu mono (0 %) et bas-médium élargi (200 %), Mid jamais touché") {
    const auto measure = [](double fm, double fs) {
        ImagerStage st;
        st.prepare({kFs, 256, 2});
        st.setParameter(static_cast<ParamId>(static_cast<int>(ParamId::ImgWidthFirst) + 3), 0.0, true);   // > 8 kHz : mono
        st.setParameter(static_cast<ParamId>(static_cast<int>(ParamId::ImgWidthFirst) + 1), 200.0, true); // 250 Hz – 2 kHz
        Stereo s = midSide(fm, 0.25, fs, 0.25, 2.0);
        run(st, s);
        return std::pair{db(amplitudeAt(midOf(s), fm) / 0.25), db(amplitudeAt(sideOf(s), fs) / 0.25)};
    };
    const auto [mid12k, side12k] = measure(12000.0, 12000.0);
    OMNIS_CHECK_NEAR(mid12k, 0.0, 0.02);
    OMNIS_CHECK(side12k < -50.0);
    const auto [mid1k, side1k] = measure(1000.0, 1000.0);
    OMNIS_CHECK_NEAR(mid1k, 0.0, 0.02);
    OMNIS_CHECK_NEAR(side1k, 6.02, 0.05);
    const auto [mid4k, side4k] = measure(4000.0, 4000.0); // bande médium-aigu : inchangée
    OMNIS_CHECK_NEAR(mid4k, 0.0, 0.02);
    OMNIS_CHECK_NEAR(side4k, 0.0, 0.05);
}

OMNIS_TEST("imager: transitions douces et fréquences saisies dans le désordre") {
    ImagerKernelRequest r;
    r.width = {1.0, 2.0, 1.0, 0.0};
    // Au milieu d'une transition : moyenne des deux bandes ; loin : la bande.
    OMNIS_CHECK_NEAR(r.sideMagnitude(r.splitHz[0]), 1.5, 1e-12);
    OMNIS_CHECK_NEAR(r.sideMagnitude(1000.0), 2.0, 1e-12);
    OMNIS_CHECK_NEAR(r.sideMagnitude(16000.0), 0.0, 1e-12);
    // Aucune marche : variation bornée entre deux fréquences proches.
    double maxStep = 0.0;
    for (double f = 100.0; f < 20000.0; f *= 1.01) maxStep = std::max(maxStep, std::fabs(r.sideMagnitude(f * 1.01) - r.sideMagnitude(f)));
    OMNIS_CHECK(maxStep < 0.05);

    // Fréquences inversées : même résultat que dans l'ordre.
    const auto sideAt = [](double a, double b, double c) {
        ImagerStage st;
        st.prepare({kFs, 256, 2});
        st.setParameter(ParamId::ImgSplitLow, a, true);
        st.setParameter(ParamId::ImgSplitMid, b, true);
        st.setParameter(ParamId::ImgSplitHigh, c, true);
        st.setParameter(static_cast<ParamId>(static_cast<int>(ParamId::ImgWidthFirst) + 3), 0.0, true);
        Stereo s = midSide(1000.0, 0.0, 10000.0, 0.25, 1.5);
        run(st, s);
        return amplitudeAt(sideOf(s), 10000.0);
    };
    OMNIS_CHECK_NEAR(sideAt(8000.0, 2000.0, 250.0), sideAt(250.0, 2000.0, 8000.0), 1e-9);
}
