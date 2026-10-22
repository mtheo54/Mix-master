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
