// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/CommunityHabits.h — La moyenne de ce que font les utilisateurs d'OMNIS.
//
//  Comme pour les références son : chaque personne qui l'accepte donne une
//  CONTRIBUTION anonyme (contributionText) : pour chaque contexte générique
//  (AUTO · trap, preset · style rage fr), ses gestes : vus, utiles, choix
//  artistiques, écart moyen. Aucun compte, aucune date, aucun nom de fichier,
//  aucun nom de référence (les presets « titre » sont laissés de côté).
//
//  Abrasion Labs en fait la MOYENNE (aggregateContributions, outil
//  `omnis_refdb usages`) : un geste n'y entre que s'il est une habitude chez
//  au moins kMinPeople personnes dans ce contexte (en dessous, on pourrait
//  reconnaître quelqu'un). Le résultat, data/usages_communaute.txt, est livré
//  avec la version suivante du plug-in : AUTO s'en sert tant que la personne
//  a peu d'habitudes à elle (sa part baisse à mesure que les siennes
//  grandissent), dans des bornes étroites. Le plug-in évolue ainsi de lui-même,
//  version après version.
// =============================================================================
#pragma once

#include "HabitLearning.h"
#include "UsageGraph.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omnis::ai {

/// Une ligne de contribution : un geste dans un contexte, pour une personne.
struct ContributionRow {
    std::string context; ///< « auto:trap », « style:rage fr »
    std::string gesture; ///< « sub+ », « mod7:on », « infl:2 »
    int seen = 0, useful = 0, creative = 0;
    double mean = 0.0;   ///< écart moyen quand il était utile (unités du réglage)
};

/// Le contexte générique d'un nœud du graphe, vide s'il est propre à une personne
/// (un preset « titre » : le nom d'une référence).
std::string genericContext(std::string_view contextId);

/// La contribution anonyme d'une personne (texte TSV « omnis-contribution v1 »).
std::string contributionText(const UsageGraph& g);
std::optional<std::vector<ContributionRow>> parseContribution(std::string_view text);

struct CommunityHabit {
    std::string context, gesture;
    int people = 0;      ///< personnes chez qui c'est une habitude dans ce contexte
    double share = 0.0;  ///< parmi les personnes qui ont travaillé dans ce contexte
    double mean = 0.0;   ///< écart moyen chez elles
};

class CommunityHabits {
public:
    static constexpr int kMinPeople = 5;          ///< en dessous, rien n'est publié (anonymat)
    static constexpr double kMinShare = 0.3;      ///< une habitude d'au moins 30 % des personnes
    static constexpr double kMaxWeight = 0.5;     ///< sans habitude à soi, la moitié de la moyenne au plus

    /// La moyenne de toutes les contributions (une par personne).
    static CommunityHabits aggregate(const std::vector<std::vector<ContributionRow>>& contributions);
    static std::optional<CommunityHabits> parse(std::string_view text);
    [[nodiscard]] std::string serialize() const;
    /// Celle livrée avec le plug-in (data/usages_communaute.txt).
    static const CommunityHabits& shipped();

    [[nodiscard]] const std::vector<CommunityHabit>& habits() const noexcept { return habits_; }
    [[nodiscard]] int contributions() const noexcept { return contributions_; }
    /// Ce qu'AUTO ajoute dans ce contexte (« auto:trap ») : réglages continus seulement,
    /// pondérés par la part des personnes et par (1 − confiance de la personne) × kMaxWeight,
    /// bornés à la moitié des bornes des habitudes.
    [[nodiscard]] HabitVector delta(std::string_view context, double personalConfidence) const;
    /// Le nombre de personnes derrière ce contexte (0 si rien de publié).
    [[nodiscard]] int peopleIn(std::string_view context) const;

private:
    std::vector<CommunityHabit> habits_;
    int contributions_ = 0;
};

/// En clair, après AUTO : « ce que font les autres en trap (12 personnes) : sub +0.4 dB ». Vide si
/// rien ne s'entend.
std::string describeCommunity(const HabitVector& d, std::string_view context, int people);

} // namespace omnis::ai
