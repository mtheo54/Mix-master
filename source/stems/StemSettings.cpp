// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/StemSettings.cpp
// =============================================================================
#include "StemSettings.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace omnis::stems {

namespace {

/// Limites des bandes d'analyse du classifieur (bandShare).
constexpr std::array<double, kAnalysisBands + 1> kBandEdges{20.0, 60.0, 150.0, 400.0, 2000.0, 6000.0, 20000.0};

double finiteOr(double v, double fallback) noexcept { return std::isfinite(v) ? v : fallback; }

/// Part de l'énergie mesurée sous `hz` (interpolation logarithmique dans la bande).
double shareBelow(const Character& c, double hz) noexcept {
    double share = 0.0;
    for (int b = 0; b < kAnalysisBands; ++b) {
        const double lo = kBandEdges[static_cast<std::size_t>(b)], hi = kBandEdges[static_cast<std::size_t>(b) + 1];
        const double s = finiteOr(c.bandShare[static_cast<std::size_t>(b)], 0.0);
        if (hz >= hi) share += s;
        else if (hz > lo) share += s * std::log(hz / lo) / std::log(hi / lo);
    }
    return share;
}

struct Builder {
    StemSettings s;
    Builder() { s.active = true; }
    Builder& hpf(double hz, bool steep = false) { s.hpfHz = hz; s.hpfSteep = steep; return *this; }
    Builder& band(int i, double hz, double q, double allowDb, double maxCutDb) {
        s.dynamic[static_cast<std::size_t>(i)] = {hz, q, allowDb, maxCutDb};
        return *this;
    }
    Builder& comp(double ratio, double depthDb, double attackMs, double releaseMs) {
        s.compRatio = ratio; s.compDepthDb = depthDb; s.compAttackMs = attackMs; s.compReleaseMs = releaseMs;
        return *this;
    }
    Builder& sat(double dose) { s.saturation = dose; return *this; }
    Builder& mono(double belowHz, double amount) { s.monoBelowHz = belowHz; s.monoAmount = amount; return *this; }
    Builder& width(double w) { s.width = w; return *this; }
};

} // namespace

Correction clampCorrection(Correction c) noexcept {
    if (!std::isfinite(c.hz) || !std::isfinite(c.gainDb) || !std::isfinite(c.q)) return {};
    switch (c.type) {
        case Correction::Type::LowCut:
            return {c.type, std::clamp(c.hz, 20.0f, 300.0f), 0.0f, 0.7071f};
        case Correction::Type::Bell:
            return {c.type, std::clamp(c.hz, 20.0f, 16000.0f), std::clamp(c.gainDb, -6.0f, 3.0f), std::clamp(c.q, 0.3f, 4.0f)};
        case Correction::Type::LowShelf:
            return {c.type, std::clamp(c.hz, 20.0f, 400.0f), std::clamp(c.gainDb, -6.0f, 3.0f), 0.7071f};
        case Correction::Type::None:
        default:
            return {};
    }
}

double pinkBalanceDb(double q) noexcept {
    const double share = std::min(0.95, std::numbers::pi / (2.0 * std::max(q, 1.0e-3) * std::log(1000.0)));
    return 10.0 * std::log10(share / (1.0 - share));
}

