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
    int albums = 0; ///< sorties (albums, EP, singles)
};

struct Track {
    long long id = 0;
    std::string title, link, artist, album, cover; ///< cover : la plus grande (cover_xl)
    long long artistId = 0;
    int durationS = 0;
    std::string isrc;
};

/// Une sortie d'un artiste (/artist/{id}/albums).
struct Album {
    long long id = 0;
    std::string title, type, date, link; ///< type : album, ep, single, compile
    int genre = 0;                       ///< genre Deezer (116 : rap) ; ≤ 0 : inconnu
};

/// Une page de réponse : `next` = adresse de la page suivante (vide : fin).
/// `quota` : Deezer demande d'attendre (trop de demandes) ; `error` : autre refus.
struct Page {
    std::string next;
    bool quota = false;
    std::string error;
};

/// Réponse de /search/artist ; vide si erreur ou rien trouvé.
std::vector<Artist> parseArtists(const std::string& json);
/// Réponse de /search/track, ou des titres d'un album (/album/{id}/tracks).
std::vector<Track> parseTracks(const std::string& json, Page* page = nullptr);
/// /artist/{id}/albums.
std::vector<Album> parseAlbums(const std::string& json, Page* page = nullptr);
/// /artist/{id}/related (même forme que la recherche d'artistes).
std::vector<Artist> parseArtistList(const std::string& json, Page* page = nullptr);
/// /artist/{id} : une fiche (id 0 si erreur).
Artist parseArtist(const std::string& json);
/// /track/{id} : le titre complet (sortie, date) et tous ses artistes crédités.
struct TrackDetail {
    Track track;
    long long albumId = 0;
    std::string albumDate;
    std::vector<Artist> contributors;
};
TrackDetail parseTrackDetail(const std::string& json);
/// /album/{id} : son genre Deezer (0 si inconnu).
int parseAlbumGenre(const std::string& json);

/// L'artiste demandé parmi les résultats : même nom (accents, casse, espaces
/// mis à part), sinon une lettre d'écart ; à égalité, le plus suivi. nullptr sinon.
const Artist* pickArtist(const std::vector<Artist>& found, const std::string& name);
/// Même nom d'artiste (casse, accents, espaces, « $ » mis à part).
bool sameArtist(const std::string& a, const std::string& b);
/// Le titre demandé de cet artiste (les « (feat. …) » ignorés si besoin).
const Track* pickTrack(const std::vector<Track>& found, const std::string& artist, const std::string& title);

std::string urlEncode(const std::string& text);
/// « Titre (feat. X) » -> « Titre ».
std::string bareTitle(const std::string& title);
std::string artistSearchUrl(const std::string& name);
/// Recherches d'un titre, à essayer dans l'ordre : la recherche avancée
/// (artist:"…" track:"…") ne trouve rien dès qu'il y a un accent (« CINÉMA ») ;
/// la recherche simple « artiste titre » prend le relais.
std::vector<std::string> trackSearchUrls(const std::string& artist, const std::string& title);

/// Une adresse sûre à passer à curl : https, caractères d'URL seulement.
bool safeUrl(const std::string& url);
/// Téléchargements par curl (présent sous Windows 10+, macOS, Linux), dans un
/// dossier cache : une adresse déjà là n'est pas redemandée (reprise après
/// coupure). Plusieurs adresses passent par un seul curl (une connexion), en
/// paquets qui respectent la limite de l'API (50 demandes / 5 s).
class Fetcher {
public:
    explicit Fetcher(std::string cacheDir);
    /// Le texte de chaque adresse (vide : échec). Réponses « quota » redemandées.
    std::vector<std::string> getAll(const std::vector<std::string>& urls);
    std::string get(const std::string& url) { return getAll({url}).front(); }
    /// Un fichier (image) copié vers `path` (chemin quelconque, accents compris).
    bool download(const std::string& url, const std::string& path, std::string& error);
    /// Les octets d'une adresse, en mémoire seulement (curl -> tube, aucun fichier) :
    /// une image d'origine n'est jamais écrite sur le disque.
    std::string fetchBytes(const std::string& url, std::string& error);
    [[nodiscard]] long long requests() const noexcept { return requests_; }
    /// Ne pas garder les réponses (recherches : elles changent).
    bool keep = true;

private:
    std::string cachedName(const std::string& url) const;
    bool runCurl(const std::vector<std::string>& urls);
    std::string dir_;
    long long requests_ = 0;
    bool parallel_ = true; ///< curl -Z (sinon en série)
};

} // namespace omnis::tools::deezer
