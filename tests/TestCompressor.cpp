// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestCompressor.cpp — Compresseur de bus (stages/CompressorStage) :
//  courbe à genou continue, réduction statique exacte, rien sous le seuil (au
//  bit près), stéréo liée, passe-haut du détecteur (la 808 ne pompe pas),
//  attaque / relâchement, Opto selon le programme, aucun clic au changement
//  de caractère, Mix 0 % = entrée.
// =============================================================================
#include "TestFramework.h"

#include "stages/CompressorStage.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace omnis;
using params::ParamId;

namespace {

constexpr double kFs = 48000.0;

struct Stereo {
    std::vector<double> l, r;
};

Stereo sine(double hz, double ampL, double ampR, double seconds) {
    const auto n = static_cast<std::size_t>(seconds * kFs);
    Stereo s{std::vector<double>(n), std::vector<double>(n)};
    for (std::size_t i = 0; i < n; ++i) {
        const double v = std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kFs);
        s.l[i] = ampL * v;
        s.r[i] = ampR * v;
    }
    return s;
}

void run(CompressorStage& st, Stereo& s, int block = 256) {
    const int n = static_cast<int>(s.l.size());
    for (int pos = 0; pos < n; pos += block) {
        const int len = std::min(block, n - pos);
        double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
        st.process(ch, len);
    }
}

double peakDb(const std::vector<double>& x, std::size_t from, std::size_t to) {
    double p = 0.0;
    for (std::size_t i = from; i < to; ++i) p = std::max(p, std::fabs(x[i]));
    return 20.0 * std::log10(std::max(p, 1e-300));
}

CompressorStage make(int character, double threshold, double ratio, double attack = 10.0, double release = 200.0) {
    CompressorStage st;
    st.prepare({kFs, 256, 2});
    st.setParameter(ParamId::CompCharacter, character, true);
    st.setParameter(ParamId::CompThreshold, threshold, true);
    st.setParameter(ParamId::CompRatio, ratio, true);
    st.setParameter(ParamId::CompAttack, attack, true);
    st.setParameter(ParamId::CompRelease, release, true);
    st.setParameter(ParamId::CompScHpf, 20.0, true);
    return st;
}

} // namespace

OMNIS_TEST("compresseur: courbe à genou doux continue (0 sous le seuil, pente 1/R - 1 au-dessus)") {
    for (int c = 0; c < 4; ++c) {
        const double knee = CompressorCurve::kneeOf(c);
        OMNIS_CHECK(CompressorCurve::gainReductionDb(-40.0, -18.0, 4.0, knee) == 0.0);
        OMNIS_CHECK(std::fabs(CompressorCurve::gainReductionDb(-18.0 + 20.0, -18.0, 4.0, knee) - (-15.0)) < 1e-12);
        // Continuité aux bords du genou.
        for (double edge : {-18.0 - 0.5 * knee, -18.0 + 0.5 * knee}) {
            const double a = CompressorCurve::gainReductionDb(edge - 1e-9, -18.0, 4.0, knee);
            const double b = CompressorCurve::gainReductionDb(edge + 1e-9, -18.0, 4.0, knee);
            OMNIS_CHECK(std::fabs(a - b) < 1e-6);
        }
        // Monotone : plus fort -> plus de réduction.
        double prev = 0.0;
        for (double x = -40.0; x <= 0.0; x += 0.25) {
            const double g = CompressorCurve::gainReductionDb(x, -18.0, 4.0, knee);
            OMNIS_CHECK(g <= prev + 1e-12);
            prev = g;
        }
    }
    // Les genoux se distinguent : FET franc, Vari-mu large.
    OMNIS_CHECK(CompressorCurve::kneeOf(1) < CompressorCurve::kneeOf(2) && CompressorCurve::kneeOf(2) < CompressorCurve::kneeOf(0) &&
                CompressorCurve::kneeOf(0) < CompressorCurve::kneeOf(3));
}

OMNIS_TEST("compresseur: réduction statique exacte (sinus -6 dBFS, seuil -18, 4:1 -> -9 dB) ; sous le seuil, au bit près") {
    const char* names[] = {"Opto", "FET", "VCA", "Vari-mu"};
    for (int c = 0; c < 4; ++c) {
        auto st = make(c, -18.0, 4.0);
        Stereo s = sine(1000.0, 0.5, 0.5, 2.0);
        run(st, s);
        const auto n = s.l.size();
        const double out = peakDb(s.l, n - 24000, n);
        std::printf("      %-8s sortie %.2f dB crête (attendu -15,0), réduction affichée %.2f dB\n", names[c], out, st.gainReductionDb());
        OMNIS_CHECK(std::fabs(out - (-15.0)) < 0.6);
    }
    // Sous le seuil (-30 dBFS, seuil -18, genou le plus large 18 dB : -27 au plus bas) : entrée, au bit près.
    for (int c = 0; c < 4; ++c) {
        auto st = make(c, -10.0, 4.0);
        Stereo s = sine(1000.0, 0.03, 0.03, 0.5);
        const Stereo in = s;
        run(st, s);
        OMNIS_CHECK(s.l == in.l && s.r == in.r);
    }
}

