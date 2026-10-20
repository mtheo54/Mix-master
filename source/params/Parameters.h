// =============================================================================
//  Abrasion Labs // OMNIS
//  params/Parameters.h — Table unique des paramètres (DSP, VST3, outils, IA).
//
//  Toute la connaissance d'un paramètre (identifiant stable, clé texte, bornes,
//  valeur par défaut, unité, lissage) vit ici et nulle part ailleurs. Le
//  processeur VST3, le contrôleur, l'outil de rendu hors ligne et, plus tard,
//  le prédicteur IA (qui produira un vecteur indexé par ces clés) partagent
//  cette table — pas de risque de désynchronisation.
//
//  Identifiants : ils sont enregistrés dans les sessions et l'automation des
//  DAW, ils ne doivent JAMAIS changer. Chaque étage de la chaîne dispose d'une
//  plage réservée de 100 identifiants :
//       0 –  99  Global (bypass, trims, monitoring)
//     100 – 199  Sub Control & Mono Maker
//     200 – 299  Match EQ dynamique & correctif
//     300 – 399  Excitation harmonique M/S
//     400 – 499  Multi-Band Dynamic Processor
//     500 – 599  Smart Wave-Shaping / Mastering Clipper
//     600 – 699  Lookahead Brickwall Limiter
//     700 – 799  Dither TPDF & noise-shaping
//    1000 –1099  Mesures (lecture seule, processeur -> contrôleur)
//
//  Valeurs « plain » : unités physiques (dB, Hz, index de choix).
//  Valeurs normalisées : [0, 1], convention VST3 (y compris pour les choix
//  discrets : plain = min(stepCount, floor(norm · (stepCount + 1)))).
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace omnis::params {

enum class ParamId : std::uint32_t {
    // --- Global --------------------------------------------------------------
    Bypass = 0,
    InputGain = 1,
    OutputGain = 2,
    Monitor = 3,

    // --- Mesures (lecture seule) ---------------------------------------------
    MeterMomentary = 1000,
    MeterShortTerm = 1001,
    MeterIntegrated = 1002,
    MeterTruePeak = 1003,
    MeterCorrelation = 1004,
    MeterLoudnessRange = 1005,
};

enum class ParamKind : std::uint8_t {
    Continuous, ///< valeur réelle, mapping linéaire min..max
    Toggle,     ///< 0 / 1
    Choice,     ///< index entier 0..stepCount
    Meter,      ///< sortie lecture seule (non automatisable, non sauvegardée)
};

/// Modes d'écoute (bouton chiclet « Side solo » de l'interface).
enum class MonitorMode : int { Stereo = 0, Mid = 1, Side = 2 };

struct ParamSpec {
    ParamId id;
    std::string_view key;       ///< clé stable ASCII (CLI, presets JSON, IA)
    std::string_view name;      ///< nom affiché dans le DAW
    std::string_view shortName; ///< nom court (surfaces de contrôle)
    std::string_view unit;
    ParamKind kind;
    double minValue;
    double maxValue;
    double defaultValue;
    int stepCount;                           ///< 0 = continu
    std::span<const std::string_view> choices; ///< libellés (Choice uniquement)
    double smoothingMs;                      ///< temps de lissage à 99 % dans le DSP
    int displayDecimals;
};

/// Tous les paramètres, dans l'ordre d'enregistrement auprès de l'hôte.
std::span<const ParamSpec> allParameters() noexcept;

const ParamSpec* findParameter(ParamId id) noexcept;
const ParamSpec* findParameter(std::uint32_t id) noexcept;
const ParamSpec* findParameterByKey(std::string_view key) noexcept;

/// Index dans allParameters() (pour les tableaux de valeurs), ou -1.
int parameterIndex(ParamId id) noexcept;

double toPlain(const ParamSpec& spec, double normalized) noexcept;
double toNormalized(const ParamSpec& spec, double plain) noexcept;
double clampPlain(const ParamSpec& spec, double plain) noexcept;

inline bool isPersistent(const ParamSpec& spec) noexcept { return spec.kind != ParamKind::Meter; }

/// Formatage pour l'affichage (non temps réel : alloue une std::string).
std::string formatValue(const ParamSpec& spec, double plain, bool withUnit = false);

/// Analyse d'une saisie texte (« -3.5 », « -3.5 dB », « Side », « on »...).
std::optional<double> parseValue(const ParamSpec& spec, std::string_view text);

} // namespace omnis::params
