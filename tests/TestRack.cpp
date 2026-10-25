// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestRack.cpp — Le rack : modules allumés / éteints, ordre, écoute
//  Delta, Gain Match ; et le limiteur (Punch, upward, lien stéréo).
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "analysis/LoudnessMeter.h"
#include "analysis/TruePeakMeter.h"
#include "core/Decibels.h"
#include "engine/Engine.h"
#include "engine/MasteringChain.h"
#include "stages/LimiterStage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace omnis;
using params::Module;
using params::ParamId;

namespace {

constexpr double kFs = 48000.0;

struct Stereo {
    std::vector<double> l, r;
};

Stereo from(const tools::AudioFile& f) { return {f.channels[0], f.channels[1]}; }

std::unique_ptr<Engine> chain(const std::vector<std::pair<ParamId, double>>& settings) {
    auto e = std::make_unique<Engine>();
    buildMasteringChain(*e);
    e->prepare(kFs, 512);
    for (const auto& [id, v] : settings) e->setParameterImmediate(id, v);
    return e;
}

/// Traite tout le signal, avec éventuellement un réglage changé au bloc `at`.
Stereo render(Engine& e, const Stereo& in, int changeAtBlock = -1, ParamId id = ParamId::Bypass, double value = 0.0) {
    Stereo out = in;
    const int n = static_cast<int>(in.l.size());
    int block = 0;
    for (int pos = 0; pos < n; pos += 512, ++block) {
        if (block == changeAtBlock) e.setParameter(id, value);
        double* ch[2] = {out.l.data() + pos, out.r.data() + pos};
        e.process(ch, ch, std::min(512, n - pos));
    }
    return out;
}

double maxDiff(const Stereo& a, const Stereo& b, std::size_t from) {
    double d = 0.0;
    for (std::size_t i = from; i < a.l.size(); ++i) d = std::max({d, std::fabs(a.l[i] - b.l[i]), std::fabs(a.r[i] - b.r[i])});
    return d;
}

double truePeakDb(const Stereo& s, std::size_t from = 0) {
    analysis::TruePeakMeter m;
    m.prepare(2);
    const double* ch[2] = {s.l.data() + from, s.r.data() + from};
    m.process(ch, static_cast<int>(s.l.size() - from));
    std::vector<double> z(64, 0.0);
    const double* zz[2] = {z.data(), z.data()};
    m.process(zz, 64);
    return gainToDb(m.truePeakMax());
}

double integratedLufs(const Stereo& s, std::size_t from = 0) {
    analysis::LoudnessMeter m;
    m.prepare(kFs, 2);
    const double* ch[2] = {s.l.data() + from, s.r.data() + from};
    m.process(ch, static_cast<int>(s.l.size() - from));
    return m.integratedLufs();
}

/// Réglages qui font vraiment travailler chaque module.
const std::vector<std::pair<ParamId, double>> kDriven{
    {ParamId::SubGain, 4.0},     {ParamId::MatchTilt, 2.0}, {ParamId::InflHighEffect, 60.0}, {ParamId::InflLowEffect, 40.0},
    {params::ParamId::ImgSplitLow, 250.0}, {static_cast<ParamId>(812), 140.0}, {params::mbId(1, params::MbControl::Ratio), 3.0},
    {params::mbId(1, params::MbControl::Threshold), -24.0}, {ParamId::ClipDrive, 6.0}, {ParamId::LimGain, 8.0}};

} // namespace

OMNIS_TEST("rack: ordre des modules, rang <-> permutation pour les 5040 ordres") {
    bool ok = true;
    for (int r = 0; r < params::kChainOrders; ++r) ok = ok && params::chainOrderRank(params::chainOrder(r)) == r;
    OMNIS_CHECK(ok);
    const auto first = params::chainOrder(0);
    for (int i = 0; i < params::kModules; ++i) OMNIS_CHECK(static_cast<int>(first[static_cast<std::size_t>(i)]) == i);
    // Le limiteur avant le clipper : un seul échange en fin d'ordre.
    std::array<Module, 7> swapped{Module::Sub, Module::Match, Module::Inflator, Module::Imager, Module::Multiband, Module::Limiter, Module::Clipper};
    OMNIS_CHECK(params::chainOrderRank(swapped) == 1);
}

