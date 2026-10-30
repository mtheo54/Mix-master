// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestAvatarStylizer.cpp — L'illustration d'une pochette : macaron du
//  coin retiré, grain effacé, teinte gardée, trait sur les contours, détourage.
// =============================================================================
#include "TestFramework.h"

#include "gfx/AvatarCache.h"
#include "gfx/OmnisAvatarStylizer.h"

#include "AudioRead.h"

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

OMNIS_TEST("avatar HD: 256 px, macaron du coin retiré, couleurs d'origine, fondu sur le bord seulement") {
    const auto bytes = cover();
    RgbaImage art;
    std::string error;
    OMNIS_CHECK(OmnisAvatarStylizer().stylize(bytes.data(), bytes.size(), art, &error));
    OMNIS_CHECK(art.width == 256 && art.height == 256 && art.pixels.size() == 256u * 256u * 4u);
    // Macaron : le cadrage de 9 % n'en retire qu'une partie ; le reste est repéré et comblé.
    int white = 0;
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            const auto* p = px(art, x, y);
            if (p[0] > 235 && p[1] > 235 && p[2] > 235) ++white;
        }
    std::printf("      pixels blancs (macaron) : %d\n", white);
    OMNIS_CHECK(white == 0);
    // Couleurs d'origine (90 %) : le sujet reste le rouge brique, le fond la nuit.
    const auto* c = px(art, 128, 115);
    const auto* bg = px(art, 128, 15);
    std::printf("      centre %d %d %d (origine 190 70 50), fond %d %d %d (origine 30 40 70)\n", c[0], c[1], c[2], bg[0], bg[1], bg[2]);
    OMNIS_CHECK(std::abs(c[0] - 190) < 30 && std::abs(c[1] - 70) < 30 && std::abs(c[2] - 50) < 30);
    OMNIS_CHECK(std::abs(bg[0] - 30) < 25 && std::abs(bg[2] - 70) < 25);
    // Fondu : seulement les derniers pixels du bord.
    std::printf("      alpha : bord %d, 4 px %d, 12 px %d, centre %d\n", px(art, 0, 128)[3], px(art, 4, 128)[3], px(art, 12, 128)[3], px(art, 128, 128)[3]);
    OMNIS_CHECK(px(art, 0, 128)[3] < 40 && px(art, 12, 128)[3] == 255 && px(art, 128, 128)[3] == 255);
}

OMNIS_TEST("avatar peint (option): macaron retiré, teinte gardée, détourage radial") {
    OmnisAvatarStylizer::Options o;
    o.style = OmnisAvatarStylizer::Style::Painted;
    o.crop = 0.18f;
    const auto bytes = cover();
    RgbaImage art;
    OMNIS_CHECK(OmnisAvatarStylizer(o).stylize(bytes.data(), bytes.size(), art));
    const auto* c = px(art, 128, 118);
    OMNIS_CHECK(c[0] > c[1] + 40 && c[0] > c[2] + 40);
    OMNIS_CHECK(px(art, 128, 128)[3] == 255 && px(art, 2, 2)[3] == 0 && px(art, 253, 253)[3] == 0);
}

OMNIS_TEST("avatar: un bloc de texte noir et blanc est repéré dans un coin, jamais au centre") {
    OmnisAvatarStylizer::Rgb im;
    im.width = im.height = 400;
    im.c.assign(3 * 400 * 400, 0.1f);
    const auto stripes = [&](int x0, int y0, int x1, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const float v = ((y - y0) / 5 + (x - x0) / 7) % 2 ? 1.0f : 0.0f;
                for (int c = 0; c < 3; ++c) im.c[static_cast<std::size_t>(3 * (y * 400 + x) + c)] = v;
            }
    };
    stripes(12, 340, 90, 388);   // coin bas gauche
    stripes(170, 170, 250, 220); // milieu : un motif de la pochette, à garder
    const auto boxes = OmnisAvatarStylizer::findCornerText(im);
    OMNIS_CHECK(boxes.size() == 1);
    if (!boxes.empty()) {
        std::printf("      bloc : %d,%d -> %d,%d\n", boxes[0].x0, boxes[0].y0, boxes[0].x1, boxes[0].y1);
        OMNIS_CHECK(boxes[0].x0 <= 12 && boxes[0].y0 <= 340 && boxes[0].x1 >= 90 && boxes[0].y1 >= 388);
        OmnisAvatarStylizer::inpaint(im, boxes[0]);
        float worst = 0;
        for (int y = 340; y < 388; ++y)
            for (int x = 12; x < 90; ++x) worst = std::max(worst, std::fabs(im.c[static_cast<std::size_t>(3 * (y * 400 + x))] - 0.1f));
        OMNIS_CHECK(worst < 0.05f); // comblé avec le fond
    }
}

