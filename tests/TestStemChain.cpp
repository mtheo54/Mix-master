// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestStemChain.cpp — La chaîne d'un stem : réglages par élément et son.
// =============================================================================
#include "TestFramework.h"

#include "stems/StemChain.h"
#include "stems/StemSettings.h"

#include <cmath>
#include <cstdio>
#include <limits>
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
};

/// Niveau (dB) d'une fréquence sur [from, to) (Goertzel, fenêtre de Hann).
double levelAt(const std::vector<double>& x, double hz, std::size_t from, std::size_t to) {
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i - from) / static_cast<double>(to - from));
        re += w * x[i] * std::cos(2 * kPi * hz * static_cast<double>(i) / kFs);
        im += w * x[i] * std::sin(2 * kPi * hz * static_cast<double>(i) / kFs);
        wsum += w;
    }
    return 20.0 * std::log10(std::max(2.0 * std::hypot(re, im) / wsum, 1e-15));
}

/// Énergie (dB) dans une bande, par un banc de Goertzel (pas de 5 Hz).
double bandDb(const std::vector<double>& x, double lo, double hi, std::size_t from, std::size_t to) {
    double p = 0.0;
    for (double f = lo; f <= hi; f += 5.0) p += std::pow(10.0, levelAt(x, f, from, to) / 10.0);
    return 10.0 * std::log10(p);
}

double rms(const std::vector<double>& x, std::size_t from, std::size_t to) {
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i) s += x[i] * x[i];
    return std::sqrt(s / static_cast<double>(to - from));
}

double peak(const std::vector<double>& x, std::size_t from, std::size_t to) {
    double m = 0.0;
    for (std::size_t i = from; i < to; ++i) m = std::max(m, std::fabs(x[i]));
    return m;
}

/// Bruit rose (filtre de Paul Kellet), mono sur les deux canaux.
Stereo pink(double seconds, double gain, unsigned seed) {
    Stereo s(seconds);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (std::size_t i = 0; i < s.l.size(); ++i) {
        const double w = u(rng);
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        const double v = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
        b6 = w * 0.115926;
        s.l[i] = s.r[i] = gain * v;
    }
    return s;
}

void run(StemChain& chain, Stereo& s, int block = 512) {
    for (std::size_t i = 0; i < s.l.size(); i += static_cast<std::size_t>(block)) {
        const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), s.l.size() - i));
        chain.process(s.l.data() + i, s.r.data() + i, n);
    }
}

StemSettings only() {
    StemSettings s;
    s.active = true;
    return s;
}

} // namespace

OMNIS_TEST("stem chaîne: Off laisse passer au bit près, aussi après avoir agi") {
    StemChain chain;
    chain.prepare(kFs);
    const Stereo in = pink(2.0, 0.3, 1);
    Stereo s = in;
    run(chain, s, 333);
    OMNIS_CHECK(s.l == in.l && s.r == in.r);

    // Actif (modèle kick), puis Off : après le fondu, de nouveau au bit près.
    chain.setSettings(settingsFor(StemMode::Auto, Element::Kick, {}, 0.0, {}));
    Stereo t = in;
    run(chain, t);
    OMNIS_CHECK(t.l != in.l);
    chain.setSettings(settingsFor(StemMode::Off, Element::Kick, {}, 0.0, {}));
    Stereo fade = in;
    run(chain, fade); // fondu de 30 ms
    OMNIS_CHECK(!chain.processing());
    Stereo after = in;
    run(chain, after, 100);
    OMNIS_CHECK(after.l == in.l && after.r == in.r);
}

OMNIS_TEST("stem chaîne: aucune latence (le son direct sort au même échantillon)") {
    for (const Element e : {Element::Kick, Element::Bass808, Element::Keys, Element::Vocal, Element::HiHat}) {
        StemChain chain;
        chain.prepare(kFs);
        chain.setSettings(settingsFor(StemMode::Auto, e, {}, 0.0, {}));
        Stereo s(0.1);
        // Le fondu d'entrée est déjà passé : on laisse tourner du silence.
        Stereo warm(0.2);
        run(chain, warm);
        s.l[1000] = s.r[1000] = 0.5;
        run(chain, s, 64);
        bool before = true;
        for (std::size_t i = 0; i < 1000; ++i) before = before && s.l[i] == 0.0 && s.r[i] == 0.0;
        OMNIS_CHECK(before);
        // La réponse démarre à l'échantillon même de l'impulsion.
        OMNIS_CHECK(std::fabs(s.l[1000]) > 0.15);
        OMNIS_CHECK(StemChain::latencySamples() == 0);
    }
}

