// =============================================================================
//  Abrasion Labs // OMNIS
//  import/HttpGet.h — Une requête HTTPS GET, la seule connexion d'OMNIS :
//  confirmer un fichier de référence auprès de MusicBrainz, quand la personne
//  en ajoute un (et si elle ne l'a pas désactivé).
//
//  Passe par `curl`, fourni par le système (Windows 10 / 11 : System32\curl.exe ;
//  macOS : /usr/bin/curl), lancé SANS interpréteur de commandes (posix_spawnp,
//  CreateProcessW) : aucune bibliothèque réseau à livrer, aucun risque
//  d'injection par le nom d'un fichier. Hors du thread audio uniquement
//  (bloquant, temps limité).
// =============================================================================
#pragma once

#include <string>

namespace omnis::import {

/// L'identité qu'OMNIS donne au serveur (MusicBrainz l'exige : application et contact).
const std::string& userAgent();

/// `url` : adresse https:// déjà encodée (lettres, chiffres, % et ponctuation d'URL
/// seulement). false : pas de curl, pas de réseau, erreur HTTP ou délai dépassé.
bool httpGet(const std::string& url, std::string& body, std::string& error, int timeoutSeconds = 10);

/// Encodage d'une valeur de paramètre (« La Fève » -> « La%20F%C3%A8ve »).
std::string urlEncode(const std::string& text);

} // namespace omnis::import
