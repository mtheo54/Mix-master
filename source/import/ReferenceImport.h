// =============================================================================
//  Abrasion Labs // OMNIS
//  import/ReferenceImport.h — Ajouter un fichier de référence (bouton du menu
//  RÉF, commande `omnis_refdb reconnaitre`).
//
//  Un fichier doit donner un ARTISTE et un TITRE, dans cet ordre de recherche :
//    1. ses étiquettes : ID3v2 (MP3, ou chunk « id3 » d'un WAV), commentaires
//       Vorbis (FLAC), LIST/INFO (WAV) — artiste, titre, ISRC ;
//    2. son nom : « Artiste - Titre » ou « Titre - Artiste » (le nom qu'OMNIS
//       connaît comme artiste décide de l'ordre ; à défaut, artiste d'abord).
//       Numéro de piste, « (Clip Officiel) », « [prod. …] », « 140bpm » retirés ;
//  sinon refusé : « nomme le fichier Artiste - Titre ».
//  3. En option (une requête, une seule fois par fichier ajouté), MusicBrainz
//     confirme : par l'ISRC s'il existe (exact), sinon par artiste + titre, retenu
//     seulement si les deux correspondent et que la durée colle (un extrait plus
//     court est accepté). Absent de MusicBrainz (petits artistes) : on garde ce
//     que disent le fichier et son nom.
//  La pochette vient du fichier (FLAC, MP3) : avatar OMNIS dans le cache local.
//  Le titre et son nom ne vont QUE dans la base de la personne, sur son poste.
// =============================================================================
#pragma once

#include "ai/ReferenceLibrary.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace omnis::import {

struct FileTags {
    std::string artist, title, isrc;
};

/// Étiquettes d'un fichier audio (vides si absentes).
FileTags readFileTags(const std::filesystem::path& path);
/// Lecteurs sur les octets (tests) : FLAC, ID3v2, WAV (LIST/INFO et chunk id3).
FileTags readFlacTags(const std::string& bytes);
FileTags readId3Tags(const std::string& bytes);
FileTags readWavTags(const std::string& bytes);

/// Images intégrées (pochette en premier) : FLAC, MP3 (APIC), WAV (chunk id3).
std::vector<std::string> readCoverImages(const std::filesystem::path& path);

/// « 01 - GRY - BRR (Clip Officiel) » -> {"GRY", "BRR"} ; vide si moins de deux parties.
struct NameParts {
    std::string first, second;
    [[nodiscard]] bool ok() const { return !first.empty() && !second.empty(); }
};
NameParts splitFileName(const std::string& stem);
/// Titre sans « (Official Video) », « [Clip] », « (feat. …) » : pour comparer.
std::string bareTitle(const std::string& title);

/// Ce que MusicBrainz confirme.
struct OnlineMatch {
    bool found = false;
    std::string artist, title, isrc, mbid;
    double seconds = 0.0;
};
std::string musicBrainzIsrcUrl(const std::string& isrc);
std::string musicBrainzSearchUrl(const std::string& artist, const std::string& title);
/// Le premier enregistrement dont l'artiste ET le titre correspondent (fautes
/// tolérées) et dont la durée colle (±max(8 s, 6 %), ou un extrait plus court).
bool parseMusicBrainz(const std::string& json, const std::string& artist, const std::string& title, double fileSeconds, OnlineMatch& out);

/// Une requête GET (url -> corps) ; nullptr : hors ligne.
using Fetch = std::function<bool(const std::string& url, std::string& body)>;
/// La vraie requête (import/HttpGet, curl du système).
Fetch onlineFetch();

struct Identification {
    std::string artist, title, isrc;
    std::string source;      ///< « étiquettes », « nom du fichier »
    bool asked = false;      ///< MusicBrainz interrogé
    bool confirmed = false;  ///< MusicBrainz l'a reconnu
    std::string problem;     ///< non vide : refusé (pas d'artiste ou pas de titre)
};

/// `knownArtist` : nom qu'OMNIS connaît comme artiste (catalogue, base de la
/// personne) — décide de l'ordre d'un nom de fichier. `fetch` nullptr : hors ligne.
Identification identifyFile(const std::filesystem::path& path, double fileSeconds, const std::function<bool(const std::string&)>& knownArtist,
                            const Fetch* fetch);

struct ImportResult {
    bool ok = false;
    std::string message;   ///< ce qu'OMNIS répond (en français ; i18n::tr à l'affichage)
    Identification id;
    ai::ReferenceEntry entry;
};

/// Mesure le fichier, le reconnaît, fait l'avatar de sa pochette et l'ajoute à
/// la base de la personne (`userBase`, créée au besoin). `genresOf` : genres connus
/// de l'artiste (catalogue), ajoutés à la fiche.
ImportResult importReferenceFile(const std::filesystem::path& path, const std::filesystem::path& userBase,
                                 const std::function<bool(const std::string&)>& knownArtist,
                                 const std::function<std::vector<std::string>(const std::string&)>& genresOf, const Fetch* fetch);

} // namespace omnis::import