StemSettings templateFor(Element element) noexcept {
    // Doses moyennes : ce qu'un ingénieur ferait d'abord sur ce genre de son.
    // Bandes dynamiques : boue (~250), carton (~400-800), dureté (~3-4 k),
    // sifflantes / grésillement (~7 k).
    switch (element) {
        // Kick, 808, basse : pas de passe-haut. Sur un son écrêté, retirer le
        // continu de l'attaque décale la forme d'onde : +3 à +4 dB de crête
        // mesurés sur une vraie 808, même à 10 Hz (le master gère le grondement).
        case Element::Kick:
            return Builder().band(0, 400.0, 1.4, -3.0, 4.0).comp(3.0, 4.0, 15.0, 90.0).sat(0.25).mono(150.0, 1.0).s;
        case Element::Snare:
            return Builder().hpf(80.0, true).band(0, 350.0, 1.2, 3.0, 3.0).band(1, 5000.0, 1.4, 4.0, 3.0).comp(3.0, 4.0, 10.0, 120.0).sat(0.2).mono(150.0, 1.0).s;
        case Element::Clap:
            return Builder().hpf(120.0).band(0, 800.0, 1.2, 3.0, 3.0).comp(2.5, 3.0, 5.0, 100.0).sat(0.15).mono(150.0, 1.0).width(1.05).s;
        case Element::HiHat:
            return Builder().hpf(350.0, true).band(0, 7000.0, 1.5, 8.0, 4.0).width(1.1).s;
        case Element::OpenHat:
            return Builder().hpf(300.0, true).band(0, 6500.0, 1.5, 8.0, 4.0).comp(2.0, 2.0, 5.0, 80.0).width(1.15).s;
        case Element::Cymbal:
            return Builder().hpf(300.0).band(0, 5000.0, 1.4, 7.0, 3.0).width(1.15).s;
        case Element::Perc:
            return Builder().hpf(100.0).band(0, 500.0, 1.4, 4.0, 3.0).band(1, 4000.0, 1.4, 5.0, 3.0).comp(2.5, 3.0, 8.0, 100.0).sat(0.15).mono(150.0, 1.0).width(1.1).s;
        case Element::DrumLoop:
            return Builder().hpf(30.0).band(0, 300.0, 1.2, 3.0, 3.0).band(1, 4000.0, 1.4, 2.0, 3.0).comp(2.0, 3.0, 20.0, 120.0).sat(0.15).mono(150.0, 1.0).s;
        case Element::Bass808:
            // La 808 : saturation (elle s'entend sur les petits haut-parleurs) et
            // EQ dynamique sur le « boum » au-dessus de la fondamentale.
            return Builder().band(0, 250.0, 1.0, -2.0, 4.0).comp(2.0, 3.0, 30.0, 150.0).sat(0.3).mono(200.0, 1.0).s;
        case Element::Bass:
            return Builder().band(0, 250.0, 1.0, 3.0, 3.0).band(1, 800.0, 1.4, -2.0, 3.0).comp(3.0, 4.0, 20.0, 120.0).sat(0.2).mono(150.0, 1.0).s;
        case Element::Keys:
            return Builder().hpf(60.0).band(0, 250.0, 1.0, 7.0, 4.0).band(1, 600.0, 1.4, 6.0, 3.0).band(2, 3500.0, 1.4, -2.0, 3.0).mono(120.0, 0.7).s;
        case Element::Guitar:
            return Builder().hpf(80.0).band(0, 250.0, 1.0, 6.0, 4.0).band(1, 3000.0, 1.4, 1.0, 3.0).mono(120.0, 0.7).s;
        case Element::Brass:
            return Builder().hpf(100.0).band(0, 500.0, 1.2, 7.0, 3.0).band(1, 2500.0, 1.4, 4.0, 4.0).comp(2.0, 3.0, 10.0, 120.0).mono(120.0, 0.7).s;
        case Element::Strings:
            return Builder().hpf(60.0).band(0, 300.0, 1.0, 6.0, 3.0).band(1, 4000.0, 1.4, -1.0, 3.0).mono(120.0, 0.7).width(1.05).s;
        case Element::Pad:
            return Builder().hpf(100.0).band(0, 300.0, 1.0, 6.0, 4.0).band(1, 2000.0, 1.2, 2.0, 2.0).mono(150.0, 0.8).width(1.1).s;
        case Element::Lead:
            return Builder().hpf(120.0).band(0, 300.0, 1.0, 5.0, 3.0).band(1, 3000.0, 1.4, 4.0, 4.0).comp(2.0, 2.0, 10.0, 100.0).mono(150.0, 0.8).s;
        case Element::Pluck:
            return Builder().hpf(120.0).band(0, 300.0, 1.0, 5.0, 3.0).band(1, 4000.0, 1.4, 2.0, 3.0).mono(150.0, 0.8).width(1.05).s;
        case Element::Vocal:
            return Builder().hpf(80.0).band(0, 250.0, 1.0, 4.0, 4.0).band(1, 3500.0, 1.4, 4.0, 3.0).band(2, 7000.0, 2.0, 0.0, 5.0).comp(3.0, 5.0, 8.0, 100.0).mono(200.0, 1.0).s;
        case Element::Backs:
            return Builder().hpf(120.0).band(0, 300.0, 1.0, 4.0, 4.0).band(1, 3500.0, 1.4, 4.0, 3.0).band(2, 7000.0, 2.0, 0.0, 4.0).comp(2.5, 4.0, 8.0, 100.0).width(1.15).s;
        case Element::Fx:
        case Element::Other:
            return Builder().hpf(25.0).s;
        case Element::Auto:
        case Element::Count:
        default:
            return StemSettings{}; // rien de reconnu : rien n'est fait
    }
}