OMNIS_TEST("stem chaîne: le passe-haut retire le grondement, garde le médium") {
    StemSettings set = only();
    set.hpfHz = 100.0;
    set.hpfSteep = true;
    StemChain chain;
    chain.prepare(kFs);
    chain.setSettings(set);
    Stereo s(2.0);
    for (std::size_t i = 0; i < s.l.size(); ++i) {
        const double t = static_cast<double>(i) / kFs;
        s.l[i] = s.r[i] = 0.3 * std::sin(2 * kPi * 15.0 * t) + 0.3 * std::sin(2 * kPi * 1000.0 * t);
    }
    const Stereo in = s;
    run(chain, s);
    const std::size_t a = 48000, b = 96000;
    const double rumble = levelAt(s.l, 15.0, a, b) - levelAt(in.l, 15.0, a, b);
    const double mids = levelAt(s.l, 1000.0, a, b) - levelAt(in.l, 1000.0, a, b);
    std::printf("      15 Hz : %.1f dB, 1 kHz : %+.2f dB\n", rumble, mids);
    OMNIS_CHECK(rumble < -45.0);
    OMNIS_CHECK(std::fabs(mids) < 0.1);
}

OMNIS_TEST("stem chaîne: grave en mono, largeur au-dessus, Mid intact") {
    StemSettings set = only();
    set.monoBelowHz = 150.0;
    set.monoAmount = 1.0;
    set.width = 1.2;
    StemChain chain;
    chain.prepare(kFs);
    chain.setSettings(set);
    Stereo s(2.0);
    for (std::size_t i = 0; i < s.l.size(); ++i) {
        const double t = static_cast<double>(i) / kFs;
        const double mid = 0.2 * std::sin(2 * kPi * 60.0 * t) + 0.2 * std::sin(2 * kPi * 1000.0 * t);
        const double side = 0.1 * std::sin(2 * kPi * 50.0 * t) + 0.1 * std::sin(2 * kPi * 3000.0 * t);
        s.l[i] = mid + side;
        s.r[i] = mid - side;
    }
    const Stereo in = s;
    run(chain, s);
    std::vector<double> sideIn(in.l.size()), sideOut(in.l.size()), midIn(in.l.size()), midOut(in.l.size());
    for (std::size_t i = 0; i < in.l.size(); ++i) {
        sideIn[i] = 0.5 * (in.l[i] - in.r[i]);
        sideOut[i] = 0.5 * (s.l[i] - s.r[i]);
        midIn[i] = 0.5 * (in.l[i] + in.r[i]);
        midOut[i] = 0.5 * (s.l[i] + s.r[i]);
    }
    const std::size_t a = 48000, b = 96000;
    const double low = levelAt(sideOut, 50.0, a, b) - levelAt(sideIn, 50.0, a, b);
    const double high = levelAt(sideOut, 3000.0, a, b) - levelAt(sideIn, 3000.0, a, b);
    std::printf("      Side 50 Hz : %.1f dB, Side 3 kHz : %+.2f dB\n", low, high);
    OMNIS_CHECK(low < -12.0);
    OMNIS_CHECK_NEAR(high, 20.0 * std::log10(1.2), 0.3);
    double midDiff = 0.0;
    for (std::size_t i = a; i < b; ++i) midDiff = std::max(midDiff, std::fabs(midOut[i] - midIn[i]));
    OMNIS_CHECK(midDiff < 1e-12);
}

