// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/ReferenceLibrary.h — La base de références : artistes, titres, genres.
//
//  Une entrée = une SIGNATURE mesurée (analysis::SpectralSignature : balance
//  M/S par nœud, énergie du sub, dynamique par bande, sonie). Jamais l'audio :
//  la base est faite de nombres, elle se partage et se met à jour sans
//  redistribuer aucune musique.
//
//  Recherche tolérante aux fautes (« 2holis », « deux hollis » via les alias,
//  « slayer » pour SLAYR) sur l'artiste, le titre, les alias et les genres.
//  Plusieurs titres (un artiste, un genre) -> signature moyenne : ce qui est
//  commun à tous ces titres est justement ce qui est persistant.
//
//  Format texte (data/references.txt, base utilisateur) :
//      [artiste - titre]
//      artiste = 2hollis
//      titre = poster boy
//      alias = 2 hollis | two hollis
//      genres = rage | hyperpop
//      lufs = -3.7          lra = …   tp = …   sub = …   (une clé par ligne)
//      mid = 21 valeurs     side = 21 valeurs     dyn = 21 valeurs
//  Une entrée sans mesures (pas de « mid ») est un artiste connu DE NOM : il
//  est reconnu dans la demande et rattaché à ses genres, le temps qu'un son
//  permette de le mesurer.
// =============================================================================
#pragma once

#include "analysis/SpectralSignature.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace omnis::ai {

struct ReferenceEntry {
    std::string artist;
    std::string title;
    std::vector<std::string> aliases;
    std::vector<std::string> genres;
    analysis::SpectralSignature signature;
    bool measured = true; ///< false : connu de nom (et de genre) seulement, en attente d'un son

    [[nodiscard]] std::string displayName() const;
};

struct ReferenceMatch {
    enum class Kind { None, Title, Artist, Genre };
    Kind kind = Kind::None;
    bool exact = false; ///< noms compactés identiques (sinon : faute tolérée)
    std::vector<const ReferenceEntry*> entries;
    std::string how; ///< « artiste 2hollis (1 titre) », « genre rage (2 titres) »
    [[nodiscard]] bool found() const noexcept { return !entries.empty(); }
};

class ReferenceLibrary {
public:
    bool parse(std::string_view text, std::string* error = nullptr);
    [[nodiscard]] std::string serialize() const;

    /// Remplace l'entrée de même artiste et même titre (noms compactés identiques).
    void add(ReferenceEntry entry);
    [[nodiscard]] const std::vector<ReferenceEntry>& entries() const noexcept { return entries_; }

    /// Noms exacts d'abord (« artiste titre » > artiste > titre), puis les mêmes
    /// avec fautes tolérées, puis les genres.
    [[nodiscard]] ReferenceMatch find(std::string_view query) const;

    /// Un titre parmi les entrées d'un artiste déjà reconnu, comparé plus
    /// largement (« fein de travis scott » -> « FE!N »).
    [[nodiscard]] ReferenceMatch findTitleOf(const std::vector<const ReferenceEntry*>& artistEntries, std::string_view title) const;

    /// Entrées MESURÉES portant ce genre, le leur ou celui de leur artiste
    /// (entrée sans titre : « SLAYR ; hyperpop » vaut pour tous ses titres).
    [[nodiscard]] std::vector<const ReferenceEntry*> measuredWithGenre(std::string_view genre) const;
    /// Artistes dont au moins un titre est mesuré (ceux qu'OMNIS a écoutés).
    [[nodiscard]] std::vector<std::string> measuredArtists() const;

    [[nodiscard]] std::vector<std::string> artists() const;
    [[nodiscard]] std::vector<std::string> genres() const;

    /// Base livrée avec le plug-in (data/references.txt).
    static const ReferenceLibrary& builtin();

    /// Clé de comparaison d'un nom, calculée une fois (la base peut compter des
    /// milliers de titres : la recherche ne retokenise rien).
    struct NameKey {
        std::string compact, digits, phonetic;
        [[nodiscard]] bool matches(const NameKey& other) const noexcept;
        static NameKey of(std::string_view name);
    };

private:
    struct EntryKeys {
        NameKey artist, title, artistTitle, titleArtist;
        std::vector<NameKey> aliases, genres;
    };
    void index(std::size_t i);

    std::vector<ReferenceEntry> entries_;
    std::vector<EntryKeys> keys_;
    std::unordered_map<std::string, std::size_t> exact_; ///< « artiste|titre » compacts -> index
    std::unordered_map<std::string, std::size_t> artistLevel_; ///< artiste compact -> entrée sans titre
};

/// Signature moyenne (balances relatives au corps, sonies moyennes) des
/// entrées MESURÉES ; les entrées connues de nom seulement sont ignorées.
analysis::SpectralSignature aggregateSignature(const std::vector<const ReferenceEntry*>& entries);

/// Comparaison tolérante de deux noms (« 2 hollis » = « 2hollis » = « 2holis ») ;
/// exacte sous 5 caractères (« trop » n'est pas le genre « trap »).
bool namesMatch(std::string_view a, std::string_view b);

} // namespace omnis::ai
