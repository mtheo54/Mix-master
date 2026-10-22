// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestLanguage.cpp — Comprendre la demande (fautes, nuances, références).
// =============================================================================
#include "TestFramework.h"

#include "ai/EmbeddedData.h"
#include "ai/Lexicon.h"
#include "ai/MatchIntent.h"
#include "ai/ReferenceLibrary.h"
#include "ai/TextMatch.h"

#include <algorithm>
#include <string>

using namespace omnis;
using namespace omnis::ai;

namespace {

const ConceptHit* hitOf(const Interpretation& in, const char* id) {
    for (const auto& h : in.hits)
        if (h.conceptId == id) return &h;
    return nullptr;
}

Interpretation read(const char* text) { return interpret(text, Lexicon::builtin()); }

bool has(const std::vector<std::string>& v, const char* s) { return std::find(v.begin(), v.end(), s) != v.end(); }

const char* kLibrary = R"(
[2hollis - poster boy]
artiste = 2hollis
titre = poster boy
alias = 2 hollis
genres = rage | hyperpop
lufs = -3.7
mid = 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
side = -10 -10 -10 -10 -10 -10 -10 -10 -10 -10 -10 -10 -10 -10 -10 -10 -10 -10 -10 -10 -10
dyn = 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5
sub = 8

[SLAYR - Sloppy Joe]
artiste = SLAYR
titre = Sloppy Joe
genres = rage
lufs = -6
mid = 3 3 3 3 3 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
side = -12 -12 -12 -12 -12 -12 -12 -12 -12 -12 -12 -12 -12 -12 -12 -12 -12 -12 -12 -12 -12
dyn = 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4
sub = 10
)";

} // namespace

OMNIS_TEST("texte: fautes de frappe et d'orthographe comprises, sans faux amis") {
    OMNIS_CHECK(fuzzyEquals("crispi", "crispy"));
    OMNIS_CHECK(fuzzyEquals("krispy", "crispy"));
    OMNIS_CHECK(fuzzyEquals("propr", "propre"));
    OMNIS_CHECK(fuzzyEquals("brilan", "brillant"));
    OMNIS_CHECK(fuzzyEquals("dinamique", "dynamique"));
    OMNIS_CHECK(fuzzyEquals("bouheux", "boueux"));
    OMNIS_CHECK(fuzzyEquals("agresif", "agressif"));
    OMNIS_CHECK(fuzzyEquals("lumineu", "lumineux"));
    OMNIS_CHECK(normalizeText("FOOORT") == "fort");
    OMNIS_CHECK(normalizeText("Très ÉTOUFFÉ") == "tres etouffe");
    OMNIS_CHECK(!fuzzyEquals("mort", "fort"));
    OMNIS_CHECK(!fuzzyEquals("masse", "basse"));
    OMNIS_CHECK(!fuzzyEquals("large", "lourd"));
}

OMNIS_TEST("lexique: la base livrée se lit sans erreur et couvre les nuances") {
    Lexicon l;
    std::string error;
    OMNIS_CHECK(l.parse(data::lexicon(), &error));
    OMNIS_CHECK(error.empty());
    OMNIS_CHECK(l.concepts().size() >= 40);
    // Réécriture puis relecture : même base (c'est le format de l'apprentissage).
    Lexicon again;
    OMNIS_CHECK(again.parse(l.serialize()));
    OMNIS_CHECK(again.concepts().size() == l.concepts().size());
    OMNIS_CHECK_NEAR(again.find("crispy")->effect[Axis::Presence], 1.5, 1e-12);
}

OMNIS_TEST("demande: « ça manque de crispy », « c'est trop brillant », « trop propre » (souhait)") {
    const auto a = read("ça manque de crispy");
    OMNIS_CHECK(hitOf(a, "crispy") && hitOf(a, "crispy")->weight > 0.0);
    OMNIS_CHECK_NEAR(a.desire[Axis::Presence], 1.5, 1e-9);

    const auto b = read("c'est trop brillant");
    OMNIS_CHECK_NEAR(b.desire[Axis::Air], -2.0, 1e-9);

    const auto c = read("je veux que ce soit trop propre");
    OMNIS_CHECK_NEAR(c.desire[Axis::Cleanliness], 1.6, 1e-9);

    const auto d = read("jveu ke sa sonne + crispi stp");
    OMNIS_CHECK(hitOf(d, "crispy") != nullptr);
}

