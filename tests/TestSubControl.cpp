// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestSubControl.cpp — Étage Sub Control & Mono Maker.
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "engine/Engine.h"
#include "engine/MasteringChain.h"
#include "stages/SubControlStage.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <complex>
#include <numbers>
#include <random>
#include <vector>

using namespace omnis;
using params::ParamId;

namespace {

constexpr double kFs = 48000.0;

struct Stereo {
    std::vector<double> l, r;
};

Stereo tone(double fl, double al, double fr, double ar, double seconds) {
    Stereo s;
    const int n = static_cast<int>(seconds * kFs);
    for (int i = 0; i < n; ++i) {
        s.l.push_back(al * std::sin(2.0 * std::numbers::pi * fl * i / kFs));
        s.r.push_back(ar * std::sin(2.0 * std::numbers::pi * fr * i / kFs));
    }
    return s;
}

/// Traite en place par blocs de 256 (taille de buffer de l'utilisateur).
void run(SubControlStage& st, Stereo& s, int block = 256) {
    const int n = static_cast<int>(s.l.size());
    for (int pos = 0; pos < n; pos += block) {
        double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
        st.process(ch, std::min(block, n - pos));
    }
}

/// Amplitude de la composante à `f` sur la dernière seconde (nombre entier de cycles).
double amplitudeAt(const std::vector<double>& x, double f) {
    const std::size_t n = static_cast<std::size_t>(kFs);
    std::complex<double> acc{0.0, 0.0};
    for (std::size_t i = x.size() - n; i < x.size(); ++i)
        acc += x[i] * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(i) / kFs);
    return 2.0 * std::abs(acc) / static_cast<double>(n);
}

double db(double a) { return 20.0 * std::log10(std::max(a, 1e-300)); }

SubControlStage& prepared(SubControlStage& st) {
    st.prepare({kFs, 256, 2});
    return st;
}

std::vector<double> sideOf(const Stereo& s) {
    std::vector<double> v(s.l.size());
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = 0.5 * (s.l[i] - s.r[i]);
    return v;
}

} // namespace

OMNIS_TEST("mono maker: Side à 40 Hz supprimé (LR4 -37 dB, LR8 -70 dB), Side à 1 kHz intact") {
    for (int slope : {0, 1}) {
        SubControlStage st;
        st.setParameter(ParamId::MonoSlope, slope, true);
        st.setParameter(ParamId::LowCutFreq, 5.0, true);
        prepared(st);
        Stereo lowSide = tone(40.0, 0.5, 40.0, -0.5, 3.0); // S pur à 40 Hz
        run(st, lowSide);
        const double attenuation = db(amplitudeAt(sideOf(lowSide), 40.0) / 0.5);
        OMNIS_CHECK(attenuation < (slope == 0 ? -37.0 : -70.0));

        SubControlStage st2;
        st2.setParameter(ParamId::MonoSlope, slope, true);
        prepared(st2);
        Stereo highSide = tone(1000.0, 0.5, 1000.0, -0.5, 2.0);
        run(st2, highSide);
        OMNIS_CHECK_NEAR(db(amplitudeAt(sideOf(highSide), 1000.0) / 0.5), 0.0, 0.01);
    }
}

OMNIS_TEST("mono maker: un signal mono reste mono (|L − R| < 1e-12)") {
    SubControlStage st;
    st.setParameter(ParamId::SubSynth, 50.0, true);
    st.setParameter(ParamId::SubGain, 3.0, true);
    prepared(st);
    auto noise = tools::signals::pinkNoise(kFs, 2.0, -12.0, 5, false);
    Stereo s{noise.channels[0], noise.channels[1]};
    run(st, s, 333);
    // Chemin linéaire : Side nul en entrée -> sortie Side au bruit d'arrondi de
    // la FFT près (~1e-17), soit < -240 dBFS.
    double maxDiff = 0.0;
    for (std::size_t i = 0; i < s.l.size(); ++i) maxDiff = std::max(maxDiff, std::fabs(s.l[i] - s.r[i]));
    OMNIS_CHECK(maxDiff < 1e-12);
}

OMNIS_TEST("mono maker: image au-dessus de f_mono intacte (L seul à 2 kHz -> R reste muet)") {
    SubControlStage st;
    prepared(st);
    Stereo s = tone(2000.0, 0.5, 2000.0, 0.0, 2.0);
    run(st, s);
    OMNIS_CHECK_NEAR(db(amplitudeAt(s.l, 2000.0) / 0.5), 0.0, 0.01);
    OMNIS_CHECK(db(amplitudeAt(s.r, 2000.0) / 0.5) < -40.0);
}

