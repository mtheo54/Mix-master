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
constexpr std::array<std::string_view, 3> kLinkRoleLabels{"Seul", "Stem", "Master"};
// Même ordre que stems::Element (vérifié par les tests) : Auto, puis les 21 éléments.
constexpr std::array<std::string_view, 22> kStemKindLabels{
    "Auto",           "Kick",          "Snare",          "Clap",            "Hi-hat",  "Open hat",        "Cymbale",
    "Percu",          "Drums (boucle)", "808",           "Basse",           "Piano / clavier", "Guitare", "Cuivres / vents",
    "Cordes",         "Nappe",         "Lead / synthé",  "Pluck / cloche",  "Voix lead", "Backs / adlibs", "FX", "Autre"};
// Afficheur « élément reconnu » : « — » tant que rien n'est reconnu.
constexpr std::array<std::string_view, 22> kElementMeterLabels{
    "—",              "Kick",          "Snare",          "Clap",            "Hi-hat",  "Open hat",        "Cymbale",
    "Percu",          "Drums (boucle)", "808",           "Basse",           "Piano / clavier", "Guitare", "Cuivres / vents",
    "Cordes",         "Nappe",         "Lead / synthé",  "Pluck / cloche",  "Voix lead", "Backs / adlibs", "FX", "Autre"};
constexpr std::array<std::string_view, 8> kGroupLabels{"1", "2", "3", "4", "5", "6", "7", "8"};
constexpr std::array<std::string_view, 3> kStemModeLabels{"Off", "Auto", "Manuel"};
constexpr std::array<std::string_view, 8> kDeltaLabels{"Off", "Sub", "Match EQ", "Inflator", "Imager", "Multiband", "Clipper", "Limiter"};
constexpr ParamScale kLog = ParamScale::Logarithmic;

