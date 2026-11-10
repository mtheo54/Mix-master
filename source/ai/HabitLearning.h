// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/HabitLearning.h — Apprendre les habitudes de la personne (étape 5).
//
//  1. Un JOURNAL sur son poste (apprentissage/journal.tsv), versionné, fait de
//     nombres seulement — jamais d'audio, jamais de nom de fichier ni de phrase :
//       AUTO     les grandeurs de la prod (styleFeatures), son groupe, les
//                réglages qu'AUTO a proposés ;
//       RETOUCHE ce que la personne a changé ensuite (réglages gardés − proposés) ;
//       ANNULE   AUTO défait aussitôt (mauvais choix) ;
//       EXPORT   les réglages au moment de vérifier l'export (le bon choix).
//  2. Un PETIT RÉSEAU de neurones local (entrées : les grandeurs de la prod ;
//     16 neurones cachés tanh ; sorties : les écarts habituels sur les réglages
//     clés). Entraîné sur le poste, à chaque nouvelle retouche (quelques
//     millisecondes : quelques centaines d'exemples). Il corrige AUTO
//     d'autant plus qu'il a d'exemples (n / (n + 8)), et toujours dans des
//     bornes (docs/REFONTE.md §3) : il ne peut pas « se battre » contre la prod.
//  3. Un EXPORT ANONYME (grandeurs + écarts, rien d'autre), prêt pour une mise
//     en commun sur un serveur, avec l'accord explicite de la personne. Aucun
//     envoi ici : seulement le fichier.
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omnis::ai {

/// Les réglages appris (écarts appliqués à ce qu'AUTO propose).
enum class Habit : int { Tilt = 0, Sub = 1, Inflate = 2, Loudness = 3, Width = 4, Comp = 5, Count = 6 };
inline constexpr int kHabits = static_cast<int>(Habit::Count);
using HabitVector = std::array<double, kHabits>;

/// Nom (fichier) et bornes de chaque écart : tilt ±2 dB, sub ±2 dB, Inflator ±15 %, volume ±2 dB,
/// largeur ±20 %, seuil du Comp ±6 dB.
const char* habitKey(Habit h) noexcept;
const char* habitLabel(Habit h) noexcept; ///< « pente (tilt) », pour la barre IA
double habitBound(Habit h) noexcept;

/// PRESET : comme AUTO, mais la proposition vient d'un preset (référence ou style choisi au menu RÉF).
enum class JournalKind : int { Auto = 0, Retouch = 1, Undo = 2, Export = 3, Preset = 4 };

struct JournalEvent {
    static constexpr int kVersion = 1;
    JournalKind kind = JournalKind::Auto;
    std::int64_t time = 0;          ///< secondes depuis 1970 (ordre des événements)
    std::string group;              ///< groupe sonore reconnu ou nommé
    std::vector<double> features;   ///< styleFeatures de la prod
    HabitVector values{};           ///< AUTO / EXPORT : réglages ; RETOUCHE : écarts (gardé − proposé)
};

std::string serializeJournalEvent(const JournalEvent& e);
std::optional<JournalEvent> parseJournalEvent(std::string_view line);
bool appendJournal(const std::string& path, const JournalEvent& e);
std::vector<JournalEvent> loadJournal(const std::string& path);

/// Les exemples d'apprentissage : chaque RETOUCHE (grandeurs -> écarts). Un ANNULE juste après un
/// AUTO compte comme « rien de ce qu'AUTO a proposé n'allait » : il n'apprend pas d'écart (pas de
/// direction connue), il est seulement compté.
struct HabitSample {
    std::vector<double> features;
    HabitVector deltas{};
};
std::vector<HabitSample> samplesFromJournal(const std::vector<JournalEvent>& journal);

/// Le petit réseau : entrées réduites (moyenne / écart-type des exemples), 16 neurones cachés tanh,
/// plus un chemin direct (linéaire) des entrées aux sorties : les habitudes simples (« plus c'est
/// sombre, plus je remonte l'aigu ») s'apprennent avec peu de séances, les neurones gardent le reste.
/// kHabits sorties (écarts / borne, donc entre −1 et 1 en pratique).
class HabitNet {
public:
    static constexpr int kHidden = 16;

    /// Entraîne de zéro (graine fixe : déterministe). false : pas assez d'exemples (< 3).
    bool train(const std::vector<HabitSample>& samples, int epochs = 200);
    [[nodiscard]] bool trained() const noexcept { return samples_ > 0; }
    [[nodiscard]] int samples() const noexcept { return samples_; }
    /// Écarts prédits (unités des réglages), bornés, multipliés par la confiance n / (n + 8).
    [[nodiscard]] HabitVector predict(const std::vector<double>& features) const;
    [[nodiscard]] std::size_t weightCount() const noexcept;

    [[nodiscard]] std::string serialize() const;
    static std::optional<HabitNet> parse(std::string_view text);

private:
    [[nodiscard]] std::array<double, kHabits> forward(const std::vector<double>& x, std::array<double, kHidden>* hidden) const;
    int inputs_ = 0, samples_ = 0;
    std::vector<double> mean_, sd_;
    std::vector<double> w1_, b1_, w2_, b2_, w3_; ///< w1 : kHidden × inputs, w2 : kHabits × kHidden, w3 : chemin direct kHabits × inputs
};

/// Export anonyme : groupe, grandeurs, écarts des RETOUCHES (aucun temps exact : jour arrondi).
std::string exportAnonymous(const std::vector<JournalEvent>& journal);

/// En clair : « ton habitude : sub +0.8 dB, aigu -0.5 dB (appris sur 6 séances) ». Vide si rien de net.
std::string describeHabits(const HabitVector& deltas, int samples);

} // namespace omnis::ai