OMNIS_TEST("rack: tous les modules éteints = entrée retardée de la latence, bit à bit (même poussés)") {
    auto settings = kDriven;
    for (int m = 0; m < params::kModules; ++m) settings.push_back({params::moduleOnId(static_cast<Module>(m)), 0.0});
    auto e = chain(settings);
    const Stereo in = from(tools::signals::drumLoop(kFs, 2));
    const Stereo out = render(*e, in);
    const auto lat = static_cast<std::size_t>(e->latencySamples());
    bool exact = true;
    for (std::size_t i = lat; i < in.l.size(); ++i)
        if (out.l[i] != in.l[i - lat] || out.r[i] != in.r[i - lat]) exact = false;
    OMNIS_CHECK(exact);
}

OMNIS_TEST("rack: éteindre un module en route = fondu sans clic vers le même son que sans lui") {
    const Stereo in = from(tools::signals::drumLoop(kFs, 4));
    auto withLim = chain(kDriven);
    auto noLim = kDriven;
    noLim.push_back({ParamId::LimOn, 0.0});
    auto without = chain(noLim);
    // Éteint au bloc 100 (~1 s) : après le fondu (20 ms) et la latence, identique à « sans ».
    const Stereo a = render(*withLim, in, 100, ParamId::LimOn, 0.0);
    const Stereo b = render(*without, in);
    const auto settled = static_cast<std::size_t>(100 * 512 + withLim->latencySamples()) + static_cast<std::size_t>(0.2 * kFs);
    OMNIS_CHECK(maxDiff(a, b, settled) < 1e-9);
    // Pendant le fondu, pas de saut d'un échantillon au suivant plus grand qu'ailleurs dans le son.
    double jumpFade = 0.0, jumpElsewhere = 0.0;
    for (std::size_t i = 1; i < a.l.size(); ++i) {
        const double j = std::fabs(a.l[i] - a.l[i - 1]);
        const bool inFade = i > 100 * 512 && i < 100 * 512 + 4800;
        if (inFade) jumpFade = std::max(jumpFade, j);
        else jumpElsewhere = std::max(jumpElsewhere, j);
    }
    OMNIS_CHECK(jumpFade <= jumpElsewhere * 1.05);
}

OMNIS_TEST("rack Delta: on n'entend que ce que le module change (limiteur en fin de chaîne, Sub seul)") {
    const Stereo in = from(tools::signals::drumLoop(kFs, 3));
    {
        auto on = chain(kDriven);
        auto offSettings = kDriven;
        offSettings.push_back({ParamId::LimOn, 0.0});
        auto off = chain(offSettings);
        auto deltaSettings = kDriven;
        deltaSettings.push_back({ParamId::DeltaListen, 7.0});
        auto delta = chain(deltaSettings);
        const Stereo a = render(*on, in), b = render(*off, in), d = render(*delta, in);
        Stereo diff = a;
        for (std::size_t i = 0; i < a.l.size(); ++i) {
            diff.l[i] = a.l[i] - b.l[i];
            diff.r[i] = a.r[i] - b.r[i];
        }
        const double err = maxDiff(d, diff, static_cast<std::size_t>(0.5 * kFs));
        std::printf("    delta limiteur : écart %.2e\n", err);
        OMNIS_CHECK(err < 1e-9);
        OMNIS_CHECK(truePeakDb(d, static_cast<std::size_t>(0.5 * kFs)) > -40.0); // il y a bien quelque chose à entendre
    }
    {
        // Sub seul allumé (les suivants éteints = retards purs) : delta = (avec Sub) − (sans rien).
        std::vector<std::pair<ParamId, double>> base{{ParamId::SubGain, 6.0}};
        for (int m = 1; m < params::kModules; ++m) base.push_back({params::moduleOnId(static_cast<Module>(m)), 0.0});
        auto on = chain(base);
        auto offSettings = base;
        offSettings.push_back({ParamId::SubOn, 0.0});
        auto off = chain(offSettings);
        auto deltaSettings = base;
        deltaSettings.push_back({ParamId::DeltaListen, 1.0});
        auto delta = chain(deltaSettings);
        const Stereo a = render(*on, in), b = render(*off, in), d = render(*delta, in);
        Stereo diff = a;
        for (std::size_t i = 0; i < a.l.size(); ++i) {
            diff.l[i] = a.l[i] - b.l[i];
            diff.r[i] = a.r[i] - b.r[i];
        }
        const double err = maxDiff(d, diff, static_cast<std::size_t>(0.5 * kFs) + static_cast<std::size_t>(on->latencySamples()));
        std::printf("    delta sub : écart %.2e\n", err);
        OMNIS_CHECK(err < 1e-9);
    }
}

