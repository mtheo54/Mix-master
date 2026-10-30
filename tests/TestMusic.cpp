// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestMusic.cpp — La fiche complète d'un son (analysis/MusicDescriptors) :
//  ce qu'elle mesure, comment elle s'écrit dans la base, ce qu'OMNIS en fait.
// =============================================================================
#include "TestFramework.h"

#include "ai/ConsoleActions.h"
#include "ai/ReferenceLibrary.h"
#include "analysis/MusicDescriptors.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <vector>

using namespace omnis;
using namespace omnis::analysis;

namespace {

constexpr double kFs = 48000.0;

/// Une boucle trap : kick sur 1 et 3, hats en croches, 808 tenue, accord de La mineur.
void trapLoop(double bpm, double seconds, double subHz, std::vector<double>& l, std::vector<double>& r) {
    const int n = static_cast<int>(seconds * kFs);
    l.assign(static_cast<std::size_t>(n), 0.0);
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    const double beat = 60.0 / bpm;
    double prev = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i / kFs;
        const double inBeat = std::fmod(t, beat), inEighth = std::fmod(t, beat / 2.0);
        const int beatIndex = static_cast<int>(t / beat);
        double x = 0.25 * std::sin(2.0 * std::numbers::pi * subHz * t); // 808 tenue
        for (const double f : {220.0, 261.63, 329.63}) x += 0.04 * std::sin(2.0 * std::numbers::pi * f * t);
        if (beatIndex % 2 == 0 && inBeat < 0.25) x += 0.5 * std::exp(-inBeat / 0.06) * std::sin(2.0 * std::numbers::pi * (50.0 + 80.0 * std::exp(-inBeat / 0.02)) * inBeat);
        if (inEighth < 0.05) {
            const double w = u(rng), hp = w - prev;
            prev = w;
            x += 0.15 * std::exp(-inEighth / 0.012) * hp;
        }
        l[static_cast<std::size_t>(i)] = x;
    }
    r = l;
}

} // namespace

OMNIS_TEST("fiche: tempo, note de la 808, tonalité et stéréo d'une boucle trap") {
    std::vector<double> l, r;
    trapLoop(142.0, 40.0, 55.0, l, r);
    const auto m = analyzeMusic(l.data(), r.data(), static_cast<int>(l.size()), kFs);
    std::printf("    %.1f BPM, %s, 808 %s, %.1f coups/s, corrélation %.2f / grave %.2f\n", m.bpm, m.keyName().c_str(),
                MusicDescriptors::noteName(m.subHz).c_str(), m.onsetsPerSecond, m.correlation, m.lowCorrelation);
    OMNIS_CHECK(std::fabs(m.bpm - 142.0) <= 1.0); // pas 71
    OMNIS_CHECK(std::fabs(m.subHz - 55.0) <= 1.5);
    OMNIS_CHECK(m.keyRoot == 9 && m.keyMinor); // La mineur
    OMNIS_CHECK(m.correlation > 0.99 && m.lowCorrelation > 0.99 && std::fabs(m.monoLossDb) < 0.1);
    OMNIS_CHECK(m.onsetsPerSecond > 3.0); // les hats comptent
}

OMNIS_TEST("fiche: deux canaux sans rapport = corrélation nulle, perte en mono ; un master écrêté se voit") {
    std::mt19937 rng(9);
    std::normal_distribution<double> g(0.0, 0.1);
    std::vector<double> l(static_cast<std::size_t>(20 * kFs)), r(l.size());
    for (std::size_t i = 0; i < l.size(); ++i) {
        l[i] = g(rng);
        r[i] = g(rng);
    }
    const auto wide = analyzeMusic(l.data(), r.data(), static_cast<int>(l.size()), kFs);
    OMNIS_CHECK(std::fabs(wide.correlation) < 0.05);
    OMNIS_CHECK(std::fabs(wide.monoLossDb + 3.0) < 0.3);
    for (auto& x : l) x = std::clamp(x * 12.0, -0.999, 0.999);
    const auto clipped = analyzeMusic(l.data(), l.data(), static_cast<int>(l.size()), kFs);
    OMNIS_CHECK(clipped.clipPercent > 10.0);
    OMNIS_CHECK(clipped.samplePeakDb > -0.1);
}