OMNIS_TEST("demande: un défaut nommé est une plainte, « trop » ou pas") {
    const auto a = read("c'est boueux");
    const auto b = read("trop bouheux dans les bas");
    OMNIS_CHECK_NEAR(a.desire[Axis::LowMid], -2.0, 1e-9);
    OMNIS_CHECK(b.desire[Axis::LowMid] < -1.9);
    const auto c = read("la voix est étouffée");
    OMNIS_CHECK(c.desire[Axis::Air] > 1.9);
}

OMNIS_TEST("demande: négations (« c'est pas propre » = veut du propre, « pas de disto » = sans)") {
    OMNIS_CHECK(read("c'est pas propre").desire[Axis::Cleanliness] > 0.9);
    const auto b = read("je veux pas de disto");
    OMNIS_CHECK(b.desire[Axis::Saturation] < -0.9);
    OMNIS_CHECK(b.desire[Axis::Cleanliness] > 0.9);
    OMNIS_CHECK(read("c'est pas assez fort").desire[Axis::Loudness] > 1.9);
}

OMNIS_TEST("demande: intensité (« un poil », « vraiment »), protection (« garde ma 808 »)") {
    OMNIS_CHECK_NEAR(read("un poil plus large").desire[Axis::Width], 1.0, 1e-9);
    OMNIS_CHECK_NEAR(read("vraiment plus large").desire[Axis::Width], 3.2, 1e-9);
    const auto r = parseRequest("garde ma 808 mais rends le plus brillant");
    OMNIS_CHECK_NEAR(r.subLimitDb, 1.0, 1e-12);
    OMNIS_CHECK(r.protectedRegions[0]);
    OMNIS_CHECK(r.midBiasDb[0] == 0.0); // la protection n'ajoute rien au sub
    OMNIS_CHECK(r.midBiasDb[19] > 1.5);
}

OMNIS_TEST("demande: références, ressemblance, streaming, mots inconnus") {
    const auto a = read("un son comme 2hollis mais plus propre");
    OMNIS_CHECK(has(a.referenceMentions, "2hollis"));
    OMNIS_CHECK(a.desire[Axis::Cleanliness] > 0.9);
    OMNIS_CHECK_NEAR(read("exactement comme slayr").desire[Axis::Resemblance], 0.3, 1e-9);
    OMNIS_CHECK_NEAR(read("un peu comme slayr").desire[Axis::Resemblance], -0.2, 1e-9);
    OMNIS_CHECK(read("pour spotify").lufs.value_or(0.0) == -14.0);
    OMNIS_CHECK(read("comme la ref").mentionsProvidedReference);
    OMNIS_CHECK(has(read("fais un truc zinzolin").unknownWords, "zinzolin"));
}

OMNIS_TEST("références: recherche tolérante (titre, artiste, alias, genre) et moyenne") {
    ReferenceLibrary lib;
    OMNIS_CHECK(lib.parse(kLibrary));
    OMNIS_CHECK(lib.entries().size() == 2);
    OMNIS_CHECK(lib.find("2 holis").entries.size() == 1);
    OMNIS_CHECK(lib.find("poster boy").how.rfind("titre", 0) == 0);
    OMNIS_CHECK(lib.find("slayer").found());
    OMNIS_CHECK(lib.find("rage").entries.size() == 2);
    OMNIS_CHECK(!lib.find("yeat").found());
    const auto avg = aggregateSignature(lib.find("rage").entries);
    OMNIS_CHECK_NEAR(avg.integratedLufs, -4.85, 1e-9);
    // Relecture de la base écrite.
    ReferenceLibrary again;
    OMNIS_CHECK(again.parse(lib.serialize()));
    OMNIS_CHECK(again.entries().size() == 2);
    OMNIS_CHECK_NEAR(again.entries()[1].signature.subEnergyDb, 10.0, 1e-9);
}

OMNIS_TEST("compréhension: artiste inconnu -> question ; son fourni -> on avance") {
    ReferenceLibrary lib;
    lib.parse(kLibrary);
    const auto u = understandRequest("comme yeat, bien fort", Lexicon::builtin(), lib, false);
    OMNIS_CHECK(u.question.has_value());
    OMNIS_CHECK(u.question->find("yeat") != std::string::npos);
    OMNIS_CHECK(u.question->find("2hollis") != std::string::npos); // propose un artiste connu
    OMNIS_CHECK(!understandRequest("comme yeat, bien fort", Lexicon::builtin(), lib, true).question.has_value());

    const auto k = understandRequest("un son rage mais garde ma 808", Lexicon::builtin(), lib, false);
    OMNIS_CHECK(!k.question.has_value());
    OMNIS_CHECK(k.referenceEntries.size() == 2); // genre « rage » trouvé sans « comme »

    OMNIS_CHECK(understandRequest("comme la ref", Lexicon::builtin(), lib, false).question.has_value());
    OMNIS_CHECK(understandRequest("bla bla", Lexicon::builtin(), lib, false).question.has_value());
}

