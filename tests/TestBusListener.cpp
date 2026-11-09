// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestBusListener.cpp — Ce qu'OMNIS entend dans un bus (sans rien sur les pistes).
// =============================================================================
#include "TestFramework.h"

#include "stems/BusListener.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace omnis::stems;

namespace {

constexpr double kFs = 48000.0;
constexpr double kPi = std::numbers::pi;
using Mono = std::vector<double>;

/// Les parties d'une prod synthétique (140 BPM) : chaque bus de test en mélange quelques-unes.
struct Parts {
    Mono kick, snare, hats, openHat, bass808, sat808, pad, lead, reverse, riser, impact;
    explicit Parts(double seconds) {
        const std::size_t n = static_cast<std::size_t>(seconds * kFs);
        for (Mono* m : {&kick, &snare, &hats, &openHat, &bass808, &sat808, &pad, &lead, &reverse, &riser, &impact}) m->assign(n, 0.0);
        std::mt19937 rng(11);
        std::uniform_real_distribution<double> u(-1.0, 1.0);
        const double beat = 60.0 / 140.0, eighth = beat / 2.0;
        double prev = 0.0;
        for (int step = 0; step * eighth < seconds; ++step) {
            const auto at = static_cast<std::size_t>(step * eighth * kFs);
            const int s8 = step % 8;
            for (std::size_t k = 0; k < static_cast<std::size_t>(0.7 * kFs) && at + k < n; ++k) {
                const double s = static_cast<double>(k) / kFs;
                // Kick : corps grave qui chute de hauteur, et le clic de la batte (bruit très court).
                if ((s8 == 0 || s8 == 5) && s < 0.4)
                    kick[at + k] += 0.7 * std::exp(-s / 0.1) * std::sin(2 * kPi * (50.0 * s + 3.0 * (1.0 - std::exp(-s / 0.03)))) + 0.3 * std::exp(-s / 0.004) * u(rng);
                if ((s8 == 2 || s8 == 6) && s < 0.25)
                    snare[at + k] += 0.4 * std::exp(-s / 0.07) * u(rng) + 0.25 * std::exp(-s / 0.05) * std::sin(2 * kPi * 190.0 * s);
                if (s < 0.06 && s8 != 7) {
                    const double x = u(rng);
                    hats[at + k] += 0.2 * std::exp(-s / 0.015) * (x - prev);
                    prev = x;
                }
                if (s8 == 7 && s < 0.6) {
                    const double x = u(rng);
                    openHat[at + k] += 0.15 * std::exp(-s / 0.25) * (x - prev);
                    prev = x;
                }
                // 808 : une note longue sur les temps 1 et 6 (avec le kick), 49 / 55 Hz.
                if (s8 == 0 || s8 == 5) {
                    const double f0 = (step / 8) % 2 ? 55.0 : 49.0;
                    const double v = 0.5 * std::min(1.0, s / 0.004) * std::sin(2 * kPi * f0 * s) * std::exp(-s / 0.9);
                    bass808[at + k] += v;
                    sat808[at + k] += std::tanh(3.0 * v) / 1.5;
                }
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / kFs;
            for (double f : {220.0, 277.2, 329.6})
                for (int h = 1; h <= 3; ++h) pad[i] += 0.04 / h * std::sin(2 * kPi * f * h * t + h) * (0.8 + 0.2 * std::sin(2 * kPi * 0.25 * t));
            const double note = std::fmod(t, eighth);
            const double f = static_cast<int>(t / eighth) % 3 ? 660.0 : 990.0;
            for (int h = 1; h <= 4; ++h) lead[i] += 0.12 / h * std::exp(-note / 0.18) * std::min(1.0, note / 0.005) * std::sin(2 * kPi * f * h * t);
            // Reverse cymbal : monte sur 1,5 s, coupée net, toutes les 4 mesures.
            const double bar4 = 16 * beat, cycle = std::fmod(t, bar4);
            if (cycle >= bar4 - 1.5) {
                const double x = u(rng);
                reverse[i] = 0.35 * std::pow((cycle - (bar4 - 1.5)) / 1.5, 3.0) * (x - prev);
                prev = x;
            }
            // Riser : bruit dont le volume monte sur 4 s, toutes les 8 s, retombée rapide.
            const double rc = std::fmod(t, 8.0);
            if (rc < 4.0) riser[i] = 0.3 * std::pow(rc / 4.0, 2.0) * u(rng);
            else if (rc < 4.2) riser[i] = 0.3 * std::exp(-(rc - 4.0) / 0.04) * u(rng);
            // Impact : boom + souffle large, toutes les 6 s, qui traîne ~2 s.
            const double ic = std::fmod(t, 6.0);
            if (ic < 2.5) impact[i] = 0.6 * std::exp(-ic / 0.7) * std::sin(2 * kPi * (40.0 * ic + 2.0 * (1.0 - std::exp(-ic / 0.05)))) + 0.4 * std::exp(-ic / 0.8) * u(rng);
        }
    }
};

Mono mix(std::initializer_list<const Mono*> parts) {
    Mono out((*parts.begin())->size(), 0.0);
    for (const Mono* m : parts)
        for (std::size_t i = 0; i < out.size(); ++i) out[i] += (*m)[i];
    return out;
}

BusContent listen(const Mono& m) {
    BusListener l;
    l.prepare(kFs);
    for (std::size_t pos = 0; pos < m.size(); pos += 1024) {
        const int n = static_cast<int>(std::min<std::size_t>(1024, m.size() - pos));
        l.push(m.data() + pos, m.data() + pos, n);
    }
    const BusContent c = l.content();
    std::string s;
    for (const auto& i : c.items) s += (s.empty() ? "" : ", ") + describeItem(i);
    std::printf("      -> %s\n", s.empty() ? "(rien)" : s.c_str());
    return c;
}

bool only(const BusContent& c, std::initializer_list<Element> expected) {
    for (Element e : expected)
        if (!c.has(e)) return false;
    for (const auto& i : c.items)
        if (std::find(expected.begin(), expected.end(), i.element) == expected.end()) return false;
    return true;
}

const Parts& parts() {
    static const Parts p(20.0);
    return p;
}

} // namespace

OMNIS_TEST("bus: un bus DRUMS -> kick, snare / clap, hi-hat, open hat, et rien d'autre") {
    const auto& p = parts();
    OMNIS_CHECK(only(listen(mix({&p.kick, &p.snare, &p.hats, &p.openHat})), {Element::Kick, Element::Snare, Element::HiHat, Element::OpenHat}));
}

OMNIS_TEST("bus: 808 (propre ou saturée) entendue comme 808, pas comme du mélo ; avec le kick, les deux") {
    const auto& p = parts();
    OMNIS_CHECK(only(listen(p.bass808), {Element::Bass808}));
    OMNIS_CHECK(only(listen(p.sat808), {Element::Bass808}));
    const auto both = listen(mix({&p.kick, &p.bass808}));
    OMNIS_CHECK(both.has(Element::Kick) && both.has(Element::Bass808) && !both.has(Element::Pad) && !both.has(Element::Lead));
}

OMNIS_TEST("bus: MÉLO -> nappe (tenue) et lead / synthé (attaques), sans drums ni 808") {
    const auto& p = parts();
    OMNIS_CHECK(only(listen(p.pad), {Element::Pad}));
    OMNIS_CHECK(listen(p.lead).has(Element::Lead));
    const auto melo = listen(mix({&p.pad, &p.lead}));
    OMNIS_CHECK(melo.has(Element::Pad) && melo.has(Element::Lead) && !melo.has(Element::Kick) && !melo.has(Element::Bass808));
}

OMNIS_TEST("bus: EFX -> reverse cymbal, riser, impact reconnus ; les hats n'en font pas") {
    const auto& p = parts();
    OMNIS_CHECK(only(listen(p.reverse), {Element::ReverseCymbal}));
    OMNIS_CHECK(listen(p.riser).has(Element::Riser));
    OMNIS_CHECK(listen(p.impact).has(Element::Impact));
    const auto efx = listen(mix({&p.reverse, &p.riser}));
    OMNIS_CHECK(efx.has(Element::ReverseCymbal) && efx.has(Element::Riser));
    OMNIS_CHECK(!listen(mix({&p.hats, &p.openHat})).has(Element::ReverseCymbal));
}

OMNIS_TEST("bus: toute la prod dans un bus -> tous les groupes entendus") {
    const auto& p = parts();
    const auto all = listen(mix({&p.kick, &p.snare, &p.hats, &p.bass808, &p.pad, &p.lead, &p.reverse}));
    for (Element e : {Element::Kick, Element::Snare, Element::HiHat, Element::Bass808, Element::Pad, Element::Lead, Element::ReverseCymbal})
        OMNIS_CHECK(all.has(e));
}

OMNIS_TEST("bus: silence, bruit de fond, trop peu d'écoute -> rien d'inventé") {
    OMNIS_CHECK(listen(Mono(static_cast<std::size_t>(10 * kFs), 0.0)).items.empty());
    Mono hiss(static_cast<std::size_t>(10 * kFs));
    std::mt19937 rng(3);
    std::normal_distribution<double> g(0.0, 0.001);
    for (double& v : hiss) v = g(rng);
    OMNIS_CHECK(listen(hiss).items.empty());
    const auto& p = parts();
    OMNIS_CHECK(listen(Mono(p.kick.begin(), p.kick.begin() + static_cast<std::ptrdiff_t>(kFs))).items.empty()); // 1 s
}