OMNIS_TEST("avatar: les deux couleurs dominantes (monogramme du mode promo)") {
    RgbaImage im;
    im.width = im.height = 64;
    im.pixels.resize(64 * 64 * 4);
    for (int i = 0; i < 64 * 64; ++i) {
        const bool red = i % 64 < 40; // 62 % rouge brique, 38 % bleu nuit
        auto* p = &im.pixels[static_cast<std::size_t>(4 * i)];
        p[0] = red ? 190 : 30, p[1] = red ? 70 : 40, p[2] = red ? 50 : 90, p[3] = 255;
    }
    const auto c = OmnisAvatarStylizer::dominantColors(im);
    std::printf("      %d %d %d / %d %d %d\n", c[0][0], c[0][1], c[0][2], c[1][0], c[1][1], c[1][2]);
    OMNIS_CHECK(c[0][0] > 150 && c[0][2] < 90);
    OMNIS_CHECK(c[1][2] > c[1][0]);
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
    // Couleurs à côté du PNG, relues telles qu'écrites.
    const auto png = std::filesystem::temp_directory_path() / "omnis_test_avatar.png";
    OMNIS_CHECK(writeColors(png, {{{190, 70, 50}, {30, 40, 90}}}));
    const auto back = readColors(png);
    OMNIS_CHECK(back.valid && back.rgb[0][0] == 190 && back.rgb[1][2] == 90);
    std::error_code ec;
    std::filesystem::remove(colorsPath(png), ec);
}

OMNIS_TEST("avatar: la pochette intégrée à un FLAC (bloc PICTURE) est lue") {
    // Un FLAC minimal : « fLaC », STREAMINFO, puis un bloc PICTURE (dernier bloc).
    std::vector<unsigned char> f{'f', 'L', 'a', 'C'};
    const auto u32 = [&](std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) f.push_back(static_cast<unsigned char>(v >> s)); };
    f.push_back(0x00), f.push_back(0), f.push_back(0), f.push_back(34); // STREAMINFO, 34 octets
    const unsigned char info[34] = {0x10, 0x00, 0x10, 0x00, 0, 0, 0, 0, 0, 0, 0x0A, 0xC4, 0x42, 0xF0, 0, 0, 0, 0};
    f.insert(f.end(), info, info + 34);
    const std::string mime = "image/png", jpg = "OCTETS-IMAGE";
    const std::uint32_t len = 4 + 4 + static_cast<std::uint32_t>(mime.size()) + 4 + 4 * 4 + 4 + static_cast<std::uint32_t>(jpg.size());
    f.push_back(0x80 | 6), f.push_back(static_cast<unsigned char>(len >> 16)), f.push_back(static_cast<unsigned char>(len >> 8)), f.push_back(static_cast<unsigned char>(len));
    u32(3); // pochette avant
    u32(static_cast<std::uint32_t>(mime.size()));
    f.insert(f.end(), mime.begin(), mime.end());
    u32(0); // description vide
    u32(1), u32(1), u32(24), u32(0); // largeur, hauteur, profondeur, couleurs indexées
    u32(static_cast<std::uint32_t>(jpg.size()));
    f.insert(f.end(), jpg.begin(), jpg.end());
    const auto pictures = omnis::tools::readFlacPictures(f.data(), f.size());
    std::printf("      %zu image(s)\n", pictures.size());
    OMNIS_CHECK(pictures.size() == 1);
    if (!pictures.empty()) OMNIS_CHECK(pictures[0].type == 3 && pictures[0].bytes == jpg);
}