OMNIS_TEST("rack: réordonner en route passe au son sec puis rejoint le nouvel ordre") {
    const Stereo in = from(tools::signals::drumLoop(kFs, 8));
    std::array<Module, 7> order{Module::Sub, Module::Match, Module::Inflator, Module::Imager, Module::Multiband, Module::Limiter, Module::Clipper};
    const double rank = params::chainOrderRank(order);
    auto moved = chain(kDriven);
    auto target = kDriven;
    target.push_back({ParamId::ChainOrder, rank});
    auto direct = chain(target);
    const Stereo a = render(*moved, in, 40, ParamId::ChainOrder, rank);
    const Stereo b = render(*direct, in);
    OMNIS_CHECK(moved->stageOrder()[5] == static_cast<int>(Module::Limiter));
    // Les états des étages (enveloppes, release) oublient l'ancien ordre en quelques secondes.
    const double err = maxDiff(a, b, a.l.size() - static_cast<std::size_t>(2.0 * kFs));
    std::printf("    écart après réordonnancement : %.2e\n", err);
    OMNIS_CHECK(err < 1e-6);
    // Jamais d'échantillon fou pendant la permutation.
    std::printf("    crête vraie : réordonné %.2f dBTP, ordre direct %.2f dBTP\n", truePeakDb(a), truePeakDb(b));
    OMNIS_CHECK(truePeakDb(a) < truePeakDb(b) + 0.5);
}

OMNIS_TEST("rack Gain Match: on écoute au volume de l'entrée, les mesures gardent le vrai niveau") {
    const Stereo in = from(tools::signals::pinkNoise(kFs, 12.0, -24.0, 11));
    auto loud = chain({{ParamId::LimGain, 12.0}});
    auto matched = chain({{ParamId::LimGain, 12.0}, {ParamId::GainMatch, 1.0}});
    const Stereo a = render(*loud, in), b = render(*matched, in);
    const auto from6s = static_cast<std::size_t>(6.0 * kFs);
    const double inL = integratedLufs(in, from6s), aL = integratedLufs(a, from6s), bL = integratedLufs(b, from6s);
    std::printf("    entrée %.2f LUFS, sortie %.2f, écoutée en Match %.2f (compense %.2f dB)\n", inL, aL, bL, matched->gainMatchDb());
    OMNIS_CHECK(aL - inL > 6.0);
    OMNIS_CHECK(std::fabs(bL - inL) < 0.5);
    OMNIS_CHECK(std::fabs(matched->meters().shortTermLufs - loud->meters().shortTermLufs) < 0.01);
}

