// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestConductor.cpp — Le master chef d'orchestre : écoute alignée sur
//  le temps du projet, masquages, propositions, retouches appliquées.
// =============================================================================
#include "TestFramework.h"

#include "stems/Conductor.h"
#include "stems/StemChain.h"
#include "stems/StemTimeline.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <vector>

using namespace omnis::stems;

namespace {

constexpr double kFs = 48000.0;
constexpr double kPi = std::numbers::pi;

struct Stereo {
    std::vector<double> l, r;
    explicit Stereo(double seconds) : l(static_cast<std::size_t>(seconds * kFs)), r(l.size()) {}
    void add(std::size_t i, double v) {
        if (i >= l.size()) return;
        l[i] += v;
        r[i] += v;
    }
};

/// 808 : une note de 0,9 s par seconde, fondamentale 45 Hz, un peu saturée.
Stereo make808(double seconds, double every = 1.0, double from = 0.0) {
    Stereo s(seconds);
    for (double t0 = from; t0 < seconds; t0 += every) {
        double phase = 0.0;
        for (int n = 0; n < static_cast<int>(0.9 * kFs); ++n) {
            const double t = n / kFs;
            phase += 2 * kPi * 45.0 / kFs;
            const double env = std::min(1.0, t / 0.005) * std::exp(-t / 1.2);
            s.add(static_cast<std::size_t>(t0 * kFs) + static_cast<std::size_t>(n), 0.7 * std::tanh(1.5 * std::sin(phase)) * env);
        }
    }
    return s;
}

/// Piano : accords tenus ; main gauche (fondamentales 98 / 110 Hz, force `left`) si `bassHand`.
Stereo makePiano(double seconds, bool bassHand, double level = 0.25, double onFor = 1e9, double period = 1e9, double offset = 0.0,
                 double left = 1.2) {
    Stereo s(seconds);
    const double lows[] = {98.0, 110.0}, highs[] = {392.0, 493.9, 587.3};
    for (std::size_t i = 0; i < s.l.size(); ++i) {
        const double t = static_cast<double>(i) / kFs;
        if (std::fmod(t + offset, period) >= onFor) continue;
        double v = 0.0;
        for (const double f : highs) v += 0.3 * std::sin(2 * kPi * f * t) + 0.1 * std::sin(4 * kPi * f * t);
        if (bassHand) {
            const double f = lows[static_cast<int>(t) % 2];
            v += left * std::sin(2 * kPi * f * t) + 0.4 * left / 1.2 * std::sin(4 * kPi * f * t);
        }
        s.add(i, level * v);
    }
    return s;
}

StemTimeline timelineOf(const Stereo& s, std::int64_t start = 0) {
    StemTimeline t;
    t.prepare(kFs);
    // Par morceaux, comme le thread du lien.
    for (std::size_t i = 0; i < s.l.size(); i += 2400) {
        const int n = static_cast<int>(std::min<std::size_t>(2400, s.l.size() - i));
        t.push(s.l.data() + i, s.r.data() + i, n, start + static_cast<std::int64_t>(i));
    }
    t.flush();
    return t;
}

double levelAt(const std::vector<double>& x, double hz, std::size_t from, std::size_t to) {
    double re = 0.0, im = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        re += x[i] * std::cos(2 * kPi * hz * static_cast<double>(i) / kFs);
        im += x[i] * std::sin(2 * kPi * hz * static_cast<double>(i) / kFs);
    }
    return 20.0 * std::log10(std::max(2.0 * std::hypot(re, im) / static_cast<double>(to - from), 1e-15));
}

} // namespace

OMNIS_TEST("chef d'orchestre: frise rangée au temps du projet (morceaux, décalage, passages)") {
    const Stereo piano = makePiano(4.0, true);
    const StemTimeline a = timelineOf(piano, 0);
    // Le même son, entendu à partir de la position 3 × 8192 : les blocs sont décalés de 3.
    const StemTimeline b = timelineOf(piano, 3 * StemTimeline::kBlockFrames);
    OMNIS_CHECK(b.block(0) == nullptr && b.block(2) == nullptr);
    for (int k = 0; k < 20; ++k)
        for (int band = 0; band < kTimelineBands; ++band)
            OMNIS_CHECK_NEAR(b.bandPower(k + 3, band), a.bandPower(k, band), 1e-12 + 1e-9 * a.bandPower(k, band));
    // La zone grave du piano (98 / 110 Hz) tombe dans la bande 80-100 / 100-150.
    OMNIS_CHECK(a.bandPower(5, 3) + a.bandPower(5, 4) > 10.0 * a.bandPower(5, 1));
    // Un bloc rejoué (boucle) est moyenné, pas additionné.
    StemTimeline c = timelineOf(piano, 0);
    c.push(piano.l.data(), piano.r.data(), 4 * StemTimeline::kBlockFrames, 0);
    c.flush();
    OMNIS_CHECK(c.block(1)->passes == 2);
    OMNIS_CHECK_NEAR(c.bandPower(1, 4), a.bandPower(1, 4), 1e-9 * a.bandPower(1, 4));
}

