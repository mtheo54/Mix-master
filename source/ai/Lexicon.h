// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/Lexicon.h — Des mots de la personne aux axes sonores.
//
//  Le lexique est une DONNÉE (data/lexique_fr.txt, compilé dans le plug-in et
//  surchargeable par un fichier utilisateur) : on l'agrandit sans toucher au
//  code, et l'apprentissage (ai/Feedback.h) le réécrit.
//
//  interpret() lit une phrase écrite vite, fautes comprises (ai/TextMatch.h) :
//    - concepts du lexique, expression la plus longue d'abord ;
//    - petite grammaire autour de chaque concept, dans sa proposition
//      (découpée sur la ponctuation et « mais », « et », « par contre ») :
//        « manque de », « pas assez », « plus », « rajoute »  -> +
//        « moins », « baisse », « enlève »                    -> −
//        « trop »  -> − (« c'est trop brillant »), mais intensif après un
//                     souhait (« je veux que ce soit trop propre »)
//        « pas », « sans » -> −, sauf après un constat (« c'est pas propre »
//                     = la personne veut du propre)
//        « très », « vraiment », « grave »… ×1.6 ; « un peu », « un poil »… ×0.5
//        « garde », « protège », « touche pas »… -> le concept est PROTÉGÉ
//    - un « défaut » (« boueux », « ça pique ») est toujours une plainte : on
//      applique l'inverse de ce qu'il décrit, « trop » ou pas ;
//    - références : ce qui suit « comme », « genre », « style », « façon »,
//      « dans l'esprit de »… (résolu par ai/ReferenceLibrary) ; « exactement
//      comme » / « un peu comme » modulent la ressemblance ;
//    - sonie explicite : « -8 lufs », « −8,5 LUFS », « 8lufs ».
//    - un élément (« la 808 », « the vocals ») suivi d'un niveau (« est trop
//      forte », « need to be louder ») : l'élément monte ou baisse, la sonie du
//      master ne bouge pas ; « la basse et le kick sont trop forts » : les deux ;
//    - modificateurs placés après : « propre de ouf », « brighter a bit »,
//      « turn the bass down » ;
//    - abréviations, SMS et argot (data/argot.txt) réécrits d'abord ; anglais
//      courant compris (« more », « less », « too », « a bit », « not enough »).
//  Les mots non compris sont rendus : ils alimentent la base (apprentissage).
//  Les mots grammaticaux anglais très courts (« less », « so », « no ») sont
//  comparés exactement : la tolérance aux fautes les confondait avec « les », « sa »…
// =============================================================================
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omnis::ai {

enum class Axis : int {
    Sub,
    Bass,
    LowMid,
    Mid,
    Presence,
    Air,
    Width,
    Loudness,
    Cleanliness,
    Resemblance,
    Punch,
    Density,
    Saturation,
    Count
};
inline constexpr int kAxisCount = static_cast<int>(Axis::Count);
inline constexpr int kTonalAxes = 6; ///< Sub … Air : régions du Match EQ

const char* axisKey(Axis axis) noexcept;   ///< clé du fichier (« presence »)
const char* axisLabel(Axis axis) noexcept; ///< libellé affiché (« présence »)
const char* axisUnit(Axis axis) noexcept;
/// false : axe compris et mémorisé, mais l'étage qui l'applique n'existe pas encore.
bool axisApplied(Axis axis) noexcept;
std::optional<Axis> axisFromKey(std::string_view key) noexcept;

struct Descriptor {
    std::array<double, kAxisCount> v{};
    double& operator[](Axis a) noexcept { return v[static_cast<std::size_t>(a)]; }
    double operator[](Axis a) const noexcept { return v[static_cast<std::size_t>(a)]; }
};

struct Concept {
    std::string id;
    bool flaw = false; ///< « defaut » : nommé = plainte
    std::vector<std::string> expressions;              ///< telles qu'écrites
    std::vector<std::vector<std::string>> tokenized;   ///< normalisées
    Descriptor effect;
    std::optional<double> targetLufs;
};

class Lexicon {
public:
    /// Ajoute ou remplace (même id) les concepts du texte. false + message si
    /// une ligne est invalide (les autres sont gardées).
    bool parse(std::string_view text, std::string* error = nullptr);
    [[nodiscard]] std::string serialize() const;

    void add(Concept entry); ///< remplace un concept du même id

    [[nodiscard]] const std::vector<Concept>& concepts() const noexcept { return concepts_; }
    Concept* find(std::string_view id) noexcept;
    [[nodiscard]] const Concept* find(std::string_view id) const noexcept;

    /// Lexique livré avec le plug-in (data/lexique_fr.txt).
    static const Lexicon& builtin();

private:
    std::vector<Concept> concepts_;
};

struct ConceptHit {
    std::string conceptId;
    std::string text;      ///< mots de la personne
    double weight = 0.0;   ///< signe et intensité appliqués à l'effet
    bool protect = false;  ///< « garde ma 808 »
};

struct Interpretation {
    Descriptor desire;
    std::optional<double> lufs;               ///< sonie absolue demandée
    std::vector<ConceptHit> hits;
    std::vector<std::string> unknownWords;
    std::vector<std::string> referenceMentions; ///< après « comme », « genre »…
    bool mentionsProvidedReference = false;     ///< « comme la réf », « comme le son »
};

/// Jeton qui remplace un nom de la base de références déjà reconnu
/// (ai::understandRequest) : ignoré par interpret(), jamais pris pour un mot.
inline constexpr const char* kReferenceMarker = "omnisrefmark";

/// Abréviations, SMS et argot (data/argot.txt) réécrits en mots que la grammaire
/// et le lexique connaissent : « tro d bass stp » -> « trop d bass ». Comparaison
/// exacte des jetons, forme la plus longue d'abord.
std::vector<std::string> expandShorthand(std::vector<std::string> tokens);

Interpretation interpret(std::string_view text, const Lexicon& lexicon);

/// Mot outil (« je », « de », « mix »…) ou mot de la grammaire (« trop », « plus »).
bool isCommonWord(const std::string& normalizedToken);

} // namespace omnis::ai
