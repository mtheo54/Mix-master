// =============================================================================
//  Abrasion Labs // OMNIS
//  gfx/OmnisAvatarStylizer.h — Une photo d'artiste ou une pochette (octets JPG /
//  PNG tels que téléchargés) devient une illustration peinte 256 × 256 RGBA,
//  la même recette pour tous les artistes :
//
//    1. cadrage : 18 % retirés sur chaque bord, cadre un peu remonté (visage,
//       buste) : le macaron « Parental Advisory » des coins et les textes de
//       bordure partent avec ; un carré centré sur le sujet reste ;
//    2. peinture : bilatéral léger (bruit JPEG), puis filtre de Kuwahara lissé
//       (moyenne des quadrants pondérée par leur calme), 2 passes (la 2e deux
//       fois plus fine) : plus de grain photo, des aplats doux, bords nets ;
//    3. étalonnage : contraste automatique (1 % / 99 % de la luminance), hautes
//       lumières brûlées adoucies, ombres densifiées (gamma 1,15) ; la teinte de
//       chaque image est gardée (YCbCr : seule la luminance bouge) ;
//    4. cel-shading doux : luminance sur 12 paliers reliés par smoothstep (un
//       tiers de l'éclairage d'origine gardé : du volume, pas d'escalier) ;
//    5. encre : Sobel sur la luminance peinte (un peu floutée), seuil adapté, trait
//       fin multiplié (presque noir, à peine bleuté) ;
//    6. détourage : alpha radial (opaque au centre, transparent aux bords) :
//       le sujet sort du fond de la console sans cadre carré.
//
//  L'image d'origine n'existe qu'en mémoire, le temps de stylize() : elle
//  n'est ni écrite ni rendue ; seule l'illustration sort (cache utilisateur,
//  gfx/AvatarCache.h). Une illustration tirée d'une photo ou d'une pochette
//  reste une œuvre dérivée : pour la version vendue, l'accord de l'artiste ou
//  du label reste nécessaire (docs/REFERENCES.md).
//
//  Sans dépendance hors stb_image / stb_image_write. L'interface d'OMNIS est
//  en VSTGUI : la console lit le PNG du cache. toNanoVG() existe pour une
//  interface NanoVG (OMNIS_HAS_NANOVG).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#if defined(OMNIS_HAS_NANOVG)
struct NVGcontext;
#endif

namespace omnis::gfx {

/// RGBA 8 bits, non prémultiplié, ligne par ligne.
struct RgbaImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
    [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0; }
};

class OmnisAvatarStylizer {
public:
    struct Options {
        int size = 256;            ///< côté de l'illustration
        float crop = 0.18f;        ///< part retirée sur chaque bord
        float focusUp = 0.05f;     ///< cadre remonté (part de la hauteur) : visage, buste
        int workSize = 512;        ///< côté de travail des filtres
        int kuwaharaRadius = 6;    ///< 1re passe ; les suivantes deux fois plus fines
        int kuwaharaPasses = 2;
        int levels = 12;           ///< paliers de luminance (10 … 12)
        float shadowGamma = 1.15f; ///< > 1 : ombres plus denses
        float highlightKnee = 0.80f;
        float ink = 0.85f;         ///< force du trait (0 : aucun)
        float vignetteInner = 0.62f, vignetteOuter = 0.98f; ///< rayons (1 = bord) du détourage
    };

    OmnisAvatarStylizer() = default;
    explicit OmnisAvatarStylizer(const Options& options) : o_(options) {}

    /// Octets JPG / PNG -> illustration. false (et `error`) si l'image est illisible.
    bool stylize(const std::uint8_t* data, std::size_t size, RgbaImage& out, std::string* error = nullptr) const;

    /// Plan RGB flottant (0 … 1) des étapes ; publiques pour les tests.
    struct Rgb {
        int width = 0, height = 0;
        std::vector<float> c; ///< 3 × width × height
    };
    /// Cadre (bords retirés, remonté), carré centré, réduit à `side` (moyenne de surface).
    static Rgb cropCenter(const std::uint8_t* rgba, int width, int height, float crop, float focusUp, int side);
    static void denoise(Rgb& image);
    static void kuwahara(Rgb& image, int radius);
    static void grade(Rgb& image, float shadowGamma, float highlightKnee);
    static void celShade(Rgb& image, int levels);
    /// Trait de 0 à 1 (Sobel, seuil adapté à l'image).
    static std::vector<float> inkMask(const Rgb& image);
    /// Trait multiplié, réduction à `size`, alpha radial.
    RgbaImage finish(const Rgb& image, const std::vector<float>& ink) const;

    static bool savePng(const std::filesystem::path& path, const RgbaImage& image, std::string* error = nullptr);

#if defined(OMNIS_HAS_NANOVG)
    /// Texture NanoVG (nvgCreateImageRGBA) ; 0 si échec.
    static int toNanoVG(NVGcontext* vg, const RgbaImage& image);
#endif

private:
    Options o_;
};

} // namespace omnis::gfx