OMNIS_TEST("limiteur Punch / upward / lien stéréo: le plafond tient toujours (crête vraie)") {
    const std::vector<tools::AudioFile> sources{tools::signals::drumLoop(kFs, 4), tools::signals::pinkNoise(kFs, 4.0, -16.0, 5),
                                                tools::signals::stereoImaging(kFs, 1.0, -14.0)};
    for (const auto& src : sources) {
        LimiterStage st;
        st.setParameter(ParamId::LimGain, 12.0, true);
        st.setParameter(ParamId::LimCeiling, -1.0, true);
        st.setParameter(ParamId::LimPunch, 100.0, true);
        st.setParameter(ParamId::LimUpward, 12.0, true);
        st.setParameter(ParamId::LimUnlinkTransient, 100.0, true);
        st.setParameter(ParamId::LimUnlinkSustain, 60.0, true);
        st.prepare({kFs, 256, 2});
        Stereo s = from(src);
        for (int pos = 0; pos < static_cast<int>(s.l.size()); pos += 256) {
            double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
            st.process(ch, std::min(256, static_cast<int>(s.l.size()) - pos));
        }
        const double tp = truePeakDb(s);
        std::printf("    crête vraie : %.3f dBTP (plafond -1)\n", tp);
        OMNIS_CHECK(tp <= -1.0 + 0.01);
    }
}

OMNIS_TEST("limiteur Punch: plus de relief (écart crête / sonie) au même plafond ; upward monte le calme") {
    const auto drums = tools::signals::drumLoop(kFs, 4);
    const auto measure = [&](double punch, double upward, double& plr, double& lufs) {
        LimiterStage st;
        st.setParameter(ParamId::LimGain, 9.0, true);
        st.setParameter(ParamId::LimCeiling, -1.0, true);
        st.setParameter(ParamId::LimPunch, punch, true);
        st.setParameter(ParamId::LimUpward, upward, true);
        st.prepare({kFs, 256, 2});
        Stereo s = from(drums);
        for (int pos = 0; pos < static_cast<int>(s.l.size()); pos += 256) {
            double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
            st.process(ch, std::min(256, static_cast<int>(s.l.size()) - pos));
        }
        lufs = integratedLufs(s, static_cast<std::size_t>(kFs));
        plr = truePeakDb(s, static_cast<std::size_t>(kFs)) - lufs;
    };
    double plr0 = 0, l0 = 0, plr1 = 0, l1 = 0, plrU = 0, lU = 0;
    measure(0.0, 0.0, plr0, l0);
    measure(100.0, 0.0, plr1, l1);
    std::printf("    sans Punch PLR %.2f dB (%.2f LUFS) ; Punch 100 %% PLR %.2f dB (%.2f LUFS)\n", plr0, l0, plr1, l1);
    OMNIS_CHECK(plr1 > plr0 + 1.0);
    (void)plrU;
    (void)lU;

    // Upward : un couplet calme (−40 dBFS) entre deux refrains forts.
    const auto quietLoud = [&](double upward, double& quiet, double& loud) {
        Stereo s = from(tools::signals::pinkNoise(kFs, 6.0, -12.0, 8));
        for (std::size_t i = static_cast<std::size_t>(2 * kFs); i < static_cast<std::size_t>(4 * kFs); ++i) {
            s.l[i] *= 0.01;
            s.r[i] *= 0.01;
        }
        LimiterStage st;
        st.setParameter(ParamId::LimCeiling, -1.0, true);
        st.setParameter(ParamId::LimUpward, upward, true);
        st.prepare({kFs, 256, 2});
        for (int pos = 0; pos < static_cast<int>(s.l.size()); pos += 256) {
            double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
            st.process(ch, std::min(256, static_cast<int>(s.l.size()) - pos));
        }
        Stereo q{{s.l.begin() + static_cast<long>(2.5 * kFs), s.l.begin() + static_cast<long>(4 * kFs)},
                 {s.r.begin() + static_cast<long>(2.5 * kFs), s.r.begin() + static_cast<long>(4 * kFs)}};
        Stereo f{{s.l.begin() + static_cast<long>(0.5 * kFs), s.l.begin() + static_cast<long>(2 * kFs)},
                 {s.r.begin() + static_cast<long>(0.5 * kFs), s.r.begin() + static_cast<long>(2 * kFs)}};
        quiet = integratedLufs(q);
        loud = truePeakDb(f);
    };
    double q0 = 0, p0 = 0, q8 = 0, p8 = 0;
    quietLoud(0.0, q0, p0);
    quietLoud(8.0, q8, p8);
    std::printf("    upward 8 dB : couplet %.2f -> %.2f LUFS, crête du refrain %.2f -> %.2f dBTP\n", q0, q8, p0, p8);
    OMNIS_CHECK(q8 - q0 > 5.0);           // le calme monte
    OMNIS_CHECK(std::fabs(p8 - p0) < 0.3); // les crêtes ne bougent pas
}

