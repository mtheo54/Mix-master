// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/Feedback.h — Apprendre de chaque séance, chez soi puis pour tout le monde.
//
//  Boucle :
//   1. Chaque demande traitée laisse un RETOUR : les concepts compris, ce que
//      la personne a CORRIGÉ ensuite (différence entre la courbe proposée et
//      celle qu'elle a gardée, par axe), sa note (−1 / 0 / +1), les mots et
//      références que la base ne connaissait pas.
//   2. Apprentissage LOCAL (learnFromFeedback) : si après « crispy » la
//      personne remonte encore la présence de 1 dB, « crispy » vaut un peu
//      plus de présence pour elle (taux 0.3, effet borné à ±6). Un mot
//      inconnu suivi deux fois de corrections cohérentes devient un concept.
//   3. Export ANONYME (exportAggregate) : uniquement des nombres par concept
//      et des mots isolés — jamais d'audio, jamais la phrase entière.
//   4. Fusion COMMUNAUTAIRE (mergeAggregates + applyCommunity) : côté
//      serveur, un changement n'entre dans la base de tout le monde que s'il
//      est confirmé par assez de personnes différentes (défaut 10) et assez
//      d'observations (défaut 20). La base mise à jour est un nouveau paquet
//      versionné (lexique + références) distribué aux plug-ins.
//  Le transport réseau, le consentement et la signature des paquets sont
//  décrits dans docs/APPRENTISSAGE.md ; ce fichier contient toute la logique,
//  testable hors ligne.
// =============================================================================
#pragma once

#include "Lexicon.h"
#include "stages/MatchKernelBank.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omnis::ai {

struct FeedbackRecord {
    std::string request;                ///< phrase (reste sur la machine, jamais exportée)
    std::vector<ConceptHit> hits;       ///< concepts compris (id, poids)
    Descriptor correction;              ///< ce que la personne a changé ensuite, par axe
    int rating = 0;                     ///< −1, 0, +1
    std::vector<std::string> unknownWords;
    std::vector<std::string> unknownReferences;
    std::string reference;              ///< référence utilisée
};

/// Une ligne de journal (champs séparés par des tabulations).
std::string serializeFeedback(const FeedbackRecord& record);
std::optional<FeedbackRecord> parseFeedback(std::string_view line);
bool appendFeedback(const std::string& path, const FeedbackRecord& record);
std::vector<FeedbackRecord> loadFeedback(const std::string& path);

/// Correction par axe d'après la courbe proposée et celle que la personne a
/// gardée (moyenne par région, largeur = variation Side − Mid au-dessus de
/// 640 Hz) et la sonie finale.
Descriptor correctionFromCurves(const MatchCurve& proposedMid, const MatchCurve& keptMid, const MatchCurve& proposedSide,
                                const MatchCurve& keptSide, double proposedLufs, double keptLufs);

/// Lecture d'une correction écrite (« presence=+1.5, sub=-1 »).
std::optional<Descriptor> parseCorrection(std::string_view text);

struct LearningReport {
    int recordsUsed = 0;
    std::vector<std::string> changes; ///< lisibles
};

/// Apprentissage local : modifie `lexicon` (à enregistrer comme lexique utilisateur).
LearningReport learnFromFeedback(Lexicon& lexicon, const std::vector<FeedbackRecord>& records, double rate = 0.3);

/// Export anonyme d'une machine (texte).
std::string exportAggregate(const std::vector<FeedbackRecord>& records);
/// Somme de plusieurs exports (serveur).
std::string mergeAggregates(const std::vector<std::string>& aggregates);

struct CommunityRules {
    int minContributors = 10;
    int minObservations = 20;
    double rate = 0.3;
};

/// Applique un agrégat communautaire à la base : seules les tendances
/// confirmées entrent (concepts ajustés, mots appris). Rapport lisible.
LearningReport applyCommunity(Lexicon& lexicon, std::string_view mergedAggregate, const CommunityRules& rules = {});

} // namespace omnis::ai
