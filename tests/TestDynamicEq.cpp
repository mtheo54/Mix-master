// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestDynamicEq.cpp — EQ dynamique du Match EQ (dsp/DynamicEq) : rien
//  quand la bande garde son équilibre habituel, une retenue bornée quand elle
//  sort du lot (sifflante, note de 808), indépendante du volume, Mid / Side
//  séparés, au bit près éteinte.
// =============================================================================
#include "TestFramework.h"

#include "dsp/DynamicEq.h"
#include "stages/MatchEqStage.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

using namespace omnis;
using params::DynControl;
using params::ParamId;

namespace {

constexpr double kFs = 48000.0;

/// Une bande allumée à `hz`, plage `range`, seuil `thr` dB au-dessus de l'habitude.
void band(dsp::DynamicEq& eq, int b, double hz, double range, double thr = 4.0, params::DynChannel ch = params::DynChannel::Stereo, double q = 2.0) {
    eq.setParameter(params::dynId(b, DynControl::Freq), hz);
    eq.setParameter(params::dynId(b, DynControl::Q), q);
    eq.setParameter(params::dynId(b, DynControl::Range), range);
    eq.setParameter(params::dynId(b, DynControl::Threshold), thr);
    eq.setParameter(params::dynId(b, DynControl::Channel), static_cast<double>(ch));
    eq.setParameter(params::dynId(b, DynControl::On), 1.0);
}

/// Bruit (le mix) + une sinusoïde à `toneHz` jouée de `burstFrom` à `burstTo`
/// secondes (la sifflante). Renvoie le gain le plus bas et le plus haut de la
/// bande 0 dans la salve et après elle.
struct Run {
    double minInBurst = 0.0, maxInBurst = -120.0, afterDb = 0.0, steadyDb = 0.0;
    bool finite = true;
};

Run play(dsp::DynamicEq& eq, double level, double toneHz, double toneAmp, double burstFrom, double burstTo, double seconds, bool inSide = false) {
    std::mt19937 rng(5);
    std::normal_distribution<double> n(0.0, 1.0);
    Run r;
    const int total = static_cast<int>(seconds * kFs);
    for (int i = 0; i < total; ++i) {
        const double t = i / kFs;
        const bool burst = t >= burstFrom && t < burstTo;
        const double tone = burst ? toneAmp * std::sin(2.0 * std::numbers::pi * toneHz * t) : 0.0;
        double mid = level * (0.1 * n(rng) + (inSide ? 0.0 : tone));
        double side = level * (0.03 * n(rng) + (inSide ? tone : 0.0));
        eq.process(mid, side);
        r.finite = r.finite && std::isfinite(mid) && std::isfinite(side);
        const double g = eq.gainDb(0);
        if (burst && t > burstFrom + 0.05) {
            r.minInBurst = std::min(r.minInBurst, g);
            r.maxInBurst = std::max(r.maxInBurst, g);
        }
        if (t < burstFrom && t > burstFrom - 0.5) r.steadyDb = std::max(r.steadyDb, std::fabs(g));
        if (t > burstTo + 1.0) r.afterDb = std::max(r.afterDb, std::fabs(g));
    }
    return r;
}

} // namespace

OMNIS_TEST("eq dynamique: rien tant que la bande garde son équilibre ; une sifflante de 7 kHz est retenue, bornée à la plage, puis relâchée") {
    dsp::DynamicEq eq;
    eq.prepare(kFs);
    band(eq, 0, 7000.0, -6.0);
    const Run r = play(eq, 1.0, 7000.0, 0.3, 6.0, 6.4, 9.0);
    std::printf("      avant : %.2f dB · salve : %.2f dB · après : %.2f dB\n", r.steadyDb, r.minInBurst, r.afterDb);
    OMNIS_CHECK(r.finite);
    OMNIS_CHECK(r.steadyDb < 0.5);   // le mix seul : la bande ne bouge pas
    OMNIS_CHECK(r.minInBurst < -4.0); // la sifflante : retenue franche
    OMNIS_CHECK(r.minInBurst >= -6.0 - 1e-9); // jamais au-delà de la plage
    OMNIS_CHECK(r.afterDb < 0.5);     // relâchée ensuite
}

OMNIS_TEST("eq dynamique: indépendante du volume (même prod 20 dB plus bas : même retenue à 0.3 dB près)") {
    dsp::DynamicEq a, b;
    a.prepare(kFs);
    b.prepare(kFs);
    band(a, 0, 7000.0, -8.0);
    band(b, 0, 7000.0, -8.0);
    const Run loud = play(a, 1.0, 7000.0, 0.2, 6.0, 6.4, 7.0);
    const Run soft = play(b, 0.1, 7000.0, 0.2, 6.0, 6.4, 7.0);
    std::printf("      fort : %.2f dB · 20 dB plus bas : %.2f dB\n", loud.minInBurst, soft.minInBurst);
    OMNIS_CHECK(loud.minInBurst < -2.0);
    OMNIS_CHECK_NEAR(loud.minInBurst, soft.minInBurst, 0.3);
}

