// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestStems.cpp — Reconnaître les éléments des stems (nom et écoute).
// =============================================================================
#include "TestFramework.h"

#include "stems/Element.h"
#include "stems/ElementClassifier.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace omnis::stems;

namespace {

constexpr double kFs = 44100.0;
constexpr double kPi = std::numbers::pi;

struct Stereo {
    std::vector<double> l, r;
    explicit Stereo(double seconds) : l(static_cast<std::size_t>(seconds * kFs)), r(l.size()) {}
    void add(std::size_t i, double v, double pan = 0.0) {
        if (i >= l.size()) return;
        l[i] += v * (1.0 - pan);
        r[i] += v * (1.0 + pan);
    }
};

/// Passe-bande RBJ (gain de crête 0 dB).
struct Bandpass {
    double b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    Bandpass(double f, double q) {
        const double w = 2 * kPi * f / kFs, alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
        b0 = alpha / a0;
        b2 = -alpha / a0;
        a1 = -2 * std::cos(w) / a0;
        a2 = (1 - alpha) / a0;
    }
    double operator()(double x) {
        const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

std::mt19937 rng(7);
double noise() { return std::uniform_real_distribution<double>(-1.0, 1.0)(rng); }

void kickAt(Stereo& s, double t0, double gain = 0.8) {
    double phase = 0.0;
    const auto start = static_cast<std::size_t>(t0 * kFs);
    for (int n = 0; n < static_cast<int>(0.45 * kFs); ++n) {
        const double t = n / kFs;
        const double f = 45.0 + 115.0 * std::exp(-t / 0.03);
        phase += 2 * kPi * f / kFs;
        const double click = n < 120 ? 0.3 * noise() * (1.0 - n / 120.0) : 0.0;
        s.add(start + static_cast<std::size_t>(n), gain * (std::sin(phase) * std::exp(-t / 0.12) + click));
    }
}

void snareAt(Stereo& s, double t0) {
    Bandpass bp(2500.0, 0.5);
    double phase = 0.0;
    const auto start = static_cast<std::size_t>(t0 * kFs);
    for (int n = 0; n < static_cast<int>(0.35 * kFs); ++n) {
        const double t = n / kFs;
        phase += 2 * kPi * 190.0 / kFs;
        const double body = 0.5 * std::sin(phase) * std::exp(-t / 0.05);
        s.add(start + static_cast<std::size_t>(n), 0.6 * (bp(noise()) * 1.6 * std::exp(-t / 0.08) + body));
    }
}

void clapAt(Stereo& s, double t0) {
    Bandpass bp(1800.0, 1.2);
    const auto start = static_cast<std::size_t>(t0 * kFs);
    for (int n = 0; n < static_cast<int>(0.3 * kFs); ++n) {
        const double t = n / kFs;
        // Trois éclats rapprochés puis la queue.
        double env = std::exp(-t / 0.07);
        for (double b : {0.0, 0.011, 0.022})
            if (t >= b && t < b + 0.006) env = std::max(env, 1.0);
        s.add(start + static_cast<std::size_t>(n), 0.7 * bp(noise()) * 2.0 * env);
    }
}

void hatAt(Stereo& s, double t0, double tau, double gain = 0.4) {
    double prev = 0.0, prev2 = 0.0;
    const auto start = static_cast<std::size_t>(t0 * kFs);
    for (int n = 0; n < static_cast<int>(std::min(1.2, 8 * tau) * kFs); ++n) {
        const double t = n / kFs;
        // Bruit dérivé deux fois : énergie dans l'aigu.
        const double w = noise();
        const double h = w - 2 * prev + prev2;
        prev2 = prev;
        prev = w;
        s.add(start + static_cast<std::size_t>(n), gain * 0.35 * h * std::exp(-t / tau), 0.1);
    }
}

void bass808At(Stereo& s, double t0, double f0, double length) {
    double phase = 0.0;
    const auto start = static_cast<std::size_t>(t0 * kFs);
    for (int n = 0; n < static_cast<int>(length * kFs); ++n) {
        const double t = n / kFs;
        const double f = f0 * (1.0 + 1.0 * std::exp(-t / 0.02));
        phase += 2 * kPi * f / kFs;
        const double env = std::min(1.0, t / 0.005) * std::exp(-t / 1.5) * std::min(1.0, (length - t) / 0.02);
        s.add(start + static_cast<std::size_t>(n), 0.8 * std::tanh(2.0 * std::sin(phase)) / std::tanh(2.0) * env);
    }
}

void pianoNote(Stereo& s, double t0, double f0) {
    const auto start = static_cast<std::size_t>(t0 * kFs);
    for (int n = 0; n < static_cast<int>(1.2 * kFs); ++n) {
        const double t = n / kFs;
        double v = 0.0;
        for (int k = 1; k <= 6; ++k) v += std::sin(2 * kPi * f0 * k * t) / k * std::exp(-t * k / 0.8);
        s.add(start + static_cast<std::size_t>(n), 0.25 * v * std::min(1.0, t / 0.003), 0.2);
    }
}

Stereo makePad(double seconds) {
    Stereo s(seconds);
    for (std::size_t i = 0; i < s.l.size(); ++i) {
        const double t = static_cast<double>(i) / kFs;
        double v = 0.0;
        for (double f : {261.6, 329.6, 392.0})
            for (int k = 1; k <= 5; ++k) v += std::sin(2 * kPi * f * k * t + k) / (k * k);
        const double env = std::min(1.0, t / 1.0);
        s.l[i] = 0.12 * v * env * (1.0 + 0.2 * std::sin(2 * kPi * 0.3 * t));
        s.r[i] = 0.12 * v * env * (1.0 - 0.2 * std::sin(2 * kPi * 0.3 * t));
    }
    return s;
}

Stereo makeVoice(double seconds) {
    Stereo s(seconds);
    Bandpass f1(700.0, 4.0), f2(1200.0, 5.0), f3(2600.0, 6.0);
    double phase = 0.0;
    for (std::size_t i = 0; i < s.l.size(); ++i) {
        const double t = static_cast<double>(i) / kFs;
        phase += 2 * kPi * (140.0 * (1.0 + 0.02 * std::sin(2 * kPi * 5.5 * t)) + 30.0 * std::sin(2 * kPi * 0.4 * t)) / kFs;
        const double saw = 2.0 * (phase / (2 * kPi) - std::floor(phase / (2 * kPi))) - 1.0;
        // Syllabes : 4 par seconde, avec de courts silences.
        const double syl = std::fmod(t * 4.0, 1.0);
        const double env = syl < 0.75 ? std::sin(kPi * syl / 0.75) : 0.0;
        const double v = (f1(saw) * 1.0 + f2(saw) * 0.7 + f3(saw) * 0.35) * env;
        s.l[i] = s.r[i] = 0.5 * v;
    }
    return s;
}

Identification listen(const Stereo& s, const char* name = "") {
    ElementClassifier c;
    c.prepare(kFs);
    // Par blocs, comme le thread du lien.
    for (std::size_t i = 0; i < s.l.size(); i += 1024) {
        const int n = static_cast<int>(std::min<std::size_t>(1024, s.l.size() - i));
        c.push(s.l.data() + i, s.r.data() + i, n);
    }
    return c.identify(Element::Auto, name);
}

void report(const char* what, const Identification& id) {
    std::printf("      %-10s -> %-16s (confiance %.2f, %.1f coups/s, montée %.0f ms, décroissance %.0f ms, bruit %.2f, grave %.2f, aigu %.2f, crête %.1f dB)\n",
                what, elementLabel(id.heard), id.confidence, id.character.onsetsPerSecond, id.character.attackMs, id.character.decayMs, id.character.noisiness,
                id.character.bandShare[0] + id.character.bandShare[1], id.character.bandShare[4] + id.character.bandShare[5],
                id.character.crestDb);
}

} // namespace

OMNIS_TEST("stems: nom de piste -> élément (noms de packs, abréviations, collés)") {
    struct Case {
        const char* name;
        Element expected;
    };
    const Case cases[] = {
        {"SH_Kick_Hard_04_Cmin", Element::Kick},   {"PL_ClosedHat_12", Element::HiHat},     {"OH 03", Element::OpenHat},
        {"Snr Crack", Element::Snare},             {"clp", Element::Clap},                  {"808_Mafia_Bass_C", Element::Bass808},
        {"VOX_lead", Element::Vocal},              {"Rhodes Chords", Element::Keys},        {"gtr", Element::Guitar},
        {"trp", Element::Brass},                   {"JAYY SPLASH 03", Element::Cymbal},     {"BD_Punchy", Element::Kick},
        {"jayykick", Element::Kick},               {"Bass Drum 2", Element::Kick},          {"Trompette", Element::Brass},
        {"Adlibs", Element::Backs},                {"Shaker 1", Element::Perc},             {"Piano", Element::Keys},
        {"guitare", Element::Guitar},              {"OpenHat_Loose", Element::OpenHat},     {"Riser FX", Element::Riser},
        {"Insert 3", Element::Auto},               {"Audio 12", Element::Auto},             {"Cmin_Ghost_140", Element::Auto},
        // EFX : le groupe des effets (reverse cymbals, risers, impacts…).
        {"Rev Crash 03", Element::ReverseCymbal},  {"REVERSE_CYMBAL_Long", Element::ReverseCymbal}, {"RevCym 2", Element::ReverseCymbal},
        {"Uplifter 8 bars", Element::Riser},       {"FX Impact", Element::Impact},          {"Sub Drop", Element::Impact},
        {"Downlifter", Element::Impact},           {"Vinyl Crackle", Element::Fx},          {"Tape Stop", Element::Fx},
    };
    for (const auto& c : cases) {
        const auto hint = elementFromName(c.name);
        if (hint.element != c.expected)
            std::printf("      « %s » -> %s (attendu %s)\n", c.name, elementLabel(hint.element), elementLabel(c.expected));
        OMNIS_CHECK(hint.element == c.expected);
    }
    OMNIS_CHECK(nameWords("SH_KickHard04") == std::vector<std::string>({"sh", "kick", "hard", "04"}));
}

OMNIS_TEST("stems: l'écoute reconnaît kick, 808, snare, clap, hats, piano, nappe, voix, boucle") {
    Stereo kick(8.0), b808(8.0), snare(8.0), clap(8.0), hat(8.0), open(8.0), piano(8.0), loop(8.0);
    for (int i = 0; i < 16; ++i) kickAt(kick, 0.5 * i);
    for (int i = 0; i < 8; ++i) bass808At(b808, 1.0 * i, i % 2 ? 49.0 : 43.65, 0.95);
    for (int i = 0; i < 8; ++i) snareAt(snare, 1.0 * i + 0.5);
    for (int i = 0; i < 8; ++i) clapAt(clap, 1.0 * i + 0.5);
    for (int i = 0; i < 32; ++i) hatAt(hat, 0.25 * i, 0.018);
    for (int i = 0; i < 8; ++i) hatAt(open, 1.0 * i, 0.32);
    const double notes[] = {261.6, 329.6, 392.0, 523.3, 440.0, 349.2, 293.7, 392.0};
    for (int i = 0; i < 16; ++i) pianoNote(piano, 0.5 * i, notes[i % 8]);
    for (int i = 0; i < 16; ++i) kickAt(loop, 0.5 * i);
    for (int i = 0; i < 8; ++i) snareAt(loop, 1.0 * i + 0.5);
    for (int i = 0; i < 32; ++i) hatAt(loop, 0.25 * i + 0.125, 0.018, 0.25);
    const Stereo pad = makePad(8.0), voice = makeVoice(8.0);

    struct Case {
        const char* what;
        const Stereo* s;
        Element expected;
    };
    const Case cases[] = {{"kick", &kick, Element::Kick},        {"808", &b808, Element::Bass808},     {"snare", &snare, Element::Snare},
                          {"clap", &clap, Element::Clap},        {"hat", &hat, Element::HiHat},        {"open hat", &open, Element::OpenHat},
                          {"piano", &piano, Element::Keys},      {"nappe", &pad, Element::Pad},        {"voix", &voice, Element::Vocal},
                          {"boucle", &loop, Element::DrumLoop}};
    for (const auto& c : cases) {
        const auto id = listen(*c.s);
        report(c.what, id);
        OMNIS_CHECK(id.heard == c.expected);
    }
}

OMNIS_TEST("stems: sons saturés ou « bizarres » toujours reconnus, caractère mesuré") {
    // Kick et 808 écrasés dans une saturation forte : des harmoniques jusqu'à
    // plusieurs kHz, une crête basse. Ce que le son FAIT ne change pas.
    Stereo kick(8.0), b808(8.0);
    for (int i = 0; i < 16; ++i) kickAt(kick, 0.5 * i, 0.9);
    for (int i = 0; i < 8; ++i) bass808At(b808, 1.0 * i, 43.65, 0.95);
    for (auto* s : {&kick, &b808})
        for (std::size_t i = 0; i < s->l.size(); ++i) {
            s->l[i] = 0.9 * std::tanh(6.0 * s->l[i]);
            s->r[i] = 0.9 * std::tanh(6.0 * s->r[i]);
        }
    const auto k = listen(kick), b = listen(b808);
    report("kick sat.", k);
    report("808 sat.", b);
    OMNIS_CHECK(k.heard == Element::Kick);
    OMNIS_CHECK(b.heard == Element::Bass808);
    // La saturation se voit dans le caractère : crête plus basse que le kick propre.
    Stereo clean(8.0);
    for (int i = 0; i < 16; ++i) kickAt(clean, 0.5 * i);
    OMNIS_CHECK(k.character.crestDb < listen(clean).character.crestDb - 3.0);
}

OMNIS_TEST("stems: écoute et nom combinés (le nom précise, l'écoute nette l'emporte)") {
    std::array<double, kElementCount> scores{};
    scores[static_cast<std::size_t>(Element::Keys)] = 0.6;
    scores[static_cast<std::size_t>(Element::Pad)] = 0.2;
    // Même famille : le nom précise.
    OMNIS_CHECK(combine(scores, elementFromName("guitare"), Element::Auto, 10.0).element == Element::Guitar);
    // Rien de nommé : l'écoute.
    OMNIS_CHECK(combine(scores, elementFromName("Audio 3"), Element::Auto, 10.0).element == Element::Keys);
    // Familles différentes, écoute nette : l'écoute gagne et le désaccord est signalé.
    const auto d = combine(scores, elementFromName("kick"), Element::Auto, 10.0);
    OMNIS_CHECK(d.element == Element::Keys && d.nameDisagrees);
    // Pas assez écouté : le nom.
    OMNIS_CHECK(combine(scores, elementFromName("kick"), Element::Auto, 0.5).element == Element::Kick);
    // Choix de la personne : toujours.
    const auto m = combine(scores, elementFromName("kick"), Element::Snare, 10.0);
    OMNIS_CHECK(m.element == Element::Snare && m.manual);
    // Drums : un hat nommé « hat » mais qui tient longtemps est un open hat.
    std::array<double, kElementCount> hats{};
    hats[static_cast<std::size_t>(Element::OpenHat)] = 0.7;
    hats[static_cast<std::size_t>(Element::HiHat)] = 0.2;
    OMNIS_CHECK(combine(hats, elementFromName("Hat 04"), Element::Auto, 10.0).element == Element::OpenHat);
    // Un bus nommé « DRUMS » dont le kick est ailleurs sonne surtout hats : le nom du groupe l'emporte.
    OMNIS_CHECK(combine(hats, elementFromName("DRUMS"), Element::Auto, 10.0).element == Element::DrumLoop);
}

OMNIS_TEST("stems: groupes DRUMS / 808 / MÉLO / VOIX / EFX, nom d'un bus -> son groupe") {
    OMNIS_CHECK(familyOf(Element::ReverseCymbal) == Family::Fx && familyOf(Element::Riser) == Family::Fx && familyOf(Element::Impact) == Family::Fx);
    OMNIS_CHECK(std::string(familyLabel(Family::Fx)) == "EFX" && std::string(familyLabel(Family::Instrument)) == "MÉLO");
    OMNIS_CHECK(familyFromName("EFX") == Family::Fx);
    OMNIS_CHECK(familyFromName("FX Bus") == Family::Fx);
    OMNIS_CHECK(familyFromName("DRUMS") == Family::Drums);
    OMNIS_CHECK(familyFromName("Drum Bus") == Family::Drums);
    OMNIS_CHECK(familyFromName("MELO") == Family::Instrument);
    OMNIS_CHECK(familyFromName("Synths") == Family::Instrument);
    OMNIS_CHECK(familyFromName("808") == Family::Bass);
    OMNIS_CHECK(familyFromName("Insert 12") == Family::Unknown);
    // Chaque élément a sa clé et son libellé, relus à l'identique.
    for (int i = 1; i < kElementCount; ++i) OMNIS_CHECK(elementFromKey(elementKey(static_cast<Element>(i))) == static_cast<Element>(i));
}

namespace {

/// Une reverse cymbal : bruit aigu qui monte en `rise` secondes et s'arrête net, toutes les `period` s.
Stereo makeReverse(double seconds, double rise, double period) {
    Stereo s(seconds);
    Bandpass hp(9000.0, 0.7);
    for (double t0 = 0.3; t0 + rise < seconds; t0 += period)
        for (std::size_t k = 0; k < static_cast<std::size_t>(rise * kFs); ++k) {
            const double x = static_cast<double>(k) / (rise * kFs);
            s.add(static_cast<std::size_t>(t0 * kFs) + k, 0.5 * std::pow(x, 3.0) * hp(noise()));
        }
    return s;
}

/// Un riser : bruit filtré dont la hauteur et le volume montent pendant `rise` secondes, puis retombent doucement.
Stereo makeRiser(double seconds, double rise, double period) {
    Stereo s(seconds);
    for (double t0 = 0.3; t0 + rise + 0.6 < seconds; t0 += period) {
        Bandpass bp(500.0, 1.0);
        for (std::size_t k = 0; k < static_cast<std::size_t>((rise + 0.6) * kFs); ++k) {
            const double t = static_cast<double>(k) / kFs;
            const double x = std::min(1.0, t / rise);
            if (k % 64 == 0) bp = [&] {
                Bandpass b(300.0 * std::pow(30.0, x), 1.2);
                b.x1 = bp.x1, b.x2 = bp.x2, b.y1 = bp.y1, b.y2 = bp.y2;
                return b;
            }();
            const double env = t < rise ? std::pow(x, 2.0) : std::exp(-(t - rise) / 0.15);
            s.add(static_cast<std::size_t>(t0 * kFs) + k, 0.6 * env * bp(noise()));
        }
    }
    return s;
}

/// Un impact : coup grave et large (boom + bruit) qui traîne près de 2 s, toutes les `period` s.
Stereo makeImpact(double seconds, double period) {
    Stereo s(seconds);
    for (double t0 = 0.2; t0 + 2.5 < seconds; t0 += period) {
        double phase = 0.0;
        for (std::size_t k = 0; k < static_cast<std::size_t>(2.5 * kFs); ++k) {
            const double t = static_cast<double>(k) / kFs;
            phase += 2 * kPi * (45.0 + 60.0 * std::exp(-t / 0.08)) / kFs;
            // Le boom et sa couche de bruit large (crash, souffle), comme dans les packs d'EFX.
            const double v = 0.6 * std::exp(-t / 0.6) * std::sin(phase) + 0.5 * std::exp(-t / 0.7) * noise();
            s.add(static_cast<std::size_t>(t0 * kFs) + k, v);
        }
    }
    return s;
}

} // namespace

OMNIS_TEST("stems: EFX reconnus à l'écoute (reverse cymbal, riser, impact), pas pris pour une cymbale ou une nappe") {
    const auto rev = listen(makeReverse(16.0, 1.2, 4.0));
    report("reverse", rev);
    std::printf("      montées : %.0f %% du son, %.0f ms, coupées net %.0f %%\n", 100 * rev.character.swellShare, rev.character.swellMs,
                100 * rev.character.abruptShare);
    OMNIS_CHECK(rev.heard == Element::ReverseCymbal);
    const auto riser = listen(makeRiser(20.0, 3.5, 5.0));
    report("riser", riser);
    OMNIS_CHECK(riser.heard == Element::Riser);
    const auto impact = listen(makeImpact(16.0, 4.0));
    report("impact", impact);
    OMNIS_CHECK(impact.heard == Element::Impact);
    // Le reste n'est pas pris pour un EFX : une nappe, un kick, des hats.
    const auto pad = listen(makePad(8.0));
    OMNIS_CHECK(familyOf(pad.heard) != Family::Fx);
    Stereo hats(8.0);
    for (double t = 0.1; t < 7.9; t += 0.25) hatAt(hats, t, 0.02);
    OMNIS_CHECK(familyOf(listen(hats).heard) != Family::Fx);
    // Nommé « Rev Crash » et entendu reverse : reverse ; un nom contraire ne l'emporte pas sur une écoute nette.
    OMNIS_CHECK(listen(makeReverse(16.0, 1.2, 4.0), "Rev Crash 03").element == Element::ReverseCymbal);
}
