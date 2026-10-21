// =============================================================================
//  Abrasion Labs // OMNIS
//  params/Parameters.cpp
// =============================================================================
#include "Parameters.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace omnis::params {

namespace {

constexpr std::array<std::string_view, 2> kToggleLabels{"Off", "On"};
constexpr std::array<std::string_view, 3> kMonitorLabels{"Stereo", "Mid", "Side"};
constexpr std::array<std::string_view, 2> kSlopeLabels{"24 dB/oct", "48 dB/oct"};
constexpr std::array<std::string_view, 2> kPhaseLabels{"Linear", "Analog"};
constexpr ParamScale kLog = ParamScale::Logarithmic;

// clang-format off
constexpr std::array<ParamSpec, 18> kParameters{{
    // id                         key              name                    short     unit    kind                  min     max    def    steps choices         smooth  dec
    {ParamId::Bypass,            "bypass",        "Bypass",               "Byp",    "",     ParamKind::Toggle,     0.0,    1.0,   0.0,   1,    kToggleLabels,  20.0,   0},
    {ParamId::InputGain,         "input_gain",    "Input Gain",           "In",     "dB",   ParamKind::Continuous, -24.0,  24.0,  0.0,   0,    {},             30.0,   1},
    {ParamId::OutputGain,        "output_gain",   "Output Gain",          "Out",    "dB",   ParamKind::Continuous, -24.0,  24.0,  0.0,   0,    {},             30.0,   1},
    {ParamId::Monitor,           "monitor",       "Monitor",              "Mon",    "",     ParamKind::Choice,     0.0,    2.0,   0.0,   2,    kMonitorLabels, 15.0,   0},

    // Sub Control & Mono Maker — défauts calibrés sur les mixes de référence
    // (voir docs/CALIBRATION_MODULE2.md).
    {ParamId::LowCutFreq,        "low_cut",       "Low Cut",              "LoCut",  "Hz",   ParamKind::Continuous, 5.0,    60.0,  16.0,  0,    {},             50.0,   1, kLog},
    {ParamId::MonoFreq,          "mono_freq",     "Mono Below",           "Mono",   "Hz",   ParamKind::Continuous, 20.0,   300.0, 120.0, 0,    {},             50.0,   0, kLog},
    {ParamId::MonoAmount,        "mono_amount",   "Mono Amount",          "MonoAm", "%",    ParamKind::Continuous, 0.0,    100.0, 100.0, 0,    {},             30.0,   0},
    {ParamId::MonoSlope,         "mono_slope",    "Mono Slope",           "Slope",  "",     ParamKind::Choice,     0.0,    1.0,   0.0,   1,    kSlopeLabels,   30.0,   0},
    {ParamId::SubGain,           "sub_gain",      "Sub Gain",             "Sub",    "dB",   ParamKind::Continuous, -12.0,  6.0,   0.0,   0,    {},             30.0,   1},
    {ParamId::SubFreq,           "sub_freq",      "Sub Freq",             "SubF",   "Hz",   ParamKind::Continuous, 30.0,   150.0, 70.0,  0,    {},             50.0,   0, kLog},
    {ParamId::SubSynth,          "sub_synth",     "Sub Synth",            "Synth",  "%",    ParamKind::Continuous, 0.0,    100.0, 0.0,   0,    {},             30.0,   0},
    {ParamId::SubPhaseMode,      "sub_phase",     "Sub Phase",            "Phase",  "",     ParamKind::Choice,     0.0,    1.0,   0.0,   1,    kPhaseLabels,   30.0,   0},

    {ParamId::MeterMomentary,    "meter_m",       "Momentary Loudness",   "M",      "LUFS", ParamKind::Meter,      -80.0,  10.0,  -80.0, 0,    {},             0.0,    1},
    {ParamId::MeterShortTerm,    "meter_s",       "Short-Term Loudness",  "S",      "LUFS", ParamKind::Meter,      -80.0,  10.0,  -80.0, 0,    {},             0.0,    1},
    {ParamId::MeterIntegrated,   "meter_i",       "Integrated Loudness",  "I",      "LUFS", ParamKind::Meter,      -80.0,  10.0,  -80.0, 0,    {},             0.0,    1},
    {ParamId::MeterTruePeak,     "meter_tp",      "True Peak",            "TP",     "dBTP", ParamKind::Meter,      -80.0,  12.0,  -80.0, 0,    {},             0.0,    1},
    {ParamId::MeterCorrelation,  "meter_corr",    "Phase Correlation",    "Corr",   "",     ParamKind::Meter,      -1.0,   1.0,   0.0,   0,    {},             0.0,    2},
    {ParamId::MeterLoudnessRange,"meter_lra",     "Loudness Range",       "LRA",    "LU",   ParamKind::Meter,      0.0,    40.0,  0.0,   0,    {},             0.0,    1},
}};
// clang-format on

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// Formatage et analyse en locale « C » forcée : un hôte peut avoir appelé
// setlocale() (ex. fr_FR -> virgule décimale), ce qui casserait snprintf /
// strtod et rendrait l'affichage incohérent avec la saisie. std::from_chars
// serait idéal mais n'existe pour les flottants qu'à partir de macOS 13.3.
std::optional<double> parseNumber(std::string_view s) {
    std::istringstream in{std::string(s)};
    in.imbue(std::locale::classic());
    double value = 0.0;
    in >> value;
    if (in.fail()) return std::nullopt;
    // L'unité a déjà été retirée : il ne doit rien rester après le nombre.
    in >> std::ws;
    if (!in.eof()) return std::nullopt;
    return value;
}

} // namespace

