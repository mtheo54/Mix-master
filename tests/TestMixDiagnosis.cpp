// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestMixDiagnosis.cpp — Le diagnostic du mix voit ce que le master ne
//  rattrape pas, et ne fait de la 808 hors gamme qu'une remarque.
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"
#include "ai/MixDiagnosis.h"
#include "analysis/MusicDescriptors.h"
#include "analysis/SpectralSignature.h"

#include <cmath>
#include <numbers>

using namespace omnis;

namespace {
analysis::SpectralSignature hear(const std::vector<double>& l, const std::vector<double>& r, double fs) {
    auto s = analysis::analyzeSignature(l.data(), r.data(), static_cast<int>(l.size()), fs);
    s.music = analysis::analyzeMusic(l.data(), r.data(), static_cast<int>(l.size()), fs);
    return s;
}
bool has(const std::vector<ai::MixFinding>& f, const std::string& word, ai::MixFinding::Level level) {
    for (const auto& x : f)
        if (x.problem.find(word) != std::string::npos && x.level == level) return true;
    return false;
}
} // namespace

OMNIS_TEST("diagnostic: un mix qui écrête déjà, un grave en opposition de phase") {
    const double fs = 48000.0;
    auto loop = tools::signals::drumLoop(fs, 6);
    auto l = loop.channels[0], r = loop.channels[1];
    for (std::size_t i = 0; i < l.size(); ++i) {
        l[i] = std::clamp(l[i] * 4.0, -1.0, 1.0); // poussé dans le mur
        r[i] = std::clamp(r[i] * 4.0, -1.0, 1.0);
    }
    const auto hot = ai::diagnoseMix(hear(l, r, fs), nullptr);
    std::printf("%s", ai::formatDiagnosis(hot).c_str());
    OMNIS_CHECK(!hot.empty() && hot.front().level == ai::MixFinding::Level::Important);
    // Grave stéréo : sinus à 50 Hz en opposition entre les canaux.
    std::vector<double> a(static_cast<std::size_t>(6 * fs)), b(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double t = static_cast<double>(i) / fs;
        a[i] = 0.3 * std::sin(2.0 * std::numbers::pi * 50.0 * t) + 0.05 * std::sin(2.0 * std::numbers::pi * 1000.0 * t);
        b[i] = -0.3 * std::sin(2.0 * std::numbers::pi * 50.0 * t) + 0.05 * std::sin(2.0 * std::numbers::pi * 1000.0 * t);
    }
    const auto wide = ai::diagnoseMix(hear(a, b, fs), nullptr);
    OMNIS_CHECK(has(wide, "pas mono", ai::MixFinding::Level::Important));
}

OMNIS_TEST("diagnostic: une 808 hors gamme n'est qu'une remarque (parfois voulue)") {
    analysis::SpectralSignature s;
    s.music.valid = true;
    s.music.subHz = 46.25; // Fa#
    s.music.keyRoot = 7;   // Sol
    s.music.keyMinor = true;
    s.music.keyConfidence = 0.8;
    s.music.lowCorrelation = 1.0;
    s.music.samplePeakDb = -6.0;
    s.integratedLufs = -14.0;
    s.plrDb = 12.0;
    const auto f = ai::diagnoseMix(s, &s);
    OMNIS_CHECK(has(f, "hors de la gamme", ai::MixFinding::Level::Info));
    for (const auto& x : f) OMNIS_CHECK(x.level != ai::MixFinding::Level::Important);
    // Dans la gamme (Ré, quinte de Sol mineur) : rien à dire.
    s.music.subHz = 73.42;
    OMNIS_CHECK(!has(ai::diagnoseMix(s, &s), "hors de la gamme", ai::MixFinding::Level::Info));
}