OMNIS_TEST("stem chaîne: l'EQ dynamique calme une zone qui prend trop de place, pas un son équilibré") {
    DynamicBand mud{250.0, 1.0, 3.0, 6.0};
    StemSettings set = only();
    set.dynamic[0] = mud;

    // Bruit rose : rien à corriger.
    {
        StemChain chain;
        chain.prepare(kFs);
        chain.setSettings(set);
        Stereo s = pink(3.0, 0.2, 2);
        run(chain, s);
        std::printf("      bruit rose : coupe %.2f dB\n", chain.dynamicCutDb(0));
        OMNIS_CHECK(chain.dynamicCutDb(0) < 0.5);
    }
    // Bruit rose + une grosse bosse à 250 Hz (+15 dB) : coupée, le reste intact.
    StemChain chain;
    chain.prepare(kFs);
    chain.setSettings(set);
    Stereo s = pink(3.0, 0.05, 3);
    for (std::size_t i = 0; i < s.l.size(); ++i) {
        const double t = static_cast<double>(i) / kFs;
        s.l[i] += 0.3 * std::sin(2 * kPi * 250.0 * t);
        s.r[i] = s.l[i];
    }
    const Stereo in = s;
    run(chain, s);
    const std::size_t a = 96000, b = 144000;
    const double cut = levelAt(s.l, 250.0, a, b) - levelAt(in.l, 250.0, a, b);
    const double far = bandDb(s.l, 3000.0, 4000.0, a, b) - bandDb(in.l, 3000.0, 4000.0, a, b);
    std::printf("      bosse 250 Hz : %.2f dB (coupe %.2f dB), 3-4 kHz : %+.2f dB\n", cut, chain.dynamicCutDb(0), far);
    OMNIS_CHECK(cut < -2.0 && cut > -6.5);
    OMNIS_CHECK(std::fabs(far) < 0.3);
}

OMNIS_TEST("stem chaîne: compression des drums, coups égalisés, niveau de la piste gardé") {
    StemSettings set = only();
    set.compRatio = 3.0;
    set.compDepthDb = 6.0;
    set.compAttackMs = 5.0;
    set.compReleaseMs = 100.0;
    StemChain chain;
    chain.prepare(kFs);
    chain.setSettings(set);
    // Un coup toutes les 0,25 s, de force inégale (vélocités d'un pack).
    const double velocity[] = {1.0, 0.45, 0.8, 0.3, 0.9, 0.5, 0.7, 0.35};
    Stereo s(8.0);
    for (std::size_t i = 0; i < s.l.size(); ++i) {
        const double time = static_cast<double>(i) / kFs;
        const int hit = static_cast<int>(time / 0.25);
        const double t = time - 0.25 * hit;
        const double v = 0.9 * velocity[hit % 8] * std::sin(2 * kPi * 120.0 * t) * (std::exp(-t / 0.03) + 0.2 * std::exp(-t / 0.2));
        s.l[i] = s.r[i] = v;
    }
    const Stereo in = s;
    run(chain, s);
    // Les 16 derniers coups : écart entre le plus fort et le plus faible (énergie des 40 premières ms).
    const auto spread = [](const std::vector<double>& x) {
        double lo = 1e9, hi = -1e9;
        for (int hit = 16; hit < 32; ++hit) {
            const auto from = static_cast<std::size_t>(hit * 0.25 * kFs);
            const double level = 20 * std::log10(rms(x, from, from + static_cast<std::size_t>(0.04 * kFs)));
            lo = std::min(lo, level);
            hi = std::max(hi, level);
        }
        return hi - lo;
    };
    const std::size_t a = 4 * 48000, b = 8 * 48000;
    const double levelChange = 20 * std::log10(rms(s.l, a, b) / rms(in.l, a, b));
    std::printf("      écart entre coups %.1f -> %.1f dB, niveau %+.2f dB, crête %+.2f dB\n", spread(in.l), spread(s.l), levelChange,
                20 * std::log10(peak(s.l, a, b) / peak(in.l, a, b)));
    OMNIS_CHECK(spread(s.l) < spread(in.l) - 1.5);
    OMNIS_CHECK(std::fabs(levelChange) < 2.0);
    OMNIS_CHECK(peak(s.l, a, b) < peak(in.l, a, b) * 1.12); // l'attaque passe (le punch), +1 dB au plus
}