OMNIS_TEST("chef d'orchestre: le piano qui couvre la 808 au même moment -> couper son grave (proposé)") {
    // Main gauche modérée : le grave n'est pas le cœur du piano -> passe-haut.
    const Stereo b808 = make808(12.0), piano = makePiano(12.0, true, 0.25, 1e9, 1e9, 0.0, 0.5);
    const StemTimeline t808 = timelineOf(b808), tPiano = timelineOf(piano);
    const std::vector<ConductorStem> stems{{11, Element::Bass808, "808", &t808}, {22, Element::Keys, "Piano", &tPiano}};
    const auto proposals = Conductor::propose(stems);
    for (const auto& p : proposals) std::printf("      %s\n", p.text.c_str());
    OMNIS_CHECK(proposals.size() == 1);
    if (proposals.empty()) return;
    const auto& p = proposals[0];
    OMNIS_CHECK(p.target == 1 && p.victim == 0 && p.kind == Proposal::Kind::LowMasking);
    OMNIS_CHECK(p.correction.type == Correction::Type::LowCut);
    OMNIS_CHECK(p.correction.hz >= 100.0f && p.correction.hz <= 200.0f);
    OMNIS_CHECK(p.text.find("« Piano »") != std::string::npos && p.text.find("« 808 »") != std::string::npos);
    OMNIS_CHECK(p.key == Conductor::keyFor(22, 11, Proposal::Kind::LowMasking));
    OMNIS_CHECK(p.key != Conductor::keyFor(11, 22, Proposal::Kind::LowMasking));

    // Main gauche dominante (le grave est le cœur du piano) : plateau −3 dB, pas de coupe.
    const StemTimeline heavy = timelineOf(makePiano(12.0, true));
    const auto shelf = Conductor::propose({{11, Element::Bass808, "808", &t808}, {22, Element::Keys, "Piano", &heavy}});
    for (const auto& q : shelf) std::printf("      %s\n", q.text.c_str());
    OMNIS_CHECK(shelf.size() == 1 && shelf[0].correction.type == Correction::Type::LowShelf && shelf[0].correction.gainDb == -3.0f);
}

OMNIS_TEST("chef d'orchestre: pas de fausse alerte (piano sans grave, ou qui joue quand la 808 se tait)") {
    const Stereo b808 = make808(16.0, 4.0, 0.0); // 808 de 0 à 0,9 s, puis 4 à 4,9 s…
    // Même piano grave, mais il joue de 1 à 3,9 s de chaque période de 4 s : jamais avec la 808.
    const Stereo alternate = makePiano(16.0, true, 0.25, 2.9, 4.0, -1.0 + 4.0);
    const Stereo light = makePiano(16.0, false);
    const StemTimeline t808 = timelineOf(b808), tAlt = timelineOf(alternate), tLight = timelineOf(light);
    OMNIS_CHECK(Conductor::propose({{1, Element::Bass808, "808", &t808}, {2, Element::Keys, "Piano", &tAlt}}).empty());
    const StemTimeline t808b = timelineOf(make808(12.0));
    OMNIS_CHECK(Conductor::propose({{1, Element::Bass808, "808", &t808b}, {2, Element::Keys, "Piano", &tLight}}).empty());
    // Deux victimes du grave ne se jugent pas entre elles (kick / 808 : autre affaire).
    const StemTimeline tBass = timelineOf(makePiano(12.0, true));
    OMNIS_CHECK(Conductor::propose({{1, Element::Bass808, "808", &t808b}, {2, Element::Bass, "Basse", &tBass}}).empty());
    // Un bus de drums (avec son kick) n'est jamais un gêneur du grave : on ne coupe pas le kick.
    OMNIS_CHECK(Conductor::propose({{1, Element::Bass808, "808", &t808b}, {2, Element::DrumLoop, "DRUMS", &tBass}}).empty());
}

