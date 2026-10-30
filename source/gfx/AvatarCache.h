// =============================================================================
//  Abrasion Labs // OMNIS
//  gfx/AvatarCache.h — Où vivent les illustrations (avatars d'artistes,
//  pochettes) : le cache de l'utilisateur, jamais le plug-in ni l'installateur.
//
//    <dossier OMNIS>/cache/avatars/<hash>.png   un artiste
//    <dossier OMNIS>/cache/covers/<hash>.png    un titre (sa pochette)
//
//  Le nom est un hachage (FNV-1a 64 bits) du nom réduit (minuscules, sans
//  accents ni ponctuation) : « La Fève » et « la feve » tombent sur le même
//  fichier. Seules des illustrations (OmnisAvatarStylizer) y sont écrites :
//  aucune image d'origine. En-tête seul : le plug-in s'en sert pour lire.
// =============================================================================
#pragma once

#include "core/UserDataDir.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace omnis::gfx::avatars {

/// Minuscules, sans accents (latin), lettres et chiffres seulement ; « $ » = « s ».
inline std::string reduced(std::string_view text) {
    static constexpr std::string_view kLatin = "aaaaaaaceeeeiiiidnooooo ouuuuytsaaaaaaaceeeeiiiidnooooo ouuuuyty";
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        if (c == 0xC3 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) >= 0x80 &&
            static_cast<unsigned char>(text[i + 1]) <= 0xBF)
            c = static_cast<unsigned char>(kLatin[static_cast<unsigned char>(text[++i]) - 0x80]);
        if (c == '$') c = 's';
        if (c < 0x80 && std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    }
    return out;
}

inline std::string hashName(std::string_view key) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : key) h = (h ^ c) * 1099511628211ull;
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return b;
}

inline std::filesystem::path root() { return userDataDir() / "cache"; }

/// L'avatar d'un artiste.
inline std::filesystem::path artistPath(std::string_view artist) {
    return root() / "avatars" / (hashName("artist:" + reduced(artist)) + ".png");
}

/// La pochette d'un titre (l'artiste et le titre la désignent).
inline std::filesystem::path coverPath(std::string_view artist, std::string_view title) {
    return root() / "covers" / (hashName("cover:" + reduced(artist) + "|" + reduced(title)) + ".png");
}

/// Les deux couleurs dominantes d'une illustration (monogramme du mode promo),
/// à côté du PNG : « <hash>.couleurs », « r g b r g b ».
struct Colors {
    std::array<std::array<std::uint8_t, 3>, 2> rgb{};
    bool valid = false;
};
inline std::filesystem::path colorsPath(std::filesystem::path png) { return png.replace_extension(".couleurs"); }
inline bool writeColors(const std::filesystem::path& png, const std::array<std::array<std::uint8_t, 3>, 2>& rgb) {
    std::ofstream out(colorsPath(png), std::ios::trunc);
    for (const auto& c : rgb) out << int(c[0]) << ' ' << int(c[1]) << ' ' << int(c[2]) << '\n';
    return static_cast<bool>(out);
}
inline Colors readColors(const std::filesystem::path& png) {
    Colors c;
    std::ifstream in(colorsPath(png));
    int v[6];
    for (int& x : v)
        if (!(in >> x)) return c;
    for (int i = 0; i < 6; ++i) c.rgb[static_cast<std::size_t>(i / 3)][static_cast<std::size_t>(i % 3)] = static_cast<std::uint8_t>(std::clamp(v[i], 0, 255));
    c.valid = true;
    return c;
}

/// Chemin UTF-8 d'un fichier présent, vide sinon.
inline std::string existing(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return {};
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

} // namespace omnis::gfx::avatars
