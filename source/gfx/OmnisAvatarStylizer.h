// =============================================================================
//  Abrasion Labs // OMNIS
//  gfx/OmnisAvatarStylizer.h — Une photo d'artiste ou une pochette (octets JPG /
//  PNG : API publique interrogée par le poste de la personne, ou image intégrée
//  à un FLAC qu'elle a scanné) devient l'avatar 256 × 256 RGBA de la console.
//
//  Style « HD » (par défaut) : l'artiste et l'album se reconnaissent au premier
//  coup d'œil.
//    1. cadrage léger : 9 % retirés sur chaque bord, la composition reste ;
//    2. blocs de texte noir et blanc des coins (macaron « Parental Advisory »,
//       mentions) repérés et comblés par les pixels voisins ;
//    3. bilatéral léger (rayon 2) : le bruit de compression part, les traits
//       restent nets ;
//    4. 90 % de l'image d'origine gardés (10 % d'étalonnage pour le thème
//       sombre : hautes lumières adoucies, ombres un peu denses) ;
//    5. alpha fondu sur les 10 derniers pixels du bord seulement.
//
//  Style « Peint » (option) : Kuwahara, cel-shading, encre, détourage radial.
//
//  Aucune image n'est livrée avec le plug-in : celles-ci vivent dans le cache
//  de la personne (gfx/AvatarCache.h), comme la pochette d'un lecteur de
//  musique. En vidéo / promo, le mode monogramme (« /fbi ») n'en montre aucune.
//
//  Sans dépendance hors stb_image / stb_image_write. La console (VSTGUI) lit le
//  PNG ; toNanoVG() existe pour une interface NanoVG (OMNIS_HAS_NANOVG).
// =============================================================================
#pragma once

#include <array>
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
    enum class Style { HD, Painted };
    struct Options {
        Style style = Style::HD;
        int size = 256;            ///< côté de l'avatar
        float crop = 0.09f;        ///< part retirée sur chaque bord (8 … 10 %)
        float focusUp = 0.0f;      ///< cadre remonté (part de la hauteur)
        bool removeCornerText = true; ///< macaron « Parental Advisory », mentions des coins
        float keep = 0.90f;        ///< HD : part de l'image d'origine gardée
        int featherPx = 10;        ///< HD : fondu alpha sur le bord (pixels de sortie)
        int workSize = 512;        ///< côté de travail des filtres
        // Style « Peint » seulement :
        int kuwaharaRadius = 6;    ///< 1re passe ; les suivantes deux fois plus fines
        int kuwaharaPasses = 2;
        int levels = 12;           ///< paliers de luminance (10 … 12)
        float shadowGamma = 1.15f; ///< > 1 : ombres plus denses
        float highlightKnee = 0.80f;
        // Style « Peint » seulement :
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
    /// Un bloc de texte noir et blanc dans un coin (macaron, mention), en pixels.
    struct Box {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };
    static std::vector<Box> findCornerText(const Rgb& image);
    /// Comble un rectangle par diffusion depuis son pourtour.
    static void inpaint(Rgb& image, const Box& box);
    static void denoise(Rgb& image);
    static void kuwahara(Rgb& image, int radius);
    static void grade(Rgb& image, float shadowGamma, float highlightKnee);
    static void celShade(Rgb& image, int levels);
    /// Trait de 0 à 1 (Sobel, seuil adapté à l'image).
    static std::vector<float> inkMask(const Rgb& image);
    /// Trait multiplié, réduction à `size`, alpha radial.
    RgbaImage finish(const Rgb& image, const std::vector<float>& ink) const;

    /// HD : réduction à `size`, 90 % d'origine, fondu alpha du bord.
    RgbaImage finishHd(const Rgb& image) const;

    /// Les deux couleurs dominantes (peuplées et distinctes) : monogramme du mode promo.
    static std::array<std::array<std::uint8_t, 3>, 2> dominantColors(const RgbaImage& image);

    static bool savePng(const std::filesystem::path& path, const RgbaImage& image, std::string* error = nullptr);

#if defined(OMNIS_HAS_NANOVG)
    /// Texture NanoVG (nvgCreateImageRGBA) ; 0 si échec.
    static int toNanoVG(NVGcontext* vg, const RgbaImage& image);
#endif

private:
    Options o_;
};

} // namespace omnis::gfx