OMNIS_TEST("chef d'orchestre: une guitare qui couvre la voix dans sa présence -> la creuser (proposé)") {
    Stereo voice(10.0), guitar(10.0);
    std::mt19937 rng(5);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    for (std::size_t i = 0; i < voice.l.size(); ++i) {
        const double t = static_cast<double>(i) / kFs;
        double v = 0.0;
        for (int k = 1; k <= 30; ++k) v += std::sin(2 * kPi * 160.0 * k * t) / k;
        voice.add(i, 0.2 * v);
        double g = 0.0;
        for (const double f : {1500.0, 2400.0, 3100.0, 4200.0}) g += std::sin(2 * kPi * f * t + f);
        guitar.add(i, 0.12 * g + 0.02 * u(rng));
    }
    const StemTimeline tv = timelineOf(voice), tg = timelineOf(guitar);
    const auto proposals = Conductor::propose({{1, Element::Vocal, "Voix", &tv}, {2, Element::Guitar, "Guitare", &tg}});
    for (const auto& p : proposals) std::printf("      %s\n", p.text.c_str());
    OMNIS_CHECK(proposals.size() == 1);
    if (proposals.empty()) return;
    OMNIS_CHECK(proposals[0].target == 1 && proposals[0].correction.type == Correction::Type::Bell);
    OMNIS_CHECK(proposals[0].correction.gainDb < 0.0f && proposals[0].correction.hz > 1200.0f && proposals[0].correction.hz < 6000.0f);
}

OMNIS_TEST("chef d'orchestre: la retouche acceptée s'applique au stem (même en Off), et s'efface proprement") {
    Stereo s = makePiano(3.0, true);
    const Stereo in = s;
    StemSettings set; // mode Off : rien d'autre
    set.corrections[0] = {Correction::Type::LowCut, 150.0f, 0.0f, 0.7071f};
    set.active = true;
    StemChain chain;
    chain.prepare(kFs);
    chain.setSettings(set);
    for (std::size_t i = 0; i < s.l.size(); i += 512)
        chain.process(s.l.data() + i, s.r.data() + i, static_cast<int>(std::min<std::size_t>(512, s.l.size() - i)));
    const std::size_t a = 96000, b = 144000;
    const double low = levelAt(s.l, 98.0, a, b) - levelAt(in.l, 98.0, a, b);
    const double high = levelAt(s.l, 587.3, a, b) - levelAt(in.l, 587.3, a, b);
    std::printf("      passe-haut 150 Hz : 98 Hz %.1f dB, 587 Hz %+.2f dB\n", low, high);
    OMNIS_CHECK(low < -3.0);
    OMNIS_CHECK(std::fabs(high) < 0.3);
    // Le Side n'est pas touché (M/S neutre : étage effacé).
    double side = 0.0;
    for (std::size_t i = a; i < b; ++i) side = std::max(side, std::fabs(s.l[i] - s.r[i]));
    OMNIS_CHECK(side < 1e-12);

    // Retouche retirée (annulée) en mode Off : fondu, puis au bit près.
    chain.setSettings(StemSettings{});
    Stereo t = makePiano(1.0, true);
    for (std::size_t i = 0; i < t.l.size(); i += 512) chain.process(t.l.data() + i, t.r.data() + i, 512);
    Stereo u = makePiano(1.0, true);
    const Stereo u0 = u;
    chain.process(u.l.data(), u.r.data(), 4800);
    OMNIS_CHECK(u.l == u0.l);

    // Bornes : une retouche folle reste dans les clous.
    const auto c = clampCorrection({Correction::Type::Bell, 1e9f, -40.0f, 100.0f});
    OMNIS_CHECK(c.hz <= 16000.0f && c.gainDb >= -6.0f && c.q <= 4.0f);
    OMNIS_CHECK(clampCorrection({Correction::Type::LowCut, std::nanf(""), 0.0f, 1.0f}).type == Correction::Type::None);
    OMNIS_CHECK(clampCorrection({Correction::Type::LowCut, 5000.0f, 0.0f, 1.0f}).hz <= 300.0f);
}
