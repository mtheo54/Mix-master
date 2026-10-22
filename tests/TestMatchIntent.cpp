// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestMatchIntent.cpp — Signature, intention d'adaptation, demande.
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "ai/MatchIntent.h"
#include "analysis/SpectralSignature.h"
#include "stages/MatchEqStage.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using namespace omnis;
using analysis::analyzeSignature;
using analysis::SpectralSignature;

namespace {

constexpr double kFs = 48000.0;

SpectralSignature signatureOf(const std::vector<double>& l, const std::vector<double>& r) {
    return analyzeSignature(l.data(), r.data(), static_cast<int>(l.size()), kFs);
}

bool contains(const std::vector<std::string>& v, const char* word) {
    for (const auto& s : v)
        if (s.find(word) != std::string::npos) return true;
    return false;
}

/// Signature synthétique : plate, puis écarts par nœud.
SpectralSignature flatSignature() {
    SpectralSignature s;
    for (int k = 0; k < params::kMatchBands; ++k) {
        s.midDb[static_cast<std::size_t>(k)] = -20.0;
        s.sideDb[static_cast<std::size_t>(k)] = -32.0;
    }
    s.integratedLufs = -9.0;
    return s;
}

/// Recalcule l'énergie du sub d'une signature synthétique.
SpectralSignature& withSubEnergy(SpectralSignature& s) {
    double p = 0.0;
    for (int k = 0; k < SpectralSignature::kSubBands; ++k) p += std::pow(10.0, s.midDb[static_cast<std::size_t>(k)] / 10.0);
    s.subEnergyDb = 10.0 * std::log10(p);
    return s;
}

} // namespace

OMNIS_TEST("signature: bruit rose = balance plate (±0.6 dB de 28 Hz à 14 kHz)") {
    auto noise = tools::signals::pinkNoise(kFs, 20.0, -18.0, 7, true);
    const auto sig = signatureOf(noise.channels[0], noise.channels[1]);
    for (int k = 1; k < params::kMatchBands - 1; ++k) OMNIS_CHECK_NEAR(sig.relativeMidDb(k), 0.0, 0.6);
}

OMNIS_TEST("signature: le Match EQ appliqué se retrouve dans la signature (pente +1 dB/oct)") {
    auto noise = tools::signals::pinkNoise(kFs, 20.0, -24.0, 11, true);
    std::vector<double> l = noise.channels[0], r = noise.channels[1];
    const auto before = signatureOf(l, r);
    MatchEqStage st;
    st.prepare({kFs, 512, 2});
    for (int k = 0; k < params::kMatchBands; ++k) st.setParameter(params::matchMidId(k), 0.5 * (k - 10), true);
    for (std::size_t pos = 0; pos < l.size(); pos += 512) {
        double* ch[2] = {l.data() + pos, r.data() + pos};
        st.process(ch, static_cast<int>(std::min<std::size_t>(512, l.size() - pos)));
    }
    const auto after = signatureOf(l, r);
    const double slope = (after.relativeMidDb(16) - before.relativeMidDb(16)) - (after.relativeMidDb(6) - before.relativeMidDb(6));
    OMNIS_CHECK_NEAR(slope, 5.0, 0.3); // 10 nœuds = 5 octaves
}

OMNIS_TEST("signature: une autre note de 808 (41 vs 49 Hz) ne déclenche pas d'EQ (< 0.75 dB)") {
    const auto withSub = [](double f) {
        auto noise = tools::signals::pinkNoise(kFs, 10.0, -30.0, 3, true);
        for (std::size_t i = 0; i < noise.channels[0].size(); ++i) {
            const double s = 0.3 * std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i) / kFs);
            noise.channels[0][i] += s;
            noise.channels[1][i] += s;
        }
        return signatureOf(noise.channels[0], noise.channels[1]);
    };
    // Même mix, 808 dans une autre tonalité : l'intention ne doit presque rien
    // corriger (lissage large dans le grave + ressemblance partielle).
    const auto a = withSub(41.0), b = withSub(49.0);
    const auto intent = ai::planMatch(a, b, ai::MatchRequest{});
    for (int k = 0; k < params::kMatchBands; ++k) OMNIS_CHECK(std::fabs(intent.midDb[static_cast<std::size_t>(k)]) < 0.75);
}