OMNIS_TEST("demande: phrases réelles (« ça claque », « dans les aigus », « pas trop fort », langage SMS)") {
    const auto a = read("je veu que sa sois propr et que ça claque");
    OMNIS_CHECK(hitOf(a, "punch") != nullptr); // pas « ça craque »
    OMNIS_CHECK(hitOf(a, "distordu") == nullptr);
    OMNIS_CHECK(a.desire[Axis::Cleanliness] > 0.9);
    OMNIS_CHECK(a.unknownWords.empty());

    const auto b = read("ça manque de crispy dans les aigus");
    OMNIS_CHECK(hitOf(b, "aigus") && hitOf(b, "aigus")->weight > 0.0); // « les » n'est pas « less »

    OMNIS_CHECK(read("genre slayer mais pas trop fort").desire[Axis::Loudness] < 0.0);

    const auto c = read("c trop sombre jsp ça sonne étoufé");
    OMNIS_CHECK(c.desire[Axis::Air] > 3.0);
    OMNIS_CHECK(c.unknownWords.empty());

    // Trois remarques sur les aigus : direction, pas +12 dB.
    const auto r = parseRequest("plus brillant, plus d'air, plus d'aigus, encore plus brillant");
    for (double v : r.midBiasDb) OMNIS_CHECK(v <= 4.0 + 1e-9);
}

OMNIS_TEST("références: artiste connu de nom -> profil de son genre, puis question si genre inconnu") {
    ReferenceLibrary lib;
    lib.parse(kLibrary);
    lib.parse("[Yeat]\nartiste = Yeat\ngenres = rage\n\n[Inconnu Total]\nartiste = Zorglub\ngenres = zouk\n");
    OMNIS_CHECK(lib.entries().size() == 4);
    OMNIS_CHECK(!lib.find("yeat").entries.front()->measured);

    const auto u = understandRequest("comme yeat, fort", Lexicon::builtin(), lib, false);
    OMNIS_CHECK(!u.question.has_value());
    OMNIS_CHECK(u.suggestion.has_value());
    OMNIS_CHECK(u.referenceEntries.size() == 2); // les deux titres « rage » mesurés
    for (const auto* e : u.referenceEntries) OMNIS_CHECK(e->measured);

    const auto z = understandRequest("comme zorglub", Lexicon::builtin(), lib, false);
    OMNIS_CHECK(z.question.has_value());
    OMNIS_CHECK(!understandRequest("comme zorglub", Lexicon::builtin(), lib, true).question.has_value());

    // Une entrée de nom ne remplace jamais des mesures (elle complète les genres).
    lib.parse("[2hollis]\nartiste = 2hollis\ntitre = poster boy\ngenres = electronique\n");
    const auto m = lib.find("poster boy");
    OMNIS_CHECK(m.entries.front()->measured);
    OMNIS_CHECK(m.entries.front()->genres.size() == 3);
    // Relecture : l'entrée de nom reste sans mesures.
    ReferenceLibrary again;
    again.parse(lib.serialize());
    OMNIS_CHECK(!again.find("yeat").entries.front()->measured);
}

OMNIS_TEST("références: un nom de la base n'est jamais lu comme un mot (« ken carsonn » != « carton »)") {
    ReferenceLibrary lib;
    lib.parse(kLibrary);
    lib.parse("[Ken Carson]\nartiste = Ken Carson\ngenres = rage\n");
    const auto u = understandRequest("genre ken carsonn mais propre", Lexicon::builtin(), lib, false);
    OMNIS_CHECK(hitOf(u.interpretation, "carton") == nullptr);
    OMNIS_CHECK(hitOf(u.interpretation, "propre") != nullptr);
    OMNIS_CHECK(u.referenceName.find("Ken Carson") != std::string::npos);
    OMNIS_CHECK(!u.question.has_value());
    // Un mot court n'est jamais pris pour un genre voisin (« trop » / « trap »).
    OMNIS_CHECK(!namesMatch("trop", "trap"));
    OMNIS_CHECK(namesMatch("slayer", "slayr"));
}

