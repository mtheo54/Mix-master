// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/StyleRecognizer.h — Reconnaître le style d'une prod à l'écoute.
//
//  La prod (écoutée par AUTO, ou glissée sur la barre IA) est comparée à tous
//  les profils de genre livrés (ai/GenreProfiles : moyennes de titres mesurés,
//  aucune fiche de titre). Seules comptent des grandeurs qui tiennent à un
//  master près, parce qu'une prod de beatmaker n'est pas encore masterisée :
//    - la forme du spectre du Mid, relative au corps (pas son niveau) ;
//    - la largeur (Side − Mid) par nœud ;
//    - la brillance (centroïde), la densité d'attaques (coups par seconde) ;
//    - la micro-dynamique par zone (le mordant des attaques).
//  La sonie, le PLR et le LRA, qui dépendent surtout du mastering, sont
//  écartés.
//
//  Distance : écarts réduits par l'écart-type de chaque grandeur sur les
//  profils, pondérés par groupe (poids réglés par validation croisée par
//  artiste sur data/references.txt : `omnis_refdb style-eval`). Probabilités :
//  softmax des distances (température réglée de même) ; « sûr » quand le
//  premier l'emporte nettement, sinon OMNIS propose les deux ou trois premiers.
// =============================================================================
#pragma once

#include "ReferenceLibrary.h"

#include <map>
#include <string>
#include <vector>

namespace omnis::ai {

struct StyleWeights {
    // Valeurs retenues par `omnis_refdb style-eval --groupes --grille` (validation croisée par artiste).
    double shape = 1.5;      ///< forme du spectre du Mid
    double width = 0.4;      ///< largeur Side − Mid
    double brightness = 0.0; ///< centroïde (n'aide pas : redondant avec la forme)
    double onsets = 0.0;     ///< coups par seconde (n'aide pas sur des titres masterisés)
    double micro = 0.5;      ///< micro-dynamique par zone
    double tempo = 1.0;      ///< tempo (log2 BPM ; seulement avec les statistiques : les profils n'ont pas de tempo)
    double temperature = 0.7; ///< réglée pour que « 34 % » soit juste une fois sur trois (style-eval)
};

/// La dispersion de chaque profil (moyenne et écart-type de chaque grandeur sur ses titres) :
/// des statistiques agrégées (au moins 3 titres), jamais une fiche de titre.
struct StyleClassStats {
    std::vector<double> mean, sd;
    int titles = 0;
};
using StyleStats = std::map<std::string, StyleClassStats>; ///< titre du profil -> statistiques

/// À partir des titres et de leur rangement (GenreProfileSet::assignments).
StyleStats buildStyleStats(const std::vector<const ReferenceEntry*>& titles, const std::vector<std::pair<const ReferenceEntry*, std::string>>& profileOfTitle);
std::string serializeStyleStats(const StyleStats& stats);
StyleStats parseStyleStats(const std::string& text);

struct StyleGuess {
    const ReferenceEntry* profile = nullptr;
    std::string family; ///< « Rap Français & New Wave »
    std::string style;  ///< « drill fr » ; vide : la famille seule
    double probability = 0.0;
    double distance = 0.0;
    /// Le nom à montrer : le sous-genre, sinon la famille.
    [[nodiscard]] const std::string& name() const noexcept { return style.empty() ? family : style; }
};

struct StyleRecognition {
    std::vector<StyleGuess> ranked; ///< du plus probable au moins probable
    bool confident = false;         ///< le premier l'emporte nettement
    [[nodiscard]] bool empty() const noexcept { return ranked.empty(); }
};

/// Grandeurs (non réduites) d'une signature pour la reconnaissance.
std::vector<double> styleFeatures(const analysis::SpectralSignature& s);

/// `profiles` : les profils de genre (ReferenceEntry::isProfile). Les autres entrées sont ignorées.
/// `stats` (facultatif) : la dispersion de chaque profil ; sans elle, distance au centre seulement.
StyleRecognition recognizeStyle(const analysis::SpectralSignature& prod, const std::vector<const ReferenceEntry*>& profiles,
                                const StyleWeights& weights = {}, const StyleStats* stats = nullptr);

/// Le groupe sonore d'un sous-genre ou d'une famille (« trap fr », « dark trap », « trap » -> « trap ») :
/// ce qui compte pour le master. Vide : rap sans sous-genre connu.
std::string sonicGroup(const std::string& family, const std::string& style);

/// Reconnaître le GROUPE sonore directement, d'après la dispersion de chaque groupe (beaucoup plus
/// de titres par groupe que par profil : statistiques fiables). Chaque StyleGuess porte le groupe dans `style`.
/// Mémoire personnelle : les prods dont la personne a nommé le style (grandeurs seulement, jamais
/// d'audio, sur son poste). Elles pèsent d'autant plus qu'elles sont nombreuses (jusqu'à 80 %).
struct StyleExample {
    std::string group;
    std::vector<double> features;
};
std::string serializeStyleExamples(const std::vector<StyleExample>& examples);
std::vector<StyleExample> parseStyleExamples(const std::string& text);

StyleRecognition recognizeGroup(const analysis::SpectralSignature& prod, const StyleStats& groups, const StyleWeights& weights = {},
                                const std::vector<StyleExample>* personal = nullptr);

/// Le style à viser pour un groupe (« plugg / new wave » -> « new wave fr ») : un genre de la base.
std::string styleForGroup(const std::string& group);

/// Les profils d'une base (pour recognizeStyle).
std::vector<const ReferenceEntry*> profilesOf(const ReferenceLibrary& library);

/// En clair, pour la barre IA : « J'entends de la drill fr (64 %) ; sinon trap (21 %). »
std::string describeRecognition(const StyleRecognition& r);

} // namespace omnis::ai
