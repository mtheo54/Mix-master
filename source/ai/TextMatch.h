// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/TextMatch.h — Comprendre une demande écrite vite, avec des fautes.
//
//  « je veu que sa sois propr et que ça claque » doit se lire comme « je veux
//  que ça soit propre et que ça claque ». Trois niveaux :
//    1. normalisation : minuscules, accents retirés, ponctuation isolée,
//       lettres répétées réduites (« fooort » -> « fort ») ;
//    2. clé phonétique française simplifiée : ph->f, qu/ck/c(a,o,u)->k, y->i,
//       z->s, doubles lettres réduites, finales muettes retirées (s, x, t, d,
//       e) -> « brillan » et « brillants » ont la même clé ;
//    3. distance de Damerau-Levenshtein entre clés (inversion de deux lettres
//       = 1 faute) : 0 sous 4 lettres, 1 jusqu'à 7, 2 au-delà.
//  Non temps réel.
// =============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace omnis::ai {

/// Minuscules ASCII, accents français retirés, ’ -> ', lettres répétées 3 fois
/// ou plus réduites à une.
std::string normalizeText(std::string_view text);

/// Mots et ponctuation (« , . ! ? ; : ») comme jetons séparés. Les apostrophes
/// coupent (« l'air » -> « l », « air »). « + » devient « plus » (« ++ » :
/// « beaucoup plus »), « & » devient « et ».
std::vector<std::string> tokenize(std::string_view text);

std::string phoneticKey(std::string_view word);

int damerauLevenshtein(std::string_view a, std::string_view b);

/// Deux mots désignent-ils la même chose, fautes comprises ?
bool fuzzyEquals(std::string_view word, std::string_view reference);

/// Mot (normalisé) qui n'est pas de la ponctuation.
bool isWord(std::string_view token);

} // namespace omnis::ai