OMNIS_TEST("compresseur: stéréo liée (même gain à gauche et à droite) ; Mix 0 % = entrée au bit près") {
    auto st = make(2, -24.0, 4.0);
    Stereo s = sine(500.0, 0.8, 0.1, 1.0);
    const Stereo in = s;
    run(st, s);
    for (std::size_t i = 24000; i < s.l.size(); i += 997)
        if (std::fabs(in.l[i]) > 0.1) OMNIS_CHECK(std::fabs(s.l[i] / in.l[i] - s.r[i] / in.r[i]) < 1e-12);
    auto dry = make(2, -40.0, 10.0);
    dry.setParameter(ParamId::CompMix, 0.0, true);
    Stereo t = sine(500.0, 0.8, 0.8, 0.5);
    const Stereo tin = t;
    run(dry, t);
    OMNIS_CHECK(t.l == tin.l);
}

OMNIS_TEST("compresseur: le passe-haut du détecteur empêche la 808 (40 Hz) de faire pomper") {
    const auto grWith = [](double scHz) {
        auto st = make(2, -24.0, 4.0);
        st.setParameter(ParamId::CompScHpf, scHz, true);
        Stereo s = sine(40.0, 0.5, 0.5, 2.0);
        run(st, s);
        return st.gainReductionDb();
    };
    const double open = grWith(20.0), filtered = grWith(150.0);
    std::printf("      808 à -6 dBFS : réduction %.1f dB sans filtre, %.1f dB avec passe-haut 150 Hz\n", open, filtered);
    OMNIS_CHECK(open < -8.0);
    OMNIS_CHECK(filtered > open + 6.0);
}

OMNIS_TEST("compresseur: attaque et relâchement (FET 1 ms rapide ; relâchement suit le réglage)") {
    // Saut de -40 à -6 dBFS, puis retour.
    auto st = make(1, -20.0, 10.0, 1.0, 100.0);
    Stereo quiet1 = sine(1000.0, 0.01, 0.01, 0.2), loud = sine(1000.0, 0.5, 0.5, 0.3), quiet2 = sine(1000.0, 0.01, 0.01, 0.5);
    run(st, quiet1);
    run(st, loud, 48); // blocs de 1 ms
    std::vector<double> grLoud;
    {
        auto st2 = make(1, -20.0, 10.0, 1.0, 100.0);
        Stereo a = sine(1000.0, 0.01, 0.01, 0.2), b = sine(1000.0, 0.5, 0.5, 0.05);
        run(st2, a);
        for (std::size_t pos = 0; pos + 48 <= b.l.size(); pos += 48) {
            double* ch[2] = {b.l.data() + pos, b.r.data() + pos};
            st2.process(ch, 48);
            grLoud.push_back(st2.gainReductionDb());
        }
    }
    // Réduction statique attendue : (1/10 - 1)·(-6 + 20) = -12,6 dB ; 90 % atteints en moins de 5 ms.
    OMNIS_CHECK(grLoud.size() > 5 && grLoud[4] < -0.9 * 12.6);
    // Relâchement : 100 ms -> après 0,5 s de calme, presque plus rien.
    run(st, quiet2);
    OMNIS_CHECK(st.gainReductionDb() > -0.5);
}

OMNIS_TEST("compresseur Opto: relâche vite après un coup bref, lentement après une compression longue") {
    const auto recovery = [](double burstSeconds) {
        auto st = make(0, -24.0, 4.0, 5.0, 300.0);
        Stereo burst = sine(1000.0, 0.5, 0.5, burstSeconds);
        run(st, burst, 48);
        // Temps pour revenir au-dessus de -1 dB de réduction, en silence relatif.
        Stereo calm = sine(1000.0, 0.005, 0.005, 3.0);
        int ms = 0;
        for (std::size_t pos = 0; pos + 48 <= calm.l.size(); pos += 48, ++ms) {
            double* ch[2] = {calm.l.data() + pos, calm.r.data() + pos};
            st.process(ch, 48);
            if (st.gainReductionDb() > -1.0) break;
        }
        return ms;
    };
    const int brief = recovery(0.08), sustained = recovery(3.0);
    std::printf("      retour à -1 dB : %d ms après 80 ms de coup, %d ms après 3 s de compression\n", brief, sustained);
    OMNIS_CHECK(sustained > 2 * brief);
}

OMNIS_TEST("compresseur: changer de caractère en lecture ne claque pas") {
    auto st = make(2, -24.0, 4.0);
    Stereo s = sine(220.0, 0.6, 0.6, 1.0);
    double maxStep = 0.0, prev = 0.0;
    for (std::size_t pos = 0, b = 0; pos + 256 <= s.l.size(); pos += 256, ++b) {
        if (b % 15 == 0) st.setParameter(ParamId::CompCharacter, static_cast<double>((b / 15) % 4), false);
        double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
        st.process(ch, 256);
        for (std::size_t i = pos; i < pos + 256; ++i) {
            if (i > 0) maxStep = std::max(maxStep, std::fabs(s.l[i] - prev));
            prev = s.l[i];
        }
    }
    // Un sinus de 220 Hz à 0,6 bouge d'au plus 0,017 par échantillon.
    std::printf("      plus grand saut : %.4f\n", maxStep);
    OMNIS_CHECK(maxStep < 0.02);
}
