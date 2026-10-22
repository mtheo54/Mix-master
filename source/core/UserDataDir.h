// =============================================================================
//  Abrasion Labs // OMNIS
//  core/UserDataDir.h — Dossier de données de la personne (hors thread audio).
//
//      Windows : %APPDATA%\Abrasion Labs\OMNIS
//      macOS   : ~/Library/Application Support/Abrasion Labs/OMNIS
//      Linux   : $XDG_DATA_HOME (ou ~/.local/share)/Abrasion Labs/OMNIS
//  OMNIS_USER_DATA_DIR remplace ce chemin (tests, installations portables).
//  On y trouve la base de références de la personne (references_utilisateur.txt),
//  son lexique appris et son journal de retours.
// =============================================================================
#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

namespace omnis {

inline std::string environmentVariable(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    std::string out;
    if (_dupenv_s(&value, &length, name) == 0 && value) out = value;
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

inline std::filesystem::path userDataDir() {
    if (const auto custom = environmentVariable("OMNIS_USER_DATA_DIR"); !custom.empty()) return custom;
    std::filesystem::path base;
#if defined(_WIN32)
    base = environmentVariable("APPDATA");
#elif defined(__APPLE__)
    base = std::filesystem::path(environmentVariable("HOME")) / "Library" / "Application Support";
#else
    const auto xdg = environmentVariable("XDG_DATA_HOME");
    base = xdg.empty() ? std::filesystem::path(environmentVariable("HOME")) / ".local" / "share" : std::filesystem::path(xdg);
#endif
    return base / "Abrasion Labs" / "OMNIS";
}

} // namespace omnis