OMNIS_TEST("catalogue livré: artistes et titres reconnus sans faux amis, réponses honnêtes") {
    const auto& lib = ReferenceLibrary::builtin();
    OMNIS_CHECK(lib.entries().size() > 3000);
    const auto ask = [&](const char* t) { return understandRequest(t, Lexicon::builtin(), lib, false); };

    // Connus de nom ET classés « rage » par la personne : on vise le profil
    // rage écouté (2hollis, SLAYR) en suggérant d'envoyer un son.
    const auto said = [](const Understanding& u) { return u.suggestion.value_or("") + u.question.value_or(""); };
    // « la feve » est l'artiste, pas un de ses titres : ses sons écoutés.
    const auto a = ask("garde ma 808 comme la feve");
    OMNIS_CHECK(a.referenceName.find("artiste La Fève") != std::string::npos);
    OMNIS_CHECK(a.referenceEntries.size() >= 5 && !a.question.has_value());
    for (const auto* e : a.referenceEntries) OMNIS_CHECK(e->artist == "La Fève" && e->measured);
    OMNIS_CHECK(a.request.protectedRegions[0]);
    // « fein de travis scott » -> le titre FE!N.
    OMNIS_CHECK(said(ask("un son comme fein de travis scott")).find("FE!N") != std::string::npos);
    // « ken carsonn » : l'artiste (pas « carton », pas « Ken Carson - Pots »).
    const auto k = ask("genre ken carsonn mais propre");
    OMNIS_CHECK(hitOf(k.interpretation, "carton") == nullptr);
    OMNIS_CHECK(said(k).find("Ken Carson de nom") != std::string::npos);
    // Prénom seul (« kanye ») ; genre en deux mots (« uk bass »).
    const std::string ks = said(ask("un son comme black skinhead de kanye"));
    OMNIS_CHECK(ks.find("quannnic") == std::string::npos);
    OMNIS_CHECK(ks.find("Kanye West, Kanye West") == std::string::npos); // pas de doublon titre / artiste
    OMNIS_CHECK(said(ask("genre uk bass bien propre")).find("« uk »") == std::string::npos);
    // Un mot courant qui est aussi un titre ne devient pas une référence.
    const auto i = ask("intro un peu plus large");
    OMNIS_CHECK(!i.hasReference() && !i.question.has_value());
    // Genre sans aucun son écouté : on le dit ; genre écouté : on y va.
    OMNIS_CHECK(ask("trop brillant, genre rock").question.value_or("").find("aucun son rock") != std::string::npos);
    const auto fr = ask("trop brillant, genre rap fr");
    OMNIS_CHECK(!fr.question.has_value() && fr.referenceEntries.size() >= 20);
    const auto h = ask("un son hyperpop crispy");
    OMNIS_CHECK(!h.question.has_value());
    const auto heard = [&](const char* artist) {
        return std::any_of(h.referenceEntries.begin(), h.referenceEntries.end(), [&](const ReferenceEntry* e) { return e->artist == artist; });
    };
    OMNIS_CHECK(heard("2hollis") && heard("SLAYR")); // SLAYR : classé hyperpop par l'utilisateur
    OMNIS_CHECK(heard("Luther")); // classé hyperpop par ses playlists
}

OMNIS_TEST("références: des mesures remplacent une entrée de nom sans perdre ses genres") {
    ReferenceLibrary lib;
    lib.parse("[Kanye West - Power]\nartiste = Kanye West\ntitre = Power\ngenres = rap us | maximaliste\n");
    ReferenceEntry measured;
    measured.artist = "Kanye West";
    measured.title = "Power";
    measured.signature.integratedLufs = -7.0;
    lib.add(measured);
    OMNIS_CHECK(lib.entries().size() == 1);
    const auto& e = lib.entries().front();
    OMNIS_CHECK(e.measured);
    OMNIS_CHECK(e.genres.size() == 2);
    OMNIS_CHECK_NEAR(e.signature.integratedLufs, -7.0, 1e-12);
}