OMNIS_TEST("stem chaîne: saturation de l'Inflator, crêtes en place, sans repliement") {
    StemSettings set = only();
    set.saturation = 0.5;
    StemChain chain;
    chain.prepare(kFs);
    chain.setSettings(set);
    // 9 kHz : son harmonique 3 (27 kHz) se replierait à 21 kHz sans suréchantillonnage.
    Stereo s(2.0);
    for (std::size_t i = 0; i < s.l.size(); ++i) s.l[i] = s.r[i] = 0.5 * std::sin(2 * kPi * 9000.0 * static_cast<double>(i) / kFs);
    const Stereo in = s;
    run(chain, s);
    const std::size_t a = 48000, b = 96000;
    const double fund = levelAt(s.l, 9000.0, a, b);
    const double alias = levelAt(s.l, 21000.0, a, b) - fund;
    std::printf("      9 kHz : repliement à 21 kHz %.1f dB\n", alias);
    OMNIS_CHECK(alias < -90.0);

    // 1 kHz : l'harmonique 3 est là (c'est la saturation voulue).
    StemChain c2;
    c2.prepare(kFs);
    c2.setSettings(set);
    Stereo t(2.0);
    for (std::size_t i = 0; i < t.l.size(); ++i) t.l[i] = t.r[i] = 0.5 * std::sin(2 * kPi * 1000.0 * static_cast<double>(i) / kFs);
    const Stereo t0 = t;
    run(c2, t);
    const double h3 = levelAt(t.l, 3000.0, a, b) - levelAt(t.l, 1000.0, a, b);
    std::printf("      1 kHz : harmonique 3 %.1f dB, crête %+.2f dB\n", h3, 20 * std::log10(peak(t.l, a, b) / peak(t0.l, a, b)));
    OMNIS_CHECK(h3 > -30.0 && h3 < -10.0);
    OMNIS_CHECK(peak(t.l, a, b) < peak(t0.l, a, b) * 1.19); // +1,5 dB au plus, à dose maximale

    // Kick (grave) : les crêtes restent en place, le corps monte (densité).
    for (const double hz : {60.0, 200.0}) {
        StemChain c3;
        c3.prepare(kFs);
        c3.setSettings(set);
        Stereo k(3.0);
        for (std::size_t i = 0; i < k.l.size(); ++i) {
            const double t1 = std::fmod(static_cast<double>(i) / kFs, 0.5);
            k.l[i] = k.r[i] = 0.8 * std::sin(2 * kPi * hz * t1) * std::exp(-t1 / 0.15);
        }
        const Stereo k0 = k;
        run(c3, k);
        const std::size_t from = 48000, to = 144000;
        const double crest = 20 * std::log10(peak(k.l, from, to) / peak(k0.l, from, to));
        const double body = 20 * std::log10(rms(k.l, from, to) / rms(k0.l, from, to));
        std::printf("      %.0f Hz : crête %+.2f dB, corps %+.2f dB\n", hz, crest, body);
        OMNIS_CHECK(crest < 0.3);
        OMNIS_CHECK(body > 0.5);
    }
}

OMNIS_TEST("stem réglages: un son déjà saturé ou large est respecté, un grave réel est gardé") {
    Character clean, crushed;
    clean.crestDb = 14.0;
    crushed.crestDb = 6.0;
    const auto a = settingsFor(StemMode::Auto, Element::Kick, clean, 10.0, {});
    const auto b = settingsFor(StemMode::Auto, Element::Kick, crushed, 10.0, {});
    OMNIS_CHECK(a.saturation > 0.2);
    OMNIS_CHECK(b.saturation < 0.3 * a.saturation);
    OMNIS_CHECK(b.compDepthDb < a.compDepthDb);

    Character wide;
    wide.crestDb = 12.0;
    wide.sideToMidDb = -3.0;
    OMNIS_CHECK(settingsFor(StemMode::Auto, Element::HiHat, wide, 10.0, {}).width <= 1.0);
    OMNIS_CHECK(settingsFor(StemMode::Auto, Element::HiHat, clean, 10.0, {}).width > 1.0);

    // Un piano qui joue une ligne de basse : son passe-haut descend.
    Character low;
    low.crestDb = 12.0;
    low.bandShare = {0.02, 0.35, 0.3, 0.25, 0.06, 0.02};
    const auto keys = settingsFor(StemMode::Auto, Element::Keys, low, 10.0, {});
    std::printf("      piano grave : passe-haut %.0f Hz (modèle %.0f Hz)\n", keys.hpfHz, templateFor(Element::Keys).hpfHz);
    OMNIS_CHECK(keys.hpfHz < 45.0);

    // Rien de reconnu en Auto : rien n'est fait.
    OMNIS_CHECK(!settingsFor(StemMode::Auto, Element::Auto, clean, 10.0, {}).active);
    // Chaque famille a son traitement : drums compressés et saturés, 808 saturée
    // avec EQ dynamique, instruments en EQ dynamique.
    const auto k = templateFor(Element::Kick), b808 = templateFor(Element::Bass808), piano = templateFor(Element::Keys);
    OMNIS_CHECK(k.compRatio > 1.0 && k.saturation > 0.0 && k.monoAmount > 0.0);
    OMNIS_CHECK(b808.saturation > 0.0 && b808.dynamic[0].maxCutDb > 0.0);
    OMNIS_CHECK(piano.dynamic[0].maxCutDb > 0.0 && piano.saturation == 0.0);
}

