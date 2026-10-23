// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/EmbeddedData.h — Données livrées dans le binaire (générées depuis data/).
//
//  data/lexique_fr.txt, data/argot.txt, data/references.txt et data/catalogue.txt sont convertis en tableaux
//  d'octets à la configuration CMake (cmake/OmnisEmbedData.cmake) : le plug-in
//  fonctionne sans aucun fichier externe, et les fichiers restent la source
//  éditable de la base.
// =============================================================================
#pragma once

#include <string_view>

namespace omnis::ai::data {

std::string_view lexicon() noexcept;
std::string_view slang() noexcept;      ///< abréviations, SMS, argot (data/argot.txt)
std::string_view references() noexcept;
std::string_view catalogue() noexcept; ///< artistes et titres connus de nom (data/catalogue.txt)

} // namespace omnis::ai::data
