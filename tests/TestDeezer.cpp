// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestDeezer.cpp — Lecture des réponses de l'API Deezer (omnis_refdb
//  deezer) : sur des réponses d'exemple au format de l'API, sans réseau.
// =============================================================================
#include "TestFramework.h"

#include "Deezer.h"

using namespace omnis::tools::deezer;

namespace {

// Format de /search/artist (champs utiles ; valeurs d'exemple).
const char* kArtists = R"json({"data":[
  {"id":11,"name":"La Fève Tribute","link":"https:\/\/www.deezer.com\/artist\/11","picture_xl":"https:\/\/cdn.example\/a11.jpg","nb_fan":40,"type":"artist"},
  {"id":12,"name":"La Fève","link":"https:\/\/www.deezer.com\/artist\/12","picture_xl":"https:\/\/cdn.example\/a12.jpg","nb_fan":90000,"type":"artist"},
  {"id":13,"name":"Lafeve","link":"https:\/\/www.deezer.com\/artist\/13","picture_big":"https:\/\/cdn.example\/a13.jpg","nb_fan":10,"type":"artist"}
],"total":3,"next":"https:\/\/api.deezer.com\/search\/artist?q=La+Feve&index=25"})json";

// Format de /search/track.
const char* kTracks = R"json({"data":[
  {"id":21,"title":"Autre son","link":"https:\/\/www.deezer.com\/track\/21","artist":{"id":12,"name":"La Fève"},"album":{"id":31,"title":"Album A","cover_xl":"https:\/\/cdn.example\/c31.jpg"}},
  {"id":22,"title":"Mon titre (feat. Quelqu'un)","link":"https:\/\/www.deezer.com\/track\/22","artist":{"id":12,"name":"La Fève"},"album":{"id":32,"title":"Album B","cover_xl":"https:\/\/cdn.example\/c32.jpg"}},
  {"id":23,"title":"Mon titre","link":"https:\/\/www.deezer.com\/track\/23","artist":{"id":99,"name":"Quelqu'un d'autre"},"album":{"id":33,"title":"X","cover_xl":"https:\/\/cdn.example\/c33.jpg"}}
]})json";

} // namespace

OMNIS_TEST("deezer: lecteur JSON (échappements, unicode, imbrication)") {
    Json j;
    OMNIS_CHECK(parseJson(R"({"a":[1,-2.5e1,true,null,{"b":"x\"y\\z\/é😀"}]})", j));
    OMNIS_CHECK(j["a"].array.size() == 5 && j["a"].array[1].number == -25.0 && j["a"].array[2].boolean);
    OMNIS_CHECK(j["a"].array[4].str("b") == "x\"y\\z/\xC3\xA9\xF0\x9F\x98\x80");
    OMNIS_CHECK(j["absent"]["encore"].string.empty());
    OMNIS_CHECK(!parseJson("{\"a\":", j));
    OMNIS_CHECK(!parseJson("{\"error\":{}} reste", j));
}

OMNIS_TEST("deezer: le bon artiste (accents, fautes) et le bon titre (feat. ignoré si besoin)") {
    const auto artists = parseArtists(kArtists);
    OMNIS_CHECK(artists.size() == 3);
    const auto* a = pickArtist(artists, "La Feve"); // sans accent : trouvé, le plus suivi
    OMNIS_CHECK(a && a->id == 12 && a->name == "La F\xC3\xA8ve" && a->picture == "https://cdn.example/a12.jpg");
    OMNIS_CHECK(artists[2].picture == "https://cdn.example/a13.jpg"); // picture_big si pas de picture_xl
    OMNIS_CHECK(pickArtist(artists, "Booba") == nullptr);
    const auto tracks = parseTracks(kTracks);
    OMNIS_CHECK(tracks.size() == 3 && tracks[1].album == "Album B");
    const auto* t = pickTrack(tracks, "La Fève", "Mon titre");
    OMNIS_CHECK(t && t->id == 22 && t->cover == "https://cdn.example/c32.jpg" && t->link == "https://www.deezer.com/track/22");
    OMNIS_CHECK(pickTrack(tracks, "La Fève", "Inconnu") == nullptr);
    OMNIS_CHECK(parseArtists(R"({"error":{"type":"Exception","message":"Quota limit exceeded","code":4}})").empty());
}

OMNIS_TEST("deezer: adresses de recherche encodées, adresses refusées avant curl") {
    OMNIS_CHECK(artistSearchUrl("La Fève") == "https://api.deezer.com/search/artist?q=La+F%C3%A8ve");
    OMNIS_CHECK(trackSearchUrl("La Fève", "Mon titre (feat. X)") ==
                "https://api.deezer.com/search/track?q=artist%3A%22La+F%C3%A8ve%22+track%3A%22Mon+titre%22");
    OMNIS_CHECK(safeUrl("https://cdn.example/images/1000x1000-000000-80-0-0.jpg"));
    OMNIS_CHECK(!safeUrl("http://cdn.example/a.jpg"));
    OMNIS_CHECK(!safeUrl("https://x.example/a.jpg\" & calc"));
    OMNIS_CHECK(!safeUrl("https://x.example/$(rm)"));
}
