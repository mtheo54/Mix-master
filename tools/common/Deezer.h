// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/Deezer.h — Images et liens d'une référence depuis l'API publique
//  de Deezer (api.deezer.com, sans clé) : photo de l'artiste, pochette de chaque
//  titre, lien vers la musique. Utilisé par omnis_refdb, sur le PC de la
//  personne : le plug-in, lui, reste hors ligne et ne lit que les fichiers.
//
//  Droits : ces images ne sont pas sous licence libre. Elles sont montrées avec
//  « Image : Deezer » et le lien vers la musique, pour un usage personnel ; les
//  livrer avec le plug-in vendu demande l'accord de Deezer et des labels.
// =============================================================================
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace omnis::tools::deezer {

/// Une valeur JSON (lecteur minimal : ce que renvoie l'API suffit).
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;

    [[nodiscard]] const Json& operator[](const std::string& key) const;
    [[nodiscard]] std::string str(const std::string& key) const { return (*this)[key].string; }
    [[nodiscard]] double num(const std::string& key) const { return (*this)[key].number; }
};

/// false : texte qui n'est pas du JSON.
bool parseJson(const std::string& text, Json& out);

struct Artist {
    long long id = 0;
    std::string name, link, picture; ///< picture : la plus grande (picture_xl)
    long long fans = 0;
};

struct Track {
    long long id = 0;
    std::string title, link, artist, album, cover; ///< cover : la plus grande (cover_xl)
};

/// Réponse de /search/artist ; vide si erreur ou rien trouvé.
std::vector<Artist> parseArtists(const std::string& json);
/// Réponse de /search/track.
std::vector<Track> parseTracks(const std::string& json);

/// L'artiste demandé parmi les résultats (nom tolérant aux fautes et accents ;
/// à égalité, le plus suivi) ; nullptr si aucun ne correspond.
const Artist* pickArtist(const std::vector<Artist>& found, const std::string& name);
/// Le titre demandé de cet artiste (les « (feat. …) » ignorés si besoin).
const Track* pickTrack(const std::vector<Track>& found, const std::string& artist, const std::string& title);

std::string urlEncode(const std::string& text);
std::string artistSearchUrl(const std::string& name);
std::string trackSearchUrl(const std::string& artist, const std::string& title);

/// Une adresse sûre à passer à curl : https, caractères d'URL seulement.
bool safeUrl(const std::string& url);
/// Téléchargement par curl (présent sous Windows 10+, macOS, Linux).
bool download(const std::string& url, const std::string& path, std::string& error);

} // namespace omnis::tools::deezer
