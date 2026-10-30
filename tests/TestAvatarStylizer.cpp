// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestAvatarStylizer.cpp — L'illustration d'une pochette : macaron du
//  coin retiré, grain effacé, teinte gardée, trait sur les contours, détourage.
// =============================================================================
#include "TestFramework.h"

#include "gfx/AvatarCache.h"
#include "gfx/OmnisAvatarStylizer.h"

#include "stb_image_write.h"

#include <cmath>
#include <random>

using omnis::gfx::OmnisAvatarStylizer;
using omnis::gfx::RgbaImage;

namespace {

/// Une « pochette » 600 × 600 : fond nuit, sujet chaud au centre (un peu haut),
/// grain photo, et un macaron noir et blanc rayé dans le coin bas droit.
std::vector<std::uint8_t> cover() {
    const int n = 600;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(n * n * 3));
    std::mt19937 rng(3);
    std::normal_distribution<float> grain(0.0f, 10.0f);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float r = 30, g = 40, b = 70; // nuit
            if ((x - 300) * (x - 300) + (y - 270) * (y - 270) < 110 * 110) r = 190, g = 70, b = 50; // sujet chaud
            if (x > 470 && y > 500) { // macaron : bandes noires et blanches
                const bool white = ((y - 500) / 12) % 2 == 0;
                r = g = b = white ? 255.0f : 0.0f;
            }
            const float e = (x > 470 && y > 500) ? 0.0f : grain(rng);
            auto* p = &rgb[static_cast<std::size_t>((y * n + x) * 3)];
            p[0] = static_cast<std::uint8_t>(std::clamp(r + e, 0.0f, 255.0f));
            p[1] = static_cast<std::uint8_t>(std::clamp(g + e, 0.0f, 255.0f));
            p[2] = static_cast<std::uint8_t>(std::clamp(b + e, 0.0f, 255.0f));
        }
    std::vector<std::uint8_t> png;
    stbi_write_png_to_func([](void* ctx, void* data, int size) {
        auto* out = static_cast<std::vector<std::uint8_t>*>(ctx);
        out->insert(out->end(), static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + size);
    }, &png, n, n, 3, rgb.data(), n * 3);
    return png;
}

const std::uint8_t* px(const RgbaImage& im, int x, int y) { return &im.pixels[static_cast<std::size_t>((y * im.width + x) * 4)]; }

} // namespace

OMNIS_TEST("illustration: 256 px, macaron du coin retiré, teinte gardée, trait, détourage") {
    const auto bytes = cover();
    RgbaImage art;
    std::string error;
    OMNIS_CHECK(OmnisAvatarStylizer().stylize(bytes.data(), bytes.size(), art, &error));
    OMNIS_CHECK(art.width == 256 && art.height == 256 && art.pixels.size() == 256u * 256u * 4u);
    // Macaron : aucun blanc pur ni bande noir / blanc dans l'image (le cadrage l'a retiré).
    int white = 0;
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            const auto* p = px(art, x, y);
            if (p[0] > 235 && p[1] > 235 && p[2] > 235) ++white;
        }
    std::printf("      pixels blancs (macaron) : %d\n", white);
    OMNIS_CHECK(white == 0);
    // Teinte gardée : le centre reste chaud (rouge dominant), le fond reste froid.
    const auto* c = px(art, 128, 118);
    const auto* bg = px(art, 128, 22);
    std::printf("      centre %d %d %d, fond %d %d %d\n", c[0], c[1], c[2], bg[0], bg[1], bg[2]);
    OMNIS_CHECK(c[0] > c[1] + 40 && c[0] > c[2] + 40);
    OMNIS_CHECK(bg[2] > bg[0]);
    // Détourage : opaque au centre, transparent aux coins.
    OMNIS_CHECK(px(art, 128, 128)[3] == 255 && px(art, 2, 2)[3] == 0 && px(art, 253, 253)[3] == 0);
    // Pas d'image d'origine en sortie : aucun pixel n'a gardé le grain (voisins égaux dans un aplat).
    double diff = 0;
    for (int x = 110; x < 146; ++x) diff += std::abs(px(art, x, 118)[0] - px(art, x + 1, 118)[0]);
    std::printf("      écart moyen entre voisins dans le sujet : %.2f\n", diff / 36.0);
    OMNIS_CHECK(diff / 36.0 < 3.0);
}

OMNIS_TEST("illustration: étapes (grain effacé par Kuwahara, trait sur le bord du sujet)") {
    // Un disque chaud sur fond froid, grain : après Kuwahara le fond est uni, le
    // trait tombe sur le contour et pas dans les aplats.
    OmnisAvatarStylizer::Rgb im;
    im.width = im.height = 128;
    im.c.resize(3 * 128 * 128);
    std::mt19937 rng(5);
    std::normal_distribution<float> grain(0.0f, 0.04f);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
            const bool in = (x - 64) * (x - 64) + (y - 64) * (y - 64) < 30 * 30;
            const float e = grain(rng);
            float* p = &im.c[static_cast<std::size_t>(3 * (y * 128 + x))];
            p[0] = (in ? 0.75f : 0.12f) + e, p[1] = (in ? 0.3f : 0.16f) + e, p[2] = (in ? 0.2f : 0.28f) + e;
        }
    const auto spread = [&](int y0) {
        double d = 0;
        for (int x = 5; x < 25; ++x) d += std::fabs(im.c[static_cast<std::size_t>(3 * (y0 * 128 + x))] - im.c[static_cast<std::size_t>(3 * (y0 * 128 + x + 1))]);
        return d / 20;
    };
    const double before = spread(10);
    OmnisAvatarStylizer::kuwahara(im, 5);
    OmnisAvatarStylizer::kuwahara(im, 5);
    std::printf("      grain du fond : %.4f -> %.4f\n", before, spread(10));
    OMNIS_CHECK(spread(10) < 0.25 * before);
    const auto ink = OmnisAvatarStylizer::inkMask(im);
    OMNIS_CHECK(ink[static_cast<std::size_t>(64 * 128 + 34)] > 0.5f); // sur le contour (x = 64 - 30)
    OMNIS_CHECK(ink[static_cast<std::size_t>(64 * 128 + 64)] < 0.05f); // centre du sujet
    OMNIS_CHECK(ink[static_cast<std::size_t>(10 * 128 + 10)] < 0.05f); // fond
}

OMNIS_TEST("illustration: cache par nom réduit (accents, casse, espaces)") {
    using namespace omnis::gfx::avatars;
    OMNIS_CHECK(artistPath("La Fève") == artistPath("la feve"));
    OMNIS_CHECK(artistPath("A$AP Rocky") == artistPath("ASAP ROCKY"));
    OMNIS_CHECK(artistPath("GRY") != artistPath("Gryffin"));
    OMNIS_CHECK(coverPath("La Fève", "CINÉMA") == coverPath("la feve", "cinema"));
    OMNIS_CHECK(coverPath("La Fève", "CINÉMA") != coverPath("La Fève", "POÈME"));
    OMNIS_CHECK(artistPath("x").parent_path().filename() == "avatars");
}
