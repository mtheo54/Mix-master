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
constexpr std::array<std::string_view, 3> kClipModeLabels{"Hard", "Soft Knee", "Diode"};
constexpr std::array<std::string_view, 5> kOversamplingLabels{"1x", "2x", "4x", "8x", "16x"};
constexpr ParamScale kLog = ParamScale::Logarithmic;

// clang-format off
constexpr std::array<ParamSpec, 73> kParameters{{
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

    // Match EQ (docs/CALIBRATION_MODULE4.md). Pas de lissage par paramètre : un
    // changement de courbe est fondu sur une trame de convolution.
    {ParamId::MatchAmount,       "match_amount",  "Match Amount",         "Match",  "%",    ParamKind::Continuous, 0.0,    100.0, 100.0, 0,    {},             0.0,    0},
    {static_cast<ParamId>(210), "match_m_20",    "Match Mid 20",        "M20",    "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(211), "match_m_28",    "Match Mid 28",        "M28",    "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(212), "match_m_40",    "Match Mid 40",        "M40",    "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(213), "match_m_57",    "Match Mid 57",        "M57",    "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(214), "match_m_80",    "Match Mid 80",        "M80",    "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(215), "match_m_113",   "Match Mid 113",       "M113",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(216), "match_m_160",   "Match Mid 160",       "M160",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(217), "match_m_226",   "Match Mid 226",       "M226",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(218), "match_m_320",   "Match Mid 320",       "M320",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(219), "match_m_453",   "Match Mid 453",       "M453",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(220), "match_m_640",   "Match Mid 640",       "M640",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(221), "match_m_905",   "Match Mid 905",       "M905",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(222), "match_m_1280",  "Match Mid 1.3k",      "M1.3k",  "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(223), "match_m_1810",  "Match Mid 1.8k",      "M1.8k",  "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(224), "match_m_2560",  "Match Mid 2.6k",      "M2.6k",  "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(225), "match_m_3620",  "Match Mid 3.6k",      "M3.6k",  "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(226), "match_m_5120",  "Match Mid 5.1k",      "M5.1k",  "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(227), "match_m_7241",  "Match Mid 7.2k",      "M7.2k",  "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(228), "match_m_10240", "Match Mid 10k",       "M10k",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(229), "match_m_14482", "Match Mid 14k",       "M14k",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(230), "match_m_20480", "Match Mid 20k",       "M20k",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(250), "match_s_20",    "Match Side 20",       "S20",    "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(251), "match_s_28",    "Match Side 28",       "S28",    "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(252), "match_s_40",    "Match Side 40",       "S40",    "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(253), "match_s_57",    "Match Side 57",       "S57",    "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(254), "match_s_80",    "Match Side 80",       "S80",    "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(255), "match_s_113",   "Match Side 113",      "S113",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(256), "match_s_160",   "Match Side 160",      "S160",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(257), "match_s_226",   "Match Side 226",      "S226",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(258), "match_s_320",   "Match Side 320",      "S320",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(259), "match_s_453",   "Match Side 453",      "S453",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(260), "match_s_640",   "Match Side 640",      "S640",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(261), "match_s_905",   "Match Side 905",      "S905",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(262), "match_s_1280",  "Match Side 1.3k",     "S1.3k",  "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(263), "match_s_1810",  "Match Side 1.8k",     "S1.8k",  "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(264), "match_s_2560",  "Match Side 2.6k",     "S2.6k",  "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(265), "match_s_3620",  "Match Side 3.6k",     "S3.6k",  "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(266), "match_s_5120",  "Match Side 5.1k",     "S5.1k",  "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(267), "match_s_7241",  "Match Side 7.2k",     "S7.2k",  "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(268), "match_s_10240", "Match Side 10k",      "S10k",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(269), "match_s_14482", "Match Side 14k",      "S14k",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},
    {static_cast<ParamId>(270), "match_s_20480", "Match Side 20k",      "S20k",   "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             0.0,    1},

    // Clipper (calibration : docs/CALIBRATION_MODULE3.md).
    {ParamId::ClipMode,          "clip_mode",     "Clip Mode",            "ClipM",  "",     ParamKind::Choice,     0.0,    2.0,   1.0,   2,    kClipModeLabels, 20.0,  0},
    {ParamId::ClipDrive,         "clip_drive",    "Clip Drive",           "Drive",  "dB",   ParamKind::Continuous, 0.0,    24.0,  0.0,   0,    {},             30.0,   1},
    {ParamId::ClipCeiling,       "clip_ceiling",  "Clip Ceiling",         "ClipC",  "dBFS", ParamKind::Continuous, -12.0,  0.0,   0.0,   0,    {},             30.0,   1},
    {ParamId::ClipKnee,          "clip_knee",     "Clip Knee",            "Knee",   "%",    ParamKind::Continuous, 0.0,    100.0, 50.0,  0,    {},             30.0,   0},
    {ParamId::ClipOversampling,  "clip_os",       "Clip Oversampling",    "OS",     "",     ParamKind::Choice,     0.0,    4.0,   3.0,   4,    kOversamplingLabels, 0.0, 0},
    {ParamId::ClipSplit,         "clip_split",    "Clip Split",           "Split",  "Hz",   ParamKind::Continuous, 60.0,   300.0, 120.0, 0,    {},             50.0,   0, kLog},
    {ParamId::ClipSubDrive,      "clip_sub_drive","Clip Sub Drive",       "SubDrv", "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             30.0,   1},
    {ParamId::ClipSubRoom,       "clip_sub_room", "Clip Sub Make-Room",   "Room",   "%",    ParamKind::Continuous, 0.0,    100.0, 0.0, 0,    {},             30.0,   0},

    // Limiteur.
    {ParamId::LimGain,           "lim_gain",      "Limiter Gain",         "LGain",  "dB",   ParamKind::Continuous, 0.0,    24.0,  0.0,   0,    {},             30.0,   1},
    {ParamId::LimCeiling,        "lim_ceiling",   "Limiter Ceiling",      "Ceil",   "dB",   ParamKind::Continuous, -6.0,   0.0,   -1.0,  0,    {},             30.0,   1},
    {ParamId::LimRelease,        "lim_release",   "Limiter Release",      "Rel",    "ms",   ParamKind::Continuous, 1.0,    1000.0, 60.0, 0,    {},             0.0,    0, kLog},
    {ParamId::LimAutoRelease,    "lim_auto_release","Limiter Auto Release","Auto", "%",    ParamKind::Continuous, 0.0,    100.0, 100.0, 0,    {},             0.0,    0},

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

double matchBandHz(int band) noexcept { return 20.0 * std::exp2(0.5 * band); }

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