OMNIS_TEST("mono maker: amount 0 % = passe-tout (module plat L et R)") {
    SubControlStage st;
    st.setParameter(ParamId::MonoAmount, 0.0, true);
    st.setParameter(ParamId::LowCutFreq, 5.0, true);
    prepared(st);
    for (double f : {30.0, 120.0, 400.0, 5000.0}) {
        SubControlStage local;
        local.setParameter(ParamId::MonoAmount, 0.0, true);
        local.setParameter(ParamId::LowCutFreq, 5.0, true);
        prepared(local);
        // Fréquences entières : nombre entier de cycles dans la fenêtre d'analyse.
        const double fr = std::round(f * 1.5);
        Stereo s = tone(f, 0.5, fr, 0.3, 3.0);
        run(local, s);
        OMNIS_CHECK_NEAR(db(amplitudeAt(s.l, f) / 0.5), 0.0, f < 50 ? 0.02 : 0.005);
        OMNIS_CHECK_NEAR(db(amplitudeAt(s.r, fr) / 0.3), 0.0, f < 50 ? 0.02 : 0.005);
    }
}

OMNIS_TEST("low cut: Butterworth 24 dB/oct, -3.01 dB à fc, DC supprimé") {
    // Signaux mono (L = R) : le Mono Maker n'intervient pas, seul le low cut agit.
    // Linéaire : le fenêtrage Kaiser lisse la réponse (-3.08 dB mesuré) -> ±0.1 dB.
    for (double phase : {0.0, 1.0}) {
        SubControlStage st;
        st.setParameter(ParamId::LowCutFreq, 40.0, true);
        st.setParameter(ParamId::SubPhaseMode, phase, true);
        prepared(st);
        Stereo s = tone(40.0, 0.5, 40.0, 0.5, 4.0);
        run(st, s);
        OMNIS_CHECK_NEAR(db(amplitudeAt(s.l, 40.0) / 0.5), -3.01, phase == 0.0 ? 0.1 : 0.05);
    }
    SubControlStage st10;
    st10.setParameter(ParamId::LowCutFreq, 40.0, true);
    prepared(st10);
    Stereo s10 = tone(10.0, 0.5, 10.0, 0.5, 4.0);
    run(st10, s10);
    OMNIS_CHECK(db(amplitudeAt(s10.l, 10.0) / 0.5) < -45.0); // 2 octaves sous fc : ~ -48 dB

    // DC : -60 dB minimum en phase linéaire (FIR fenêtré), extinction totale en
    // analogique (zéro du Butterworth au DC).
    for (double phase : {0.0, 1.0}) {
        SubControlStage dc;
        dc.setParameter(ParamId::SubPhaseMode, phase, true);
        prepared(dc);
        Stereo d{std::vector<double>(static_cast<std::size_t>(kFs * 3), 0.25), std::vector<double>(static_cast<std::size_t>(kFs * 3), -0.25)};
        run(dc, d);
        const double limit = phase == 0.0 ? 0.25e-3 : 1e-6;
        OMNIS_CHECK(std::fabs(d.l.back()) < limit && std::fabs(d.r.back()) < limit);
    }
}

OMNIS_TEST("sub shelf: +6 dB sous la fréquence, 0 dB au-dessus") {
    SubControlStage st;
    st.setParameter(ParamId::SubGain, 6.0, true);
    st.setParameter(ParamId::SubFreq, 70.0, true);
    st.setParameter(ParamId::LowCutFreq, 5.0, true);
    prepared(st);
    Stereo s = tone(15.0, 0.25, 15.0, 0.25, 3.0); // mono
    run(st, s);
    OMNIS_CHECK_NEAR(db(amplitudeAt(s.l, 15.0) / 0.25), 6.0, 0.25);
    SubControlStage hi;
    hi.setParameter(ParamId::SubGain, 6.0, true);
    prepared(hi);
    Stereo h = tone(2000.0, 0.25, 2000.0, 0.25, 2.0);
    run(hi, h);
    OMNIS_CHECK_NEAR(db(amplitudeAt(h.l, 2000.0) / 0.25), 0.0, 0.02);
}

OMNIS_TEST("sub synth: 80 Hz -> octave basse à 40 Hz au niveau de l'entrée, absente à 0 %") {
    for (double amount : {0.0, 100.0}) {
        SubControlStage st;
        st.setParameter(ParamId::SubSynth, amount, true);
        st.setParameter(ParamId::LowCutFreq, 5.0, true);
        prepared(st);
        Stereo s = tone(80.0, 0.4, 80.0, 0.4, 3.0);
        run(st, s);
        const double sub = db(amplitudeAt(s.l, 40.0) / 0.4);
        std::printf("    sub synth %.0f %% : composante 40 Hz = %.2f dB, 80 Hz = %.2f dB\n", amount, sub,
                    db(amplitudeAt(s.l, 80.0) / 0.4));
        if (amount == 0.0)
            OMNIS_CHECK(sub < -100.0);
        else
            OMNIS_CHECK_NEAR(sub, 0.0, 0.3);
        OMNIS_CHECK_NEAR(db(amplitudeAt(s.l, 80.0) / 0.4), 0.0, 0.2); // la fondamentale est conservée
    }
}

