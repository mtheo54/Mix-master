// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestParameters.cpp — Table de paramètres.
// =============================================================================
#include "TestFramework.h"

#include <algorithm>
#include <cmath>

#include "params/Parameters.h"

#include <cmath>
#include <locale>
#include <set>
#include <string>

using namespace omnis::params;

OMNIS_TEST("params: identifiants et clés uniques, défauts dans les bornes") {
    std::set<std::uint32_t> ids;
    std::set<std::string> keys;
    for (const auto& p : allParameters()) {
        OMNIS_CHECK(ids.insert(static_cast<std::uint32_t>(p.id)).second);
        OMNIS_CHECK(keys.insert(std::string(p.key)).second);
        OMNIS_CHECK(p.defaultValue >= p.minValue && p.defaultValue <= p.maxValue);
        OMNIS_CHECK(p.maxValue > p.minValue);
        if (p.scale == ParamScale::Logarithmic) OMNIS_CHECK(p.minValue > 0.0 && p.stepCount == 0);
        if (p.kind == ParamKind::Choice || p.kind == ParamKind::Toggle)
            OMNIS_CHECK(static_cast<int>(p.choices.size()) == p.stepCount + 1);
    }
}

OMNIS_TEST("params: aller-retour normalisé <-> plain") {
    for (const auto& p : allParameters()) {
        for (double plain : {p.minValue, p.defaultValue, p.maxValue, 0.5 * (p.minValue + p.maxValue)}) {
            const double q = clampPlain(p, plain);
            // Précision relative du double : 1e-12 Hz à 300 Hz, 2e-12 à 8 kHz.
            OMNIS_CHECK_NEAR(toPlain(p, toNormalized(p, q)), q, 1e-12 * std::max(1.0, std::fabs(q)));
        }
    }
}

OMNIS_TEST("params: convention VST3 des valeurs discrètes") {
    const auto* monitor = findParameter(ParamId::Monitor);
    OMNIS_CHECK(monitor != nullptr);
    OMNIS_CHECK(toPlain(*monitor, 0.0) == 0.0);
    OMNIS_CHECK(toPlain(*monitor, 0.34) == 1.0);
    OMNIS_CHECK(toPlain(*monitor, 0.5) == 1.0);
    OMNIS_CHECK(toPlain(*monitor, 1.0) == 2.0);
    OMNIS_CHECK_NEAR(toNormalized(*monitor, 1.0), 0.5, 1e-12);
}

OMNIS_TEST("params: formatage et saisie texte") {
    const auto* gain = findParameter(ParamId::InputGain);
    OMNIS_CHECK(formatValue(*gain, -3.54) == "-3.5");
    OMNIS_CHECK(formatValue(*gain, -0.01) == "0.0");
    OMNIS_CHECK(formatValue(*gain, 6.0, true) == "6.0 dB");
    OMNIS_CHECK(parseValue(*gain, " -3.5 dB ").value_or(99) == -3.5);
    OMNIS_CHECK(parseValue(*gain, "+6").value_or(99) == 6.0);
    OMNIS_CHECK(parseValue(*gain, "100").value_or(99) == 24.0); // borné
    OMNIS_CHECK(!parseValue(*gain, "abc").has_value());
    OMNIS_CHECK(!parseValue(*gain, "3x").has_value());
    const auto* monitor = findParameter(ParamId::Monitor);
    OMNIS_CHECK(parseValue(*monitor, "side").value_or(-1) == 2.0);
    OMNIS_CHECK(formatValue(*monitor, 1.0) == "Mid");
    const auto* bypass = findParameterByKey("bypass");
    OMNIS_CHECK(bypass && parseValue(*bypass, "On").value_or(-1) == 1.0);
}

namespace {
struct CommaDecimal : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};
} // namespace

OMNIS_TEST("params: indépendant de la locale de l'hôte (virgule décimale)") {
    const std::locale previous = std::locale::global(std::locale(std::locale::classic(), new CommaDecimal));
    const auto* gain = findParameter(ParamId::OutputGain);
    OMNIS_CHECK(formatValue(*gain, -3.5) == "-3.5");
    OMNIS_CHECK(parseValue(*gain, "-3.5").value_or(99) == -3.5);
    std::locale::global(previous);
}

OMNIS_TEST("params: échelle logarithmique des fréquences") {
    const auto* mono = findParameter(ParamId::MonoFreq);
    OMNIS_CHECK(mono && mono->scale == ParamScale::Logarithmic);
    OMNIS_CHECK_NEAR(toPlain(*mono, 0.0), 20.0, 1e-9);
    OMNIS_CHECK_NEAR(toPlain(*mono, 1.0), 300.0, 1e-9);
    OMNIS_CHECK_NEAR(toPlain(*mono, 0.5), std::sqrt(20.0 * 300.0), 1e-9); // milieu = moyenne géométrique
    OMNIS_CHECK_NEAR(toNormalized(*mono, 120.0), std::log(6.0) / std::log(15.0), 1e-12);
}