// clang-format off
constexpr std::array<ParamSpec, kParameterCount> kParameters{{
    // id                         key              name                    short     unit    kind                  min     max    def    steps choices         smooth  dec
    {ParamId::Bypass,            "bypass",        "Bypass",               "Byp",    "",     ParamKind::Toggle,     0.0,    1.0,   0.0,   1,    kToggleLabels,  20.0,   0},
    {ParamId::InputGain,         "input_gain",    "Input Gain",           "In",     "dB",   ParamKind::Continuous, -24.0,  24.0,  0.0,   0,    {},             30.0,   1},
    {ParamId::OutputGain,        "output_gain",   "Output Gain",          "Out",    "dB",   ParamKind::Continuous, -24.0,  24.0,  0.0,   0,    {},             30.0,   1},
    {ParamId::Monitor,           "monitor",       "Monitor",              "Mon",    "",     ParamKind::Choice,     0.0,    2.0,   0.0,   2,    kMonitorLabels, 15.0,   0},
    {ParamId::RefCapture,        "ref_capture",   "Listen Reference",     "Ref",    "",     ParamKind::Toggle,     0.0,    1.0,   0.0,   1,    kToggleLabels,  0.0,    0},
    // Instances liées (link/LinkBus.h) : un master et des stems d'un même groupe.
    {ParamId::LinkRole,          "link_role",     "Link Role",            "Role",   "",     ParamKind::Choice,     0.0,    2.0,   0.0,   2,    kLinkRoleLabels, 0.0,   0},
    // Auto : OMNIS écoute la piste (et lit son nom) pour reconnaître l'élément.
    {ParamId::LinkStemKind,      "link_stem",     "Stem Element",         "Elem",   "",     ParamKind::Choice,     0.0,    21.0,  0.0,   21,   kStemKindLabels, 0.0,   0},
    {ParamId::LinkGroup,         "link_group",    "Link Group",           "Group",  "",     ParamKind::Choice,     1.0,    8.0,   1.0,   7,    kGroupLabels,   0.0,    0},
    // Rack : ordre des modules (réglé par l'interface, pas d'automation).
    {ParamId::ChainOrder,        "chain_order",   "Chain Order",          "Order",  "",     ParamKind::Choice,     0.0,    5039.0, 0.0,  5039, {},             0.0,    0, ParamScale::Linear, true, false},
    // Outils d'écoute : jamais gardés (un export oublié en Match serait faux).
    {ParamId::GainMatch,         "gain_match",    "Gain Match",           "Match",  "",     ParamKind::Toggle,     0.0,    1.0,   0.0,   1,    kToggleLabels,  0.0,    0, ParamScale::Linear, false, false},
    {ParamId::DeltaListen,       "delta",         "Delta Listen",         "Delta",  "",     ParamKind::Choice,     0.0,    7.0,   0.0,   7,    kDeltaLabels,   0.0,    0, ParamScale::Linear, false, false},

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
    {ParamId::SubOn,             "sub_on",        "Sub Module",           "SubOn",  "",     ParamKind::Toggle,     0.0,    1.0,   1.0,   1,    kToggleLabels,  20.0,   0},

    // Match EQ (docs/CALIBRATION_MODULE4.md). Pas de lissage par paramètre : un
    // changement de courbe est fondu sur une trame de convolution.
    {ParamId::MatchAmount,       "match_amount",  "Match Amount",         "Match",  "%",    ParamKind::Continuous, 0.0,    100.0, 100.0, 0,    {},             0.0,    0},
    {ParamId::MatchTilt,         "match_tilt",    "Tilt",                 "Tilt",   "dB",   ParamKind::Continuous, -6.0,   6.0,   0.0,   0,    {},             0.0,    1},
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
    {ParamId::MatchOn,           "match_on",      "Match EQ Module",      "MatchOn","",     ParamKind::Toggle,     0.0,    1.0,   1.0,   1,    kToggleLabels,  20.0,   0},

    // Inflator (étage 3) : neutre par défaut (effets à 0 %, sortie = entrée).
    {ParamId::InflLowEffect,     "infl_low",      "Inflate 808",          "Inf808", "%",    ParamKind::Continuous, 0.0,    100.0, 0.0,   0,    {},             30.0,   0},
    {ParamId::InflHighEffect,    "infl_high",     "Inflate Mix",          "InfMix", "%",    ParamKind::Continuous, 0.0,    100.0, 0.0,   0,    {},             30.0,   0},
    {ParamId::InflDrive,         "infl_drive",    "Inflate Drive",        "InfDrv", "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             30.0,   1},
    {ParamId::InflCurve,         "infl_curve",    "Inflate Curve",        "InfCrv", "",     ParamKind::Continuous, -50.0,  50.0,  0.0,   0,    {},             30.0,   0},
    {ParamId::InflWarmth,        "infl_warmth",   "Inflate Warmth",       "Warmth", "%",    ParamKind::Continuous, 0.0,    100.0, 0.0,   0,    {},             30.0,   0},
    {ParamId::InflOutput,        "infl_output",   "Inflate Output",       "InfOut", "dB",   ParamKind::Continuous, -12.0,  6.0,   0.0,   0,    {},             30.0,   1},
    {ParamId::InflSplit,         "infl_split",    "Inflate Split",        "InfSpl", "Hz",   ParamKind::Continuous, 60.0,   300.0, 120.0, 0,    {},             50.0,   0, kLog},
    // 8x par défaut : avec du drive, le coude de la courbe (|u| = 1) replie à
    // −48 dB sous la fondamentale en 4x (sinus de 13 kHz, drive +12 dB), −76 dB en 8x.
    {ParamId::InflOversampling,  "infl_os",       "Inflate Oversampling", "InfOS",  "",     ParamKind::Choice,     0.0,    4.0,   3.0,   4,    kOversamplingLabels, 0.0, 0},
    {ParamId::InflOn,            "infl_on",       "Inflator Module",      "InflOn", "",     ParamKind::Toggle,     0.0,    1.0,   1.0,   1,    kToggleLabels,  20.0,   0},

    // Multibande (docs/CALIBRATION_MODULE4.md §6). Défauts neutres : ratio 1,
    // compensation 0 -> l'étage est un retard pur.
    {ParamId::MbSplitLow,        "mb_split_low",  "MB Split Low",         "MbLo",   "Hz",   ParamKind::Continuous, 60.0,   250.0, 120.0, 0,    {},             50.0,   0, kLog},
    {ParamId::MbSplitMid,        "mb_split_mid",  "MB Split Mid",         "MbMid",  "Hz",   ParamKind::Continuous, 250.0,  2500.0, 800.0, 0,   {},             50.0,   0, kLog},
    {ParamId::MbSplitHigh,       "mb_split_high", "MB Split High",        "MbHi",   "Hz",   ParamKind::Continuous, 2500.0, 12000.0, 5000.0, 0, {},             50.0,   0, kLog},
    {static_cast<ParamId>(410), "mb_sub_thr",    "MB Sub Threshold",    "SThr",   "dBFS", ParamKind::Continuous, -60.0, 0.0, 0.0, 0, {}, 30.0, 1},
    {static_cast<ParamId>(411), "mb_sub_ratio",  "MB Sub Ratio",        "SRat",   ":1",   ParamKind::Continuous, 1.0, 20.0, 1.0, 0, {}, 30.0, 2, kLog},
    {static_cast<ParamId>(412), "mb_sub_attack", "MB Sub Attack",       "SAtt",   "ms",   ParamKind::Continuous, 1.0, 300.0, 30.0, 0, {}, 0.0, 0, kLog},
    {static_cast<ParamId>(413), "mb_sub_release","MB Sub Release",      "SRel",   "ms",   ParamKind::Continuous, 10.0, 3000.0, 250.0, 0, {}, 0.0, 0, kLog},
    {static_cast<ParamId>(414), "mb_sub_makeup", "MB Sub Makeup",       "SMk",    "dB",   ParamKind::Continuous, -12.0, 12.0, 0.0, 0, {}, 30.0, 1},
    {static_cast<ParamId>(420), "mb_low_thr",    "MB Low Threshold",    "LThr",   "dBFS", ParamKind::Continuous, -60.0, 0.0, 0.0, 0, {}, 30.0, 1},
    {static_cast<ParamId>(421), "mb_low_ratio",  "MB Low Ratio",        "LRat",   ":1",   ParamKind::Continuous, 1.0, 20.0, 1.0, 0, {}, 30.0, 2, kLog},
    {static_cast<ParamId>(422), "mb_low_attack", "MB Low Attack",       "LAtt",   "ms",   ParamKind::Continuous, 1.0, 300.0, 20.0, 0, {}, 0.0, 0, kLog},
    {static_cast<ParamId>(423), "mb_low_release","MB Low Release",      "LRel",   "ms",   ParamKind::Continuous, 10.0, 3000.0, 200.0, 0, {}, 0.0, 0, kLog},
    {static_cast<ParamId>(424), "mb_low_makeup", "MB Low Makeup",       "LMk",    "dB",   ParamKind::Continuous, -12.0, 12.0, 0.0, 0, {}, 30.0, 1},
    {static_cast<ParamId>(430), "mb_mid_thr",    "MB Mid Threshold",    "MThr",   "dBFS", ParamKind::Continuous, -60.0, 0.0, 0.0, 0, {}, 30.0, 1},
    {static_cast<ParamId>(431), "mb_mid_ratio",  "MB Mid Ratio",        "MRat",   ":1",   ParamKind::Continuous, 1.0, 20.0, 1.0, 0, {}, 30.0, 2, kLog},
    {static_cast<ParamId>(432), "mb_mid_attack", "MB Mid Attack",       "MAtt",   "ms",   ParamKind::Continuous, 1.0, 300.0, 10.0, 0, {}, 0.0, 0, kLog},
    {static_cast<ParamId>(433), "mb_mid_release","MB Mid Release",      "MRel",   "ms",   ParamKind::Continuous, 10.0, 3000.0, 150.0, 0, {}, 0.0, 0, kLog},
    {static_cast<ParamId>(434), "mb_mid_makeup", "MB Mid Makeup",       "MMk",    "dB",   ParamKind::Continuous, -12.0, 12.0, 0.0, 0, {}, 30.0, 1},
    {static_cast<ParamId>(440), "mb_high_thr",   "MB High Threshold",   "HThr",   "dBFS", ParamKind::Continuous, -60.0, 0.0, 0.0, 0, {}, 30.0, 1},
    {static_cast<ParamId>(441), "mb_high_ratio", "MB High Ratio",       "HRat",   ":1",   ParamKind::Continuous, 1.0, 20.0, 1.0, 0, {}, 30.0, 2, kLog},
    {static_cast<ParamId>(442), "mb_high_attack","MB High Attack",      "HAtt",   "ms",   ParamKind::Continuous, 1.0, 300.0, 5.0, 0, {}, 0.0, 0, kLog},
    {static_cast<ParamId>(443), "mb_high_release","MB High Release",     "HRel",   "ms",   ParamKind::Continuous, 10.0, 3000.0, 100.0, 0, {}, 0.0, 0, kLog},
    {static_cast<ParamId>(444), "mb_high_makeup","MB High Makeup",      "HMk",    "dB",   ParamKind::Continuous, -12.0, 12.0, 0.0, 0, {}, 30.0, 1},
    {ParamId::MbOn,              "mb_on",         "Multiband Module",     "MbOn",   "",     ParamKind::Toggle,     0.0,    1.0,   1.0,   1,    kToggleLabels,  20.0,   0},

    // Clipper (calibration : docs/CALIBRATION_MODULE3.md).
    {ParamId::ClipMode,          "clip_mode",     "Clip Mode",            "ClipM",  "",     ParamKind::Choice,     0.0,    2.0,   1.0,   2,    kClipModeLabels, 20.0,  0},
    {ParamId::ClipDrive,         "clip_drive",    "Clip Drive",           "Drive",  "dB",   ParamKind::Continuous, 0.0,    24.0,  0.0,   0,    {},             30.0,   1},
    {ParamId::ClipCeiling,       "clip_ceiling",  "Clip Ceiling",         "ClipC",  "dBFS", ParamKind::Continuous, -12.0,  0.0,   0.0,   0,    {},             30.0,   1},
    {ParamId::ClipKnee,          "clip_knee",     "Clip Knee",            "Knee",   "%",    ParamKind::Continuous, 0.0,    100.0, 50.0,  0,    {},             30.0,   0},
    {ParamId::ClipOversampling,  "clip_os",       "Clip Oversampling",    "OS",     "",     ParamKind::Choice,     0.0,    4.0,   3.0,   4,    kOversamplingLabels, 0.0, 0},
    {ParamId::ClipSplit,         "clip_split",    "Clip Split",           "Split",  "Hz",   ParamKind::Continuous, 60.0,   300.0, 120.0, 0,    {},             50.0,   0, kLog},
    {ParamId::ClipSubDrive,      "clip_sub_drive","Clip Sub Drive",       "SubDrv", "dB",   ParamKind::Continuous, -12.0,  12.0,  0.0,   0,    {},             30.0,   1},
    {ParamId::ClipSubRoom,       "clip_sub_room", "Clip Sub Make-Room",   "Room",   "%",    ParamKind::Continuous, 0.0,    100.0, 0.0, 0,    {},             30.0,   0},
    {ParamId::ClipOn,            "clip_on",       "Clipper Module",       "ClipOn", "",     ParamKind::Toggle,     0.0,    1.0,   1.0,   1,    kToggleLabels,  20.0,   0},

    // Limiteur.
    {ParamId::LimGain,           "lim_gain",      "Limiter Gain",         "LGain",  "dB",   ParamKind::Continuous, 0.0,    24.0,  0.0,   0,    {},             30.0,   1},
    {ParamId::LimCeiling,        "lim_ceiling",   "Limiter Ceiling",      "Ceil",   "dB",   ParamKind::Continuous, -6.0,   0.0,   -1.0,  0,    {},             30.0,   1},
    {ParamId::LimRelease,        "lim_release",   "Limiter Release",      "Rel",    "ms",   ParamKind::Continuous, 1.0,    1000.0, 60.0, 0,    {},             0.0,    0, kLog},
    {ParamId::LimAutoRelease,    "lim_auto_release","Limiter Auto Release","Auto", "%",    ParamKind::Continuous, 0.0,    100.0, 100.0, 0,    {},             0.0,    0},
    // Punch : attaques accentuées avant la limitation (le soutenu juste derrière est plus bas).
    {ParamId::LimPunch,          "lim_punch",     "Limiter Punch",        "Punch",  "%",    ParamKind::Continuous, 0.0,    100.0, 0.0,   0,    {},             30.0,   0},
    // Monte les passages calmes ; les crêtes au plafond ne bougent pas.
    {ParamId::LimUpward,         "lim_upward",    "Limiter Upward",       "Upward", "dB",   ParamKind::Continuous, 0.0,    12.0,  0.0,   0,    {},             30.0,   1},
    {ParamId::LimUnlinkTransient,"lim_unlink_tr", "Limiter Unlink Transient", "UnlTr", "%", ParamKind::Continuous, 0.0,    100.0, 0.0,   0,    {},             30.0,   0},
    {ParamId::LimUnlinkSustain,  "lim_unlink_su", "Limiter Unlink Sustain",   "UnlSu", "%", ParamKind::Continuous, 0.0,    100.0, 0.0,   0,    {},             30.0,   0},
    {ParamId::LimOn,             "lim_on",        "Limiter Module",       "LimOn",  "",     ParamKind::Toggle,     0.0,    1.0,   1.0,   1,    kToggleLabels,  20.0,   0},

    // Imager (étage 4) : largeur du Side par bande, 100 % = inchangé.
    {ParamId::ImgSplitLow,       "img_split_low", "Imager Split Low",     "ImgSL",  "Hz",   ParamKind::Continuous, 100.0,  1000.0, 250.0, 0,   {},             0.0,    0, kLog},
    {ParamId::ImgSplitMid,       "img_split_mid", "Imager Split Mid",     "ImgSM",  "Hz",   ParamKind::Continuous, 500.0,  6000.0, 2000.0, 0,  {},             0.0,    0, kLog},
    {ParamId::ImgSplitHigh,      "img_split_high","Imager Split High",    "ImgSH",  "Hz",   ParamKind::Continuous, 2000.0, 16000.0, 8000.0, 0, {},             0.0,    0, kLog},
    {static_cast<ParamId>(810), "img_width_low",  "Width Low",           "WLow",   "%",    ParamKind::Continuous, 0.0,    200.0, 100.0, 0,    {},             0.0,    0},
    {static_cast<ParamId>(811), "img_width_lmid", "Width Low-Mid",       "WLMid",  "%",    ParamKind::Continuous, 0.0,    200.0, 100.0, 0,    {},             0.0,    0},
    {static_cast<ParamId>(812), "img_width_hmid", "Width High-Mid",      "WHMid",  "%",    ParamKind::Continuous, 0.0,    200.0, 100.0, 0,    {},             0.0,    0},
    {static_cast<ParamId>(813), "img_width_high", "Width High",          "WHigh",  "%",    ParamKind::Continuous, 0.0,    200.0, 100.0, 0,    {},             0.0,    0},
    {ParamId::ImgOn,             "img_on",        "Imager Module",        "ImgOn",  "",     ParamKind::Toggle,     0.0,    1.0,   1.0,   1,    kToggleLabels,  20.0,   0},

    // Stem (stems/StemChain.h) : Off par défaut, la personne choisit.
    {ParamId::StemMode,          "stem_mode",     "Stem Mode",            "SMode",  "",     ParamKind::Choice,     0.0,    2.0,   0.0,   2,    kStemModeLabels, 0.0,   0},
    {ParamId::StemHpf,           "stem_hpf",      "Stem Low Cut",         "SHpf",   "Hz",   ParamKind::Continuous, 10.0,   500.0, 20.0,  0,    {},             0.0,    0, kLog},
    {ParamId::StemComp,          "stem_comp",     "Stem Compression",     "SComp",  "%",    ParamKind::Continuous, 0.0,    100.0, 0.0,   0,    {},             0.0,    0},
    {ParamId::StemSat,           "stem_sat",      "Stem Saturation",      "SSat",   "%",    ParamKind::Continuous, 0.0,    100.0, 0.0,   0,    {},             0.0,    0},
    {ParamId::StemClean,         "stem_clean",    "Stem Dynamic EQ",      "SClean", "%",    ParamKind::Continuous, 0.0,    100.0, 0.0,   0,    {},             0.0,    0},
    {ParamId::StemWidth,         "stem_width",    "Stem Width",           "SWidth", "%",    ParamKind::Continuous, 0.0,    150.0, 100.0, 0,    {},             0.0,    0},
    {ParamId::StemTrim,          "stem_trim",     "Stem Trim",            "STrim",  "dB",   ParamKind::Continuous, -24.0,  12.0,  0.0,   0,    {},             0.0,    1},

    {ParamId::MeterMomentary,    "meter_m",       "Momentary Loudness",   "M",      "LUFS", ParamKind::Meter,      -80.0,  10.0,  -80.0, 0,    {},             0.0,    1},
    {ParamId::MeterShortTerm,    "meter_s",       "Short-Term Loudness",  "S",      "LUFS", ParamKind::Meter,      -80.0,  10.0,  -80.0, 0,    {},             0.0,    1},
    {ParamId::MeterIntegrated,   "meter_i",       "Integrated Loudness",  "I",      "LUFS", ParamKind::Meter,      -80.0,  10.0,  -80.0, 0,    {},             0.0,    1},
    {ParamId::MeterTruePeak,     "meter_tp",      "True Peak",            "TP",     "dBTP", ParamKind::Meter,      -80.0,  12.0,  -80.0, 0,    {},             0.0,    1},
    {ParamId::MeterCorrelation,  "meter_corr",    "Phase Correlation",    "Corr",   "",     ParamKind::Meter,      -1.0,   1.0,   0.0,   0,    {},             0.0,    2},
    {ParamId::MeterLoudnessRange,"meter_lra",     "Loudness Range",       "LRA",    "LU",   ParamKind::Meter,      0.0,    40.0,  0.0,   0,    {},             0.0,    1},
    {ParamId::MeterRefSeconds,   "meter_ref_s",   "Reference Listened",   "RefS",   "s",    ParamKind::Meter,      0.0,    600.0, 0.0,   0,    {},             0.0,    0},
    {ParamId::MeterRefLufs,      "meter_ref_lufs","Reference Loudness",   "RefI",   "LUFS", ParamKind::Meter,      -80.0,  10.0,  -80.0, 0,    {},             0.0,    1},
    {ParamId::MeterLinkStems,    "meter_stems",   "Linked Stems",         "Stems",  "",     ParamKind::Meter,      0.0,    32.0,  0.0,   0,    {},             0.0,    0},
    {ParamId::MeterStemElement,  "meter_element", "Identified Element",   "Heard",  "",     ParamKind::Meter,      0.0,    21.0,  0.0,   21,   kElementMeterLabels, 0.0, 0},
    {ParamId::MeterProposals,    "meter_proposals","Mix Proposals",       "Props",  "",     ParamKind::Meter,      0.0,    32.0,  0.0,   0,    {},             0.0,    0},
}};
// clang-format on
static_assert(kParameters.size() == kParameterCount, "kParameterCount doit suivre la table");

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

ParamId moduleOnId(Module m) noexcept {
    switch (m) {
        case Module::Sub: return ParamId::SubOn;
        case Module::Match: return ParamId::MatchOn;
        case Module::Inflator: return ParamId::InflOn;
        case Module::Imager: return ParamId::ImgOn;
        case Module::Multiband: return ParamId::MbOn;
        case Module::Clipper: return ParamId::ClipOn;
        case Module::Limiter: return ParamId::LimOn;
    }
    return ParamId::SubOn;
}

// Rang d'une permutation (code de Lehmer) : le rang 0 est l'ordre par défaut.
std::array<Module, kModules> chainOrder(int rank) noexcept {
    rank = std::clamp(rank, 0, kChainOrders - 1);
    std::array<int, kModules> pool{0, 1, 2, 3, 4, 5, 6};
    int left = kModules;
    std::array<Module, kModules> out{};
    int factorial = kChainOrders / kModules; // 6!
    for (int i = 0; i < kModules; ++i) {
        const int digit = rank / factorial;
        rank %= factorial;
        out[static_cast<std::size_t>(i)] = static_cast<Module>(pool[static_cast<std::size_t>(digit)]);
        for (int k = digit; k < left - 1; ++k) pool[static_cast<std::size_t>(k)] = pool[static_cast<std::size_t>(k) + 1];
        --left;
        if (left > 0) factorial /= left;
    }
    return out;
}

int chainOrderRank(const std::array<Module, kModules>& order) noexcept {
    std::array<bool, kModules> used{};
    int rank = 0, factorial = kChainOrders / kModules;
    for (int i = 0; i < kModules; ++i) {
        const int m = static_cast<int>(order[static_cast<std::size_t>(i)]);
        if (m < 0 || m >= kModules || used[static_cast<std::size_t>(m)]) return 0; // pas une permutation
        int smaller = 0;
        for (int k = 0; k < m; ++k) smaller += used[static_cast<std::size_t>(k)] ? 0 : 1;
        used[static_cast<std::size_t>(m)] = true;
        rank += smaller * factorial;
        if (kModules - 1 - i > 0) factorial /= (kModules - 1 - i);
    }
    return rank;
}

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
    if (spec.kind == ParamKind::Toggle || spec.kind == ParamKind::Choice || (spec.kind == ParamKind::Meter && !spec.choices.empty())) {
        const auto index = static_cast<std::size_t>(clampPlain(spec, plain) - spec.minValue);
        if (index < spec.choices.size()) return std::string(spec.choices[index]);
    }
    // Bas d'échelle d'un niveau (LUFS, dBTP) = silence ; pas d'une durée ou d'un compte.
    if (spec.kind == ParamKind::Meter && plain <= spec.minValue && (spec.unit == "LUFS" || spec.unit == "dBTP"))
        return withUnit ? "-inf " + std::string(spec.unit) : "-inf";

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