OMNIS_TEST("mono maker: changement de pente LR4 <-> LR8 en lecture sans discontinuité") {
    SubControlStage st;
    st.setParameter(ParamId::MonoFreq, 200.0, true);
    prepared(st);
    Stereo s = tone(150.0, 0.5, 150.0, -0.5, 2.0);
    const int n = static_cast<int>(s.l.size());
    for (int pos = 0, k = 0; pos < n; pos += 256, ++k) {
        if (k % 20 == 10) st.setParameter(ParamId::MonoSlope, (k / 20) % 2 ? 0.0 : 1.0, false);
        double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
        st.process(ch, std::min(256, n - pos));
    }
    // Un sinus à 150 Hz d'amplitude <= 0.5 a une pente max de 2π·150·0.5/fs ≈ 0.0098 par
    // échantillon ; un clic se verrait comme un saut bien supérieur.
    double maxStep = 0.0;
    for (int i = static_cast<int>(kFs * 0.5); i < n; ++i)
        maxStep = std::max(maxStep, std::fabs(s.l[static_cast<std::size_t>(i)] - s.l[static_cast<std::size_t>(i - 1)]));
    OMNIS_CHECK(maxStep < 0.0125);
}

OMNIS_TEST("chaîne complète: sortie indépendante du découpage, paramètres en mouvement") {
    const auto in = tools::signals::drumLoop(kFs, 1);
    std::vector<std::vector<double>> outs;
    for (int variant = 0; variant < 2; ++variant) {
        Engine e;
        buildMasteringChain(e);
        e.prepare(kFs, 512);
        std::vector<double> l = in.channels[0], r = in.channels[1];
        std::mt19937 rng(3);
        std::uniform_int_distribution<int> d(1, 700);
        const int n = static_cast<int>(l.size());
        int pos = 0, automationPoint = 0;
        while (pos < n) {
            // Points d'automation à des instants fixes (indépendants des blocs).
            int len = variant == 0 ? 512 : d(rng);
            const int nextAutomation = (automationPoint + 1) * 20000;
            len = std::min({len, n - pos, nextAutomation - pos});
            double* io[2] = {l.data() + pos, r.data() + pos};
            e.process(io, io, len);
            pos += len;
            if (pos == nextAutomation) {
                ++automationPoint;
                e.setParameter(ParamId::MonoFreq, 60.0 + 40.0 * automationPoint);
                e.setParameter(ParamId::SubGain, automationPoint % 2 ? -4.0 : 2.0);
                e.setParameter(ParamId::SubSynth, 30.0 * (automationPoint % 3));
            }
        }
        outs.push_back(l);
    }
    OMNIS_CHECK(outs[0] == outs[1]);
}

OMNIS_TEST("temps réel: le thread de calcul applique un nouveau noyau sans bloquer l'audio") {
    SubControlStage st;
    st.setRealtime(true);
    prepared(st);
    // Side pur à 40 Hz : supprimé à 100 % (défaut), restitué après passage à 0 %.
    Stereo s = tone(40.0, 0.5, 40.0, -0.5, 6.0);
    const int n = static_cast<int>(s.l.size());
    int pos = 0;
    for (; pos < n / 2; pos += 256) {
        double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
        st.process(ch, 256);
    }
    st.setParameter(ParamId::MonoAmount, 0.0, false);
    for (; pos + 256 <= n; pos += 256) {
        double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
        st.process(ch, 256);
        // Simule le rythme d'un hôte : laisse du temps CPU au thread de calcul.
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    const std::vector<double> side = sideOf(s);
    std::vector<double> last(side.begin(), side.begin() + pos);
    OMNIS_CHECK_NEAR(db(amplitudeAt(last, 40.0) / 0.5), 0.0, 0.2);
}

OMNIS_TEST("sub phase: bascule Linear <-> Analog en lecture sans discontinuité") {
    SubControlStage st;
    prepared(st);
    Stereo s = tone(150.0, 0.4, 210.0, 0.4, 3.0);
    const int n = static_cast<int>(s.l.size());
    for (int pos = 0, k = 0; pos < n; pos += 256, ++k) {
        if (k % 40 == 20) st.setParameter(ParamId::SubPhaseMode, (k / 40) % 2 ? 0.0 : 1.0, false);
        double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
        st.process(ch, std::min(256, n - pos));
    }
    // Pente max d'une somme de sinus 150 + 210 Hz (|a| <= 0.4 chacun) :
    // 2π·(150 + 210)·0.4 / fs ≈ 0.019 par échantillon (+ marge de phase).
    double maxStep = 0.0;
    for (int i = st.latencySamples() + 4800; i < n; ++i)
        maxStep = std::max(maxStep, std::fabs(s.l[static_cast<std::size_t>(i)] - s.l[static_cast<std::size_t>(i - 1)]));
    OMNIS_CHECK(maxStep < 0.025);
}