OMNIS_TEST("signature: les silences ne comptent pas (moitié muette = même balance)") {
    auto noise = tools::signals::pinkNoise(kFs, 12.0, -18.0, 5, true);
    const auto full = signatureOf(noise.channels[0], noise.channels[1]);
    for (std::size_t i = noise.channels[0].size() / 2; i < noise.channels[0].size(); ++i)
        noise.channels[0][i] = noise.channels[1][i] = 0.0;
    const auto half = signatureOf(noise.channels[0], noise.channels[1]);
    for (int k = 1; k < params::kMatchBands - 1; ++k) OMNIS_CHECK_NEAR(half.midDb[static_cast<std::size_t>(k)], full.midDb[static_cast<std::size_t>(k)], 0.8);
}

OMNIS_TEST("intention: écart partiel, sub protégé (±3 dB), bande vide jamais boostée") {
    auto prod = flatSignature();
    auto ref = flatSignature();
    for (int k = 0; k <= 4; ++k) ref.midDb[static_cast<std::size_t>(k)] -= 10.0;   // sub 10 dB plus discret
    for (int k = 15; k <= 17; ++k) ref.midDb[static_cast<std::size_t>(k)] += 2.0;  // présence +2
    prod.midDb[20] = -70.0;                                                         // prod vide à 20 kHz
    ref.midDb[20] = -15.0;
    ref.integratedLufs = -6.0;
    ai::MatchRequest req;
    const auto intent = ai::planMatch(withSubEnergy(prod), withSubEnergy(ref), req);
    for (int k = 0; k <= 4; ++k) OMNIS_CHECK_NEAR(intent.midDb[static_cast<std::size_t>(k)], -3.0, 1e-9);
    // 50 % de l'écart relatif : +2 dB moins le décalage du corps (le nœud
    // 3.6 kHz, dans le corps 200 Hz–5 kHz, le relève de 2/9 dB).
    OMNIS_CHECK_NEAR(intent.midDb[16], 0.5 * (2.0 - 2.0 / 9.0), 0.02);
    OMNIS_CHECK(intent.midDb[20] <= 0.0);
    OMNIS_CHECK_NEAR(intent.targetLufs, -6.0, 1e-12);
    OMNIS_CHECK(contains(intent.referenceTraits, "Sub"));
    OMNIS_CHECK(contains(intent.protectedTraits, "Sub"));
    // Sous le Mono Maker, le Side suit exactement le Mid.
    OMNIS_CHECK_NEAR(intent.sideDb[2], intent.midDb[2], 1e-12);
}

OMNIS_TEST("intention: jamais de copie (ressemblance bornée à 85 %)") {
    auto prod = flatSignature();
    auto ref = flatSignature();
    for (int k = 11; k <= 14; ++k) ref.midDb[static_cast<std::size_t>(k)] += 3.0;
    ai::MatchRequest req;
    req.resemblance = 1.0;
    const auto intent = ai::planMatch(withSubEnergy(prod), withSubEnergy(ref), req);
    OMNIS_CHECK(intent.midDb[12] < 0.86 * 3.0);
}

OMNIS_TEST("demande: 808 protégée, sonie explicite, brillance, ressemblance") {
    const auto a = ai::parseRequest("Comme la réf mais garde ma 808, très fort");
    OMNIS_CHECK_NEAR(a.subLimitDb, 1.0, 1e-12);
    OMNIS_CHECK_NEAR(a.resemblance, 0.5, 1e-12);
    OMNIS_CHECK(!a.targetLufs.has_value());

    // « un peu » porte sur « brillant » (air +2 × 0.5), pas sur la ressemblance.
    const auto b = ai::parseRequest("un peu plus brillant, -8 LUFS, propre");
    OMNIS_CHECK_NEAR(b.resemblance, 0.5, 1e-12);
    OMNIS_CHECK(b.targetLufs.has_value() && std::fabs(*b.targetLufs + 8.0) < 1e-12);
    OMNIS_CHECK_NEAR(b.midBiasDb[19], 1.0, 1e-9);
    OMNIS_CHECK_NEAR(b.maxTonalDeviationDb, 1.0, 1e-12);

    const auto c = ai::parseRequest("EXACTEMENT pareil que poster boy, -6,5 lufs, sale");
    OMNIS_CHECK_NEAR(c.resemblance, 0.8, 1e-12);
    OMNIS_CHECK(c.targetLufs.has_value() && std::fabs(*c.targetLufs + 6.5) < 1e-12);
    OMNIS_CHECK_NEAR(c.maxTonalDeviationDb, 3.0, 1e-12);
}
