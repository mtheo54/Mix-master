// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestDrumBus.cpp — Reconnaître les éléments d'un bus de drums.
// =============================================================================
#include "TestFramework.h"

#include "stems/DrumBus.h"
#include "stems/StemSettings.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <vector>

using namespace omnis::stems;

namespace {

constexpr double kFs = 48000.0;

struct Bus {
    std::vector<double> l, r;
};

/// Un bus de drums synthétique, 140 BPM, `bars` mesures : on choisit qui joue.
Bus makeBus(int bars, bool kick, bool snare, bool hats, bool openHat) {
    const double beat = 60.0 / 140.0;
    const int n = static_cast<int>(bars * 4 * beat * kFs);
    Bus b{std::vector<double>(static_cast<std::size_t>(n), 0.0), std::vector<double>(static_cast<std::size_t>(n), 0.0)};
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    const auto add = [&](double t0, double seconds, auto&& f) {
        const int s0 = static_cast<int>(t0 * kFs), len = static_cast<int>(seconds * kFs);
        for (int i = 0; i < len && s0 + i < n; ++i) {
            const double v = f(i / kFs);
            b.l[static_cast<std::size_t>(s0 + i)] += v;
            b.r[static_cast<std::size_t>(s0 + i)] += v;
        }
    };
    double hpPrev = 0.0;
    for (int bar = 0; bar < bars; ++bar)
        for (int q = 0; q < 8; ++q) {
            const double t = (bar * 4 + q * 0.5) * beat;
            if (kick && (q == 0 || q == 5))
                add(t, 0.4, [&](double s) {
                    const double f = 50.0 + 100.0 * std::exp(-s / 0.03);
                    return 0.6 * std::exp(-s / 0.15) * std::sin(2.0 * std::numbers::pi * f * s);
                });
            if (snare && (q == 2 || q == 6))
                add(t, 0.3, [&](double s) {
                    return 0.35 * std::exp(-s / 0.1) * u(rng) + 0.3 * std::exp(-s / 0.06) * std::sin(2.0 * std::numbers::pi * 200.0 * s);
                });
            if (hats && q != 3 && q != 7)
                add(t, 0.1, [&](double s) {
                    const double x = u(rng), y = x - hpPrev; // aigu : différence d'un bruit
                    hpPrev = x;
                    return 0.12 * std::exp(-s / 0.02) * y;
                });
            if (openHat && q == 7)
                add(t, 0.6, [&](double s) {
                    const double x = u(rng), y = x - hpPrev;
                    hpPrev = x;
                    return 0.1 * std::exp(-s / 0.25) * y;
                });
        }
    return b;
}

DrumContent analyse(const Bus& b) {
    DrumBusAnalyzer a;
    a.prepare(kFs);
    for (std::size_t pos = 0; pos < b.l.size(); pos += 1024) {
        const int n = static_cast<int>(std::min<std::size_t>(1024, b.l.size() - pos));
        a.push(b.l.data() + pos, b.r.data() + pos, n);
    }
    return a.content();
}

} // namespace

OMNIS_TEST("bus de drums: kick, snare, hi-hat et open hat reconnus dans un bus complet") {
    const auto c = analyse(makeBus(8, true, true, true, true));
    std::printf("    coups : kick %d, snare %d, hat %d, open hat %d\n", c.kick.hits, c.snare.hits, c.hat.hits, c.openHat.hits);
    OMNIS_CHECK(c.kick.present && c.snare.present && c.hat.present && c.openHat.present);
    // 8 mesures : 16 kicks, 16 snares, 48 hats, 8 open hats (à quelques coups près).
    OMNIS_CHECK(std::abs(c.kick.hits - 16) <= 3);
    OMNIS_CHECK(c.snare.hits >= 12 && c.snare.hits <= 24); // un coup de hat sur la snare compte avec elle
    for (const auto& line : describeDrumBus(c)) std::printf("    %s\n", line.c_str());
}

OMNIS_TEST("bus de drums: ce qui ne joue pas n'est pas inventé (kick seul, hats seuls)") {
    const auto kickOnly = analyse(makeBus(8, true, false, false, false));
    std::printf("    kick seul : kick %d, snare %d, hat %d, open hat %d\n", kickOnly.kick.hits, kickOnly.snare.hits, kickOnly.hat.hits, kickOnly.openHat.hits);
    OMNIS_CHECK(kickOnly.kick.present);
    OMNIS_CHECK(!kickOnly.snare.present && !kickOnly.hat.present && !kickOnly.openHat.present);
    const auto hatsOnly = analyse(makeBus(8, false, false, true, false));
    OMNIS_CHECK(hatsOnly.hat.present && !hatsOnly.openHat.present);
    OMNIS_CHECK(!hatsOnly.kick.present && !hatsOnly.snare.present);
}

OMNIS_TEST("bus de drums: la chaîne du bus suit ce qui est reconnu (snare plus large, hats calmés)") {
    DrumContent c;
    c.kick = {16, true};
    c.snare = {16, true};
    c.hat = {40, true};
    Character ch;
    ch.crestDb = 14.0;
    ch.sideToMidDb = -20.0;
    const auto plain = settingsFor(StemMode::Auto, Element::DrumLoop, ch, 10.0, {});
    const auto withDrums = settingsFor(StemMode::Auto, Element::DrumLoop, ch, 10.0, {}, &c);
    OMNIS_CHECK(withDrums.monoBelowHz >= 150.0 && withDrums.monoAmount >= 1.0); // kick mono
    OMNIS_CHECK(withDrums.width > plain.width);                                 // snare plus large
    OMNIS_CHECK(withDrums.dynamic[2].maxCutDb > 0.0 && std::fabs(withDrums.dynamic[2].hz - 7000.0) < 1.0);
}