OMNIS_TEST("références: liens entre artistes (« proches ») lus depuis une recherche, mise en forme tolérée") {
    ReferenceEntry e;
    OMNIS_CHECK(parseArtistLink("Yeat ; rap us ; rage, trap ; proches : Ken Carson, SLAYR, Destroy Lonely ; traits : 808 saturée, cloches", e));
    OMNIS_CHECK(e.artist == "Yeat" && !e.measured);
    OMNIS_CHECK(e.genres.size() == 3 && e.genres[0] == "rap us" && e.genres[2] == "trap");
    OMNIS_CHECK(e.similar.size() == 3 && e.similar[1] == "SLAYR");
    OMNIS_CHECK(e.traits == "808 saturée, cloches");
    // Puces, numéros, gras, champs nommés dans le désordre ; « 21 Savage » garde son 21.
    OMNIS_CHECK(parseArtistLink("12. **21 Savage** ; Famille : rap us ; Styles : trap ; Traits sonores : voix posée ; Proches: Metro Boomin", e));
    OMNIS_CHECK(e.artist == "21 Savage");
    OMNIS_CHECK(e.genres.size() == 2 && e.similar.size() == 1 && e.traits == "voix posée");
    OMNIS_CHECK(parseArtistLink("- Artiste : La Fève ; rap fr ; new wave", e) && e.artist == "La Fève" && e.similar.empty());
    OMNIS_CHECK(parseArtistLink("Earl Sweatshirt ; rap us ; abstract ; proches : Tyler, The Creator | MIKE | Navy Blue", e));
    OMNIS_CHECK(e.similar.size() == 3 && e.similar[0] == "Tyler, The Creator");
    OMNIS_CHECK(!parseArtistLink("# Artiste ; famille ; styles", e));
    OMNIS_CHECK(!parseArtistLink("Voici la liste demandée :", e));

    // Relecture : proches et traits survivent à la base texte.
    ReferenceLibrary lib;
    parseArtistLink("Yeat ; rap us ; rage ; proches : Ken Carson, SLAYR ; traits : 808 saturée", e);
    lib.add(e);
    ReferenceLibrary again;
    OMNIS_CHECK(again.parse(lib.serialize()));
    OMNIS_CHECK(again.entries().front().similar.size() == 2 && again.entries().front().traits == "808 saturée");
}

OMNIS_TEST("compréhension: artiste connu de nom -> ses proches écoutés avant le profil de son genre") {
    ReferenceLibrary lib;
    lib.parse(kLibrary); // 2hollis (rage, hyperpop) et SLAYR (rage) mesurés
    ReferenceEntry e;
    parseArtistLink("Yeat ; rage ; trap ; proches : Ken Carson, SLAYR ; traits : 808 saturée", e);
    lib.add(e);
    // Proche cité : seul SLAYR (Ken Carson n'est pas mesuré), pas tout le genre rage.
    const auto near = lib.measuredSimilarTo("yeat");
    OMNIS_CHECK(near.artists.size() == 1 && near.artists[0] == "SLAYR");
    const auto u = understandRequest("comme yeat, fort", Lexicon::builtin(), lib, false);
    OMNIS_CHECK(!u.question.has_value());
    OMNIS_CHECK(u.referenceEntries.size() == 1 && u.referenceEntries[0]->artist == "SLAYR");
    OMNIS_CHECK(u.suggestion.value_or("").find("proches") != std::string::npos);
    OMNIS_CHECK(u.suggestion.value_or("").find("SLAYR") != std::string::npos);
    OMNIS_CHECK(std::any_of(u.request.understood.begin(), u.request.understood.end(),
                            [](const std::string& s) { return s.find("808 saturée") != std::string::npos; }));

    // Lien dans l'autre sens : un artiste que personne n'a décrit, mais qu'un autre cite.
    lib.parse("[Che]\nartiste = Che\ngenres = zouk\n\n[2hollis]\nartiste = 2hollis\nproches = Che | Jane Remover\n");
    const auto c = understandRequest("comme che", Lexicon::builtin(), lib, false);
    OMNIS_CHECK(!c.question.has_value());
    OMNIS_CHECK(c.referenceEntries.size() == 1 && c.referenceEntries[0]->artist == "2hollis");

    // Aucun proche écouté : on retombe sur le profil du genre, comme avant.
    lib.parse("[Lancey Foux]\nartiste = Lancey Foux\ngenres = rage\nproches = Inconnu Un | Inconnu Deux\n");
    const auto l = understandRequest("comme lancey foux", Lexicon::builtin(), lib, false);
    OMNIS_CHECK(l.referenceEntries.size() == 2);
    OMNIS_CHECK(l.suggestion.value_or("").find("profil moyen") != std::string::npos);
}
