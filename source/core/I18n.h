// =============================================================================
//  Abrasion Labs // OMNIS
//  core/I18n.h — Langue de l'interface : français ou anglais.
//
//  Le code est écrit en français ; en anglais, tr() traduit au moment de
//  l'affichage avec data/i18n_en.txt (embarqué) : d'abord le texte entier, puis
//  les morceaux connus au milieu d'un texte composé (« Stems liés : 3 »). Les
//  chaînes de format (« %.1f dB ») se traduisent avant d'être remplies.
//
//  Langue : celle choisie (« /en », « /fr » dans la barre IA, gardée dans
//  langue.txt du dossier de l'utilisateur), sinon celle du système (français
//  si le système est en français, anglais sinon).
// =============================================================================
#pragma once

#include <string>
#include <string_view>

namespace omnis::i18n {

enum class Lang { Fr, En };

[[nodiscard]] Lang language() noexcept;
/// Choisit la langue et la garde pour les sessions suivantes.
void setLanguage(Lang lang);
/// Tests : langue imposée sans toucher au dossier de l'utilisateur.
void overrideLanguageForTests(Lang lang) noexcept;

/// Le texte dans la langue de l'interface (inchangé en français).
[[nodiscard]] std::string tr(std::string_view french);

} // namespace omnis::i18n
