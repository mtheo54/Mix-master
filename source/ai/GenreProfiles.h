// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/GenreProfiles.h — Les profils de genre livrés avec le plug-in.
//
//  Le plug-in ne contient AUCUNE fiche de titre : seulement des moyennes par
//  famille (data/genres_classement.txt) et par sous-genre (data/styles_artistes.txt),
//  chacune sur au moins `minTitles` titres, et les noms des artistes avec leurs
//  genres (sans aucun chiffre) pour comprendre « comme GRY ».
//
//  Chaque TITRE va dans UN sous-genre, pas chaque artiste : un artiste qui fait
//  de la rage et de la new wave voit chacun de ses titres rangé dans le
//  sous-genre dont le profil mesuré est le plus proche (k-moyennes restreintes
//  aux sous-genres de l'artiste : départ = tous ses sous-genres à poids égal,
//  puis chaque titre rejoint le plus proche, 8 passes). Distance : écarts
//  centrés-réduits (sur toute la base) de la balance Mid relative au corps, de
//  la largeur Side − Mid, de la dynamique par nœud, de la sonie, du PLR, du LRA,
//  de la micro-dynamique, de la brillance et des coups par seconde.
//
//  Un sous-genre de moins de `minTitles` titres n'a pas de profil : ses titres
//  restent dans celui de leur famille.
// =============================================================================
#pragma once

#include "ReferenceLibrary.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace omnis::ai {

/// Les familles de l'utilisateur (data/genres_classement.txt, embarqué) et leurs
/// mots de genre. Un mot de plusieurs familles (« rap ») est retiré : il ne les
/// distingue pas. Familles sans mots ignorées.
const std::vector<std::pair<std::string, std::vector<std::string>>>& genreFamilies();

/// Le titre appartient-il à la famille ? Un profil OMNIS porte le nom de sa
/// famille dans ses genres ; une fiche ordinaire, les mots de la famille.
bool inGenreFamily(const ReferenceEntry& e, const std::string& family, const std::vector<std::string>& words);

/// Clé d'artiste (minuscules, sans accents ni espaces : « La Fève » = « la feve »).
std::string artistKey(const std::string& name);

/// data/styles_artistes.txt (embarqué) : clé d'artiste -> sous-genres.
const std::map<std::string, std::vector<std::string>>& artistStyles();

/// Sous-genres d'un nom d'artiste, co-crédits compris (« GRy, iNESS M »,
/// « A x B », « A feat. B ») : chaque artiste connu apporte les siens.
std::vector<std::string> stylesOfArtist(const std::string& artist);

struct ProfileAssignment {
    std::string artist, title, family, style; ///< style vide : famille seulement
};

struct GenreProfileSet {
    std::vector<ReferenceEntry> profiles; ///< mesurés, artiste kProfileArtist
    std::vector<ReferenceEntry> artists;  ///< connus de nom : artiste + genres, sans chiffres
    std::vector<ProfileAssignment> assignments;
    std::vector<std::string> unclassified; ///< « Artiste - Titre » sans famille
};

/// `entries` : fiches mesurées par titre (data/references.txt). Les profils et
/// les entrées de nom sont ignorés. `names` (facultatif, le catalogue) : un
/// titre sans genre prend ceux de son artiste (entrée sans titre, « SLAYR ; hyperpop »).
GenreProfileSet buildGenreProfiles(const std::vector<const ReferenceEntry*>& entries, int minTitles = 3, const ReferenceLibrary* names = nullptr);

/// Le fichier livré (profils + artistes de nom), au format de la base.
std::string serializeGenreProfiles(const GenreProfileSet& set);

} // namespace omnis::ai