OMNIS_TEST("eq dynamique: plage positive = la bande ressort (jusqu'à +4 dB, jamais plus)") {
    dsp::DynamicEq eq;
    eq.prepare(kFs);
    band(eq, 0, 200.0, 4.0);
    const Run r = play(eq, 1.0, 200.0, 0.5, 6.0, 6.5, 8.0);
    std::printf("      salve : jusqu'à %+.2f dB\n", r.maxInBurst);
    OMNIS_CHECK(r.maxInBurst > 2.0);
    OMNIS_CHECK(r.maxInBurst <= 4.0 + 1e-9);
}

OMNIS_TEST("eq dynamique: Side seulement — une salve au centre ne la déclenche pas, sur les côtés oui") {
    dsp::DynamicEq centre, sides;
    centre.prepare(kFs);
    sides.prepare(kFs);
    band(centre, 0, 5000.0, -6.0, 4.0, params::DynChannel::Side);
    band(sides, 0, 5000.0, -6.0, 4.0, params::DynChannel::Side);
    const Run a = play(centre, 1.0, 5000.0, 0.3, 6.0, 6.4, 7.0, false);
    const Run b = play(sides, 1.0, 5000.0, 0.3, 6.0, 6.4, 7.0, true);
    std::printf("      salve au centre : %.2f dB · sur les côtés : %.2f dB\n", a.minInBurst, b.minInBurst);
    OMNIS_CHECK(a.minInBurst > -0.5);
    OMNIS_CHECK(b.minInBurst < -3.0);
}

OMNIS_TEST("eq dynamique: éteinte, l'étage Match EQ reste au bit près ; éteinte en cours de route, elle revient à 0 dB puis ne calcule plus") {
    // Le Match EQ seul, puis avec une bande allumée puis éteinte : sortie identique à la fin.
    MatchEqStage plain, withDyn;
    plain.prepare({kFs, 256, 2});
    withDyn.prepare({kFs, 256, 2});
    for (const auto& spec : params::allParameters()) {
        const auto raw = static_cast<std::uint32_t>(spec.id);
        if (raw >= 272 && raw <= 297) {
            plain.setParameter(spec.id, spec.defaultValue, true);
            withDyn.setParameter(spec.id, spec.defaultValue, true);
        }
    }
    withDyn.setParameter(params::dynId(2, DynControl::On), 1.0, true);
    std::mt19937 rng(9);
    std::normal_distribution<double> n(0.0, 0.2);
    const int total = static_cast<int>(4.0 * kFs);
    std::vector<double> l(total), r(total);
    for (int i = 0; i < total; ++i) {
        l[static_cast<std::size_t>(i)] = n(rng) + (i > kFs && i < 1.3 * kFs ? 0.4 * std::sin(0.4 * i) : 0.0);
        r[static_cast<std::size_t>(i)] = n(rng);
    }
    std::vector<double> l2 = l, r2 = r;
    double maxDiffEnd = 0.0;
    for (int pos = 0; pos < total; pos += 256) {
        if (pos >= static_cast<int>(2.0 * kFs) && pos < static_cast<int>(2.0 * kFs) + 256) withDyn.setParameter(params::dynId(2, DynControl::On), 0.0, true);
        double* a[2] = {l.data() + pos, r.data() + pos};
        double* b[2] = {l2.data() + pos, r2.data() + pos};
        const int len = std::min(256, total - pos);
        plain.process(a, len);
        withDyn.process(b, len);
        if (pos > static_cast<int>(3.0 * kFs))
            for (int i = 0; i < len; ++i)
                maxDiffEnd = std::max({maxDiffEnd, std::fabs(a[0][i] - b[0][i]), std::fabs(a[1][i] - b[1][i])});
    }
    std::printf("      écart après extinction : %.3g\n", maxDiffEnd);
    OMNIS_CHECK(maxDiffEnd == 0.0);
    OMNIS_CHECK(std::fabs(withDyn.dynamicGainDb(2)) < 1e-9);
}

OMNIS_TEST("eq dynamique: réglages absurdes (NaN, hors bornes) et fréquence balayée : sortie finie, sans saut") {
    dsp::DynamicEq eq;
    eq.prepare(44100.0);
    band(eq, 0, 7000.0, -18.0, 0.0);
    band(eq, 1, 60.0, 12.0, 0.0, params::DynChannel::Mid, 8.0);
    eq.setParameter(params::dynId(2, DynControl::Freq), std::nan(""));
    eq.setParameter(params::dynId(2, DynControl::Q), 1e9);
    eq.setParameter(params::dynId(2, DynControl::On), 1.0);
    eq.setParameter(ParamId::MatchDynAttack, -5.0);
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    bool finite = true;
    double maxOut = 0.0;
    for (int i = 0; i < static_cast<int>(3 * 44100); ++i) {
        if (i % 4410 == 0) eq.setParameter(params::dynId(0, DynControl::Freq), 20.0 * std::pow(1000.0, (i / 4410 % 30) / 29.0));
        double m = u(rng), s = 0.5 * u(rng);
        eq.process(m, s);
        finite = finite && std::isfinite(m) && std::isfinite(s);
        maxOut = std::max({maxOut, std::fabs(m), std::fabs(s)});
    }
    OMNIS_CHECK(finite);
    OMNIS_CHECK(maxOut < 8.0); // +12 dB au plus sur une bande : borné
}
