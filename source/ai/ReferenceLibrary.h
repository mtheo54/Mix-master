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
    std::vector<const ReferenceEntry*> entries;
    std::string how; ///< « artiste 2hollis (1 titre) », « genre rage (2 titres) »
    [[nodiscard]] bool found() const noexcept { return !entries.empty(); }
};

class ReferenceLibrary {
public:
    bool parse(std::string_view text, std::string* error = nullptr);
    [[nodiscard]] std::string serialize() const;

    void add(ReferenceEntry entry); ///< remplace un titre existant du même artiste
    [[nodiscard]] const std::vector<ReferenceEntry>& entries() const noexcept { return entries_; }

    /// Titre exact > artiste > genre.
    [[nodiscard]] ReferenceMatch find(std::string_view query) const;

    [[nodiscard]] std::vector<std::string> artists() const;
    [[nodiscard]] std::vector<std::string> genres() const;

    /// Base livrée avec le plug-in (data/references.txt).
    static const ReferenceLibrary& builtin();

private:
    std::vector<ReferenceEntry> entries_;
};

/// Signature moyenne (balances relatives au corps, sonies moyennes) des
/// entrées MESURÉES ; les entrées connues de nom seulement sont ignorées.
analysis::SpectralSignature aggregateSignature(const std::vector<const ReferenceEntry*>& entries);

/// Comparaison tolérante de deux noms (« 2 hollis » = « 2hollis » = « 2holis ») ;
/// exacte sous 5 caractères (« trop » n'est pas le genre « trap »).
bool namesMatch(std::string_view a, std::string_view b);

} // namespace omnis::ai