std::span<const ParamSpec> allParameters() noexcept { return kParameters; }

const ParamSpec* findParameter(ParamId id) noexcept {
    for (const auto& p : kParameters)
        if (p.id == id) return &p;
    return nullptr;
}

const ParamSpec* findParameter(std::uint32_t id) noexcept { return findParameter(static_cast<ParamId>(id)); }

const ParamSpec* findParameterByKey(std::string_view key) noexcept {
    for (const auto& p : kParameters)
        if (p.key == key) return &p;
    return nullptr;
}

int parameterIndex(ParamId id) noexcept {
    for (std::size_t i = 0; i < kParameters.size(); ++i)
        if (kParameters[i].id == id) return static_cast<int>(i);
    return -1;
}

double clampPlain(const ParamSpec& spec, double plain) noexcept {
    if (!(plain == plain)) return spec.defaultValue; // NaN
    plain = std::clamp(plain, spec.minValue, spec.maxValue);
    if (spec.stepCount > 0) plain = std::round(plain);
    return plain;
}

double toPlain(const ParamSpec& spec, double normalized) noexcept {
    normalized = std::clamp(normalized, 0.0, 1.0);
    if (spec.stepCount > 0) {
        const double step = std::min(static_cast<double>(spec.stepCount),
                                     std::floor(normalized * (spec.stepCount + 1)));
        return spec.minValue + step * (spec.maxValue - spec.minValue) / spec.stepCount;
    }
    if (spec.scale == ParamScale::Logarithmic)
        return spec.minValue * std::pow(spec.maxValue / spec.minValue, normalized);
    return spec.minValue + normalized * (spec.maxValue - spec.minValue);
}

double toNormalized(const ParamSpec& spec, double plain) noexcept {
    const double range = spec.maxValue - spec.minValue;
    if (range <= 0.0) return 0.0;
    if (spec.scale == ParamScale::Logarithmic && spec.stepCount == 0)
        return std::clamp(std::log(clampPlain(spec, plain) / spec.minValue) / std::log(spec.maxValue / spec.minValue),
                          0.0, 1.0);
    return std::clamp((clampPlain(spec, plain) - spec.minValue) / range, 0.0, 1.0);
}

std::string formatValue(const ParamSpec& spec, double plain, bool withUnit) {
    if (spec.kind == ParamKind::Toggle || spec.kind == ParamKind::Choice) {
        const auto index = static_cast<std::size_t>(clampPlain(spec, plain) - spec.minValue);
        if (index < spec.choices.size()) return std::string(spec.choices[index]);
    }
    if (spec.kind == ParamKind::Meter && plain <= spec.minValue) return withUnit ? "-inf " + std::string(spec.unit) : "-inf";

    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(spec.displayDecimals) << plain;
    std::string text = out.str();
    if (text == "-0" || text.rfind("-0.", 0) == 0) {
        // Évite l'affichage « -0.0 dB » pour les petites valeurs négatives.
        bool allZero = true;
        for (char c : text)
            if (c >= '1' && c <= '9') allZero = false;
        if (allZero) text.erase(0, 1);
    }
    if (withUnit && !spec.unit.empty()) {
        text += ' ';
        text += spec.unit;
    }
    return text;
}

std::optional<double> parseValue(const ParamSpec& spec, std::string_view text) {
    text = trim(text);
    if (text.empty()) return std::nullopt;

    if (spec.kind == ParamKind::Toggle || spec.kind == ParamKind::Choice) {
        for (std::size_t i = 0; i < spec.choices.size(); ++i)
            if (equalsIgnoreCase(text, spec.choices[i])) return spec.minValue + static_cast<double>(i);
        if (spec.kind == ParamKind::Toggle) {
            if (equalsIgnoreCase(text, "true") || equalsIgnoreCase(text, "yes")) return 1.0;
            if (equalsIgnoreCase(text, "false") || equalsIgnoreCase(text, "no")) return 0.0;
        }
    }

    // Retire l'unité éventuelle en suffixe.
    if (!spec.unit.empty() && text.size() >= spec.unit.size()) {
        const auto suffix = text.substr(text.size() - spec.unit.size());
        if (equalsIgnoreCase(suffix, spec.unit)) text = trim(text.substr(0, text.size() - spec.unit.size()));
    }
    const auto value = parseNumber(text);
    if (!value) return std::nullopt;
    return clampPlain(spec, *value);
}

} // namespace omnis::params