OMNIS_TEST("stem réglages: manuel borné (« sans partir dans l'absurde »), valeurs folles neutralisées") {
    StemMacros m;
    m.hpfHz = 2000.0;
    m.comp = 5.0;
    m.saturation = 3.0;
    m.clean = 9.0;
    m.width = 4.0;
    m.trimDb = 60.0;
    const auto s = settingsFor(StemMode::Manual, Element::Keys, {}, 0.0, m);
    OMNIS_CHECK(s.active && s.hpfHz <= 500.0 && s.compRatio <= 4.0 && s.compDepthDb <= 8.0);
    OMNIS_CHECK(s.saturation <= 0.5 && s.width <= 1.5 && s.trimDb <= 12.0);
    for (const auto& b : s.dynamic) OMNIS_CHECK(b.maxCutDb <= 6.0);
    // Manuel sans élément reconnu : les doses s'appliquent quand même.
    StemMacros half;
    half.comp = 0.5;
    OMNIS_CHECK(settingsFor(StemMode::Manual, Element::Auto, {}, 0.0, half).compRatio > 2.0);

    StemSettings bad;
    bad.active = true;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    bad.hpfHz = nan;
    bad.width = nan;
    bad.compRatio = std::numeric_limits<double>::infinity();
    bad.saturation = -1.0;
    bad.dynamic[0] = {nan, nan, nan, 1e9};
    const auto c = clampSettings(bad);
    OMNIS_CHECK(c.hpfHz == 0.0 && c.width == 1.0 && c.compRatio == 1.0 && c.saturation == 0.0);
    OMNIS_CHECK(std::isfinite(c.dynamic[0].hz) && c.dynamic[0].maxCutDb == 6.0);
}

OMNIS_TEST("stem chaîne: un NaN à l'entrée ne bloque rien, les modèles restent finis et sans saut") {
    for (const Element e : {Element::Kick, Element::Snare, Element::HiHat, Element::Bass808, Element::Keys, Element::Vocal, Element::Pad}) {
        StemChain chain;
        chain.prepare(kFs);
        Stereo s = pink(1.5, 0.3, 4);
        const Stereo in = s;
        // Activation en pleine lecture : fondu, pas de clic.
        chain.process(s.l.data(), s.r.data(), 4800);
        chain.setSettings(settingsFor(StemMode::Auto, e, {}, 0.0, {}));
        s.l[20000] = std::numeric_limits<double>::quiet_NaN();
        chain.process(s.l.data() + 4800, s.r.data() + 4800, static_cast<int>(s.l.size()) - 4800);
        bool finite = true;
        for (std::size_t i = 0; i < s.l.size(); ++i) finite = finite && std::isfinite(s.l[i]) && std::isfinite(s.r[i]);
        OMNIS_CHECK(finite);
        double jump = 0.0;
        for (std::size_t i = 4800; i < 4864; ++i) jump = std::max(jump, std::fabs(s.l[i] - in.l[i]));
        OMNIS_CHECK(jump < 0.05);
        OMNIS_CHECK(peak(s.l, 30000, s.l.size()) < 2.0 * peak(in.l, 30000, in.l.size()));
    }
}