StemSettings clampSettings(StemSettings s) noexcept {
    StemSettings d;
    s.trimDb = std::clamp(finiteOr(s.trimDb, 0.0), -24.0, 12.0);
    s.hpfHz = finiteOr(s.hpfHz, 0.0);
    s.hpfHz = s.hpfHz < 10.0 ? 0.0 : std::min(s.hpfHz, 500.0);
    for (auto& b : s.dynamic) {
        b.hz = std::clamp(finiteOr(b.hz, 1000.0), 40.0, 16000.0);
        b.q = std::clamp(finiteOr(b.q, 1.0), 0.3, 4.0);
        b.allowDb = std::clamp(finiteOr(b.allowDb, 0.0), -12.0, 12.0);
        b.maxCutDb = std::clamp(finiteOr(b.maxCutDb, 0.0), 0.0, 6.0);
    }
    s.compRatio = std::clamp(finiteOr(s.compRatio, 1.0), 1.0, 4.0);
    s.compDepthDb = std::clamp(finiteOr(s.compDepthDb, 0.0), 0.0, 8.0);
    s.compAttackMs = std::clamp(finiteOr(s.compAttackMs, d.compAttackMs), 0.5, 100.0);
    s.compReleaseMs = std::clamp(finiteOr(s.compReleaseMs, d.compReleaseMs), 20.0, 1000.0);
    s.saturation = std::clamp(finiteOr(s.saturation, 0.0), 0.0, 0.5);
    s.monoBelowHz = finiteOr(s.monoBelowHz, 0.0);
    s.monoBelowHz = s.monoBelowHz < 40.0 ? 0.0 : std::min(s.monoBelowHz, 300.0);
    s.monoAmount = s.monoBelowHz > 0.0 ? std::clamp(finiteOr(s.monoAmount, 0.0), 0.0, 1.0) : 0.0;
    s.width = std::clamp(finiteOr(s.width, 1.0), 0.0, 1.5);
    for (auto& c : s.corrections) c = clampCorrection(c);
    return s;
}

StemSettings settingsFor(StemMode mode, Element element, const Character& character, double secondsHeard,
                         const StemMacros& macros) noexcept {
    if (mode == StemMode::Off) return StemSettings{};
    const Family family = familyOf(element);

    if (mode == StemMode::Manual) {
        // La personne règle les doses ; fréquences et temps viennent de l'élément
        // (rien de reconnu : modèle générique).
        StemSettings t = templateFor(element == Element::Auto ? Element::Other : element);
        StemSettings s = t;
        s.active = true;
        s.trimDb = macros.trimDb;
        s.hpfHz = finiteOr(macros.hpfHz, 0.0) < 12.0 ? 0.0 : macros.hpfHz;
        const double comp = std::clamp(finiteOr(macros.comp, 0.0), 0.0, 1.0);
        s.compRatio = 1.0 + 3.0 * comp;
        s.compDepthDb = 8.0 * comp;
        if (t.compRatio <= 1.0) {
            const bool drums = family == Family::Drums;
            s.compAttackMs = drums ? 10.0 : 15.0;
            s.compReleaseMs = drums ? 100.0 : 150.0;
        }
        s.saturation = 0.5 * std::clamp(finiteOr(macros.saturation, 0.0), 0.0, 1.0);
        const double clean = std::clamp(finiteOr(macros.clean, 0.0), 0.0, 1.0);
        for (auto& b : s.dynamic) b.maxCutDb = b.maxCutDb > 0.0 ? 6.0 * clean : 0.0;
        s.width = macros.width;
        return clampSettings(s);
    }

    // Auto : le modèle de l'élément, ajusté à ce que l'écoute a mesuré.
    StemSettings s = templateFor(element);
    if (!s.active) return s;
    s.trimDb = macros.trimDb;
    if (secondsHeard > 0.0) {
        const double crest = finiteOr(character.crestDb, 12.0);
        // Un son déjà saturé ou écrasé (crête basse) : la saturation voulue est
        // là, on n'en rajoute presque pas.
        s.saturation *= std::clamp((crest - 4.0) / 8.0, 0.2, 1.0);
        // Déjà tenu : moins de compression, plus du tout s'il est plat.
        const double flat = family == Family::Drums || family == Family::Bass ? 8.0 : 6.0;
        if (crest < flat - 3.0) s.compDepthDb = 0.0;
        else if (crest < flat) s.compDepthDb *= 0.5;
        // Déjà large : on n'élargit pas.
        if (finiteOr(character.sideToMidDb, -120.0) > -6.0) s.width = std::min(s.width, 1.0);
        // Un instrument qui joue vraiment dans le grave garde son grave : le
        // passe-haut descend tant que l'octave au-dessus de sa coupure (là où il
        // agit encore) porte plus de 15 % de l'énergie.
        if (s.hpfHz > 40.0)
            while (s.hpfHz > 20.0 && shareBelow(character, 2.0 * s.hpfHz) > 0.15) s.hpfHz /= std::pow(2.0, 1.0 / 6.0);
        if (s.hpfHz < 20.0) s.hpfHz = 0.0;
    }
    return clampSettings(s);
}

} // namespace omnis::stems