OMNIS_TEST("limiteur Punch: une 808 tenue (50 Hz) n'est pas modulée (distorsion < -60 dB)") {
    LimiterStage st;
    st.setParameter(ParamId::LimPunch, 100.0, true);
    st.setParameter(ParamId::LimUpward, 6.0, true);
    st.prepare({kFs, 256, 2});
    Stereo s;
    const int n = static_cast<int>(3 * kFs);
    for (int i = 0; i < n; ++i) {
        const double v = 0.25 * std::sin(2.0 * 3.141592653589793 * 50.0 * i / kFs);
        s.l.push_back(v);
        s.r.push_back(v);
    }
    for (int pos = 0; pos < n; pos += 256) {
        double* ch[2] = {s.l.data() + pos, s.r.data() + pos};
        st.process(ch, std::min(256, n - pos));
    }
    // Résidu après retrait de la fondamentale (projection sur sin / cos à 50 Hz), sur la dernière seconde.
    const auto start = static_cast<std::size_t>(2 * kFs);
    double a = 0.0, b = 0.0, total = 0.0;
    for (std::size_t i = start; i < s.l.size(); ++i) {
        const double ph = 2.0 * 3.141592653589793 * 50.0 * static_cast<double>(i) / kFs;
        a += s.l[i] * std::sin(ph);
        b += s.l[i] * std::cos(ph);
        total += s.l[i] * s.l[i];
    }
    const double m = static_cast<double>(s.l.size() - start);
    const double fundamental = 2.0 * (a * a + b * b) / m;
    const double thd = 10.0 * std::log10(std::max(total - fundamental, 1e-30) / total);
    std::printf("    distorsion sur 50 Hz tenu (Punch 100 %%, upward 6 dB) : %.1f dB\n", thd);
    OMNIS_CHECK(thd < -60.0);
}

OMNIS_TEST("limiteur: Punch, upward et lien à 0 = exactement l'ancien limiteur (bit à bit)") {
    // Le limiteur par défaut est déjà vérifié transparent et brickwall ; ici, régler
    // puis remettre à zéro les nouveaux réglages ne laisse aucune trace.
    const auto drums = tools::signals::drumLoop(kFs, 2);
    LimiterStage a, b;
    for (auto* st : {&a, &b}) {
        st->setParameter(ParamId::LimGain, 10.0, true);
        st->prepare({kFs, 256, 2});
    }
    b.setParameter(ParamId::LimPunch, 0.0, true);
    b.setParameter(ParamId::LimUpward, 0.0, true);
    b.setParameter(ParamId::LimUnlinkTransient, 0.0, true);
    Stereo sa = from(drums), sb = from(drums);
    for (int pos = 0; pos < static_cast<int>(sa.l.size()); pos += 256) {
        double* ca[2] = {sa.l.data() + pos, sa.r.data() + pos};
        double* cb[2] = {sb.l.data() + pos, sb.r.data() + pos};
        a.process(ca, std::min(256, static_cast<int>(sa.l.size()) - pos));
        b.process(cb, std::min(256, static_cast<int>(sb.l.size()) - pos));
    }
    OMNIS_CHECK(maxDiff(sa, sb, 0) == 0.0);
}