OMNIS_TEST("fiche: écrite dans la base puis relue à l'identique (et une fiche ancienne reste lisible)") {
    ai::ReferenceEntry e;
    e.artist = "Test";
    e.title = "Fiche";
    e.signature.midDb.fill(60.0);
    e.signature.sideDb.fill(50.0);
    auto& m = e.signature.music;
    m.valid = true;
    m.seconds = 180.0;
    m.bpm = 142.5;
    m.keyRoot = 5;
    m.keyMinor = true;
    m.keyConfidence = 0.4;
    m.subHz = 44.0;
    m.shortTermLufs = {-12.0, -8.0, -6.0};
    m.microDb = {3.0, 5.0, 6.0, 8.0};
    m.correlation = 0.9;
    m.lowCorrelation = 0.99;
    m.monoLossDb = -0.4;
    m.onsetsPerSecond = 5.5;
    m.centroidHz = 4200.0;
    m.samplePeakDb = -0.3;
    m.clipPercent = 0.25;
    ai::ReferenceLibrary lib;
    lib.add(e);
    ai::ReferenceLibrary back;
    std::string error;
    OMNIS_CHECK(back.parse(lib.serialize(), &error));
    OMNIS_CHECK(error.empty());
    const auto& b = back.entries().front().signature.music;
    OMNIS_CHECK(b.valid && b.keyCode() == "Fm" && std::fabs(b.bpm - 142.5) < 1e-9 && std::fabs(b.subHz - 44.0) < 1e-9);
    OMNIS_CHECK(std::fabs(b.microDb[3] - 8.0) < 1e-9 && std::fabs(b.shortTermLufs[0] + 12.0) < 1e-9 && std::fabs(b.clipPercent - 0.25) < 1e-9);
    // Une fiche d'avant (sans les nouvelles clés) : lue, `music` non valide.
    ai::ReferenceLibrary old;
    OMNIS_CHECK(old.parse("[A - B]\nartiste = A\ntitre = B\nlufs = -8\nmid = " + std::string(21 * 3, ' ').replace(0, 0, "1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1") +
                          "\nside = 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1\n"));
    OMNIS_CHECK(!old.entries().front().signature.music.valid);
}

OMNIS_TEST("fiche: OMNIS s'en sert — une réf qui attaque plus donne du Punch, un grave mono le met mono") {
    ai::ReferenceLibrary lib;
    ai::ReferenceEntry ref;
    ref.artist = "Ref";
    ref.title = "Punchy";
    ref.genres = {"testgenre"};
    ref.signature.midDb.fill(60.0);
    ref.signature.sideDb.fill(48.0);
    ref.signature.integratedLufs = -8.0;
    ref.signature.activeFrames = 100;
    ref.signature.music.valid = true;
    ref.signature.music.microDb = {6.0, 8.0, 9.0, 10.0};
    ref.signature.music.lowCorrelation = 0.995;
    ref.signature.music.shortTermLufs = {-10.0, -8.0, -7.0};
    lib.add(ref);
    analysis::SpectralSignature prod = ref.signature;
    prod.music.microDb = {3.0, 4.0, 5.0, 6.0};
    prod.music.lowCorrelation = 0.80;
    const auto plan = ai::planAuto(prod, lib, "testgenre", [](params::ParamId) { return 0.0; });
    double punch = -1.0, mono = -1.0;
    for (const auto& [id, v] : plan.settings) {
        if (id == params::ParamId::LimPunch) punch = v;
        if (id == params::ParamId::MonoAmount) mono = v;
    }
    for (const auto& s : plan.said) std::printf("    %s\n", s.c_str());
    OMNIS_CHECK(punch > 40.0);
    OMNIS_CHECK(mono == 100.0);
}
