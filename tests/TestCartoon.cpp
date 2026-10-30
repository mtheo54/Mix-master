// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestCartoon.cpp — Une image redessinée en cartoon (omnis_refdb cartoon) :
//  aplats sans grain, trait noir sur les bords, petits détails gardés.
// =============================================================================
#include "TestFramework.h"

#include "Cartoon.h"

#include <cmath>
#include <random>

using namespace omnis::tools;

namespace {

/// Une « photo » : moitié gauche sombre, droite claire, un petit point blanc
/// (un œil) dans la moitié sombre, du grain partout.
RgbImage photo() {
    RgbImage im;
    im.width = im.height = 200;
    im.rgb.resize(3 * 200 * 200);
    std::mt19937 rng(7);
    std::normal_distribution<float> grain(0.0f, 0.04f);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 200; ++x) {
            float v = x < 100 ? 0.15f : 0.85f;
            if ((x - 50) * (x - 50) + (y - 100) * (y - 100) < 36) v = 0.95f;
            for (int c = 0; c < 3; ++c) im.rgb[static_cast<std::size_t>(3 * (y * 200 + x) + c)] = std::clamp(v + grain(rng), 0.0f, 1.0f);
        }
    return im;
}

float at(const RgbImage& im, int x, int y) {
    const float* p = &im.rgb[static_cast<std::size_t>(3 * (y * im.width + x))];
    return (p[0] + p[1] + p[2]) / 3.0f;
}

} // namespace

OMNIS_TEST("cartoon: aplats sans grain, trait sur le bord, petit détail gardé") {
    const auto in = photo();
    const auto out = cartoonize(in);
    OMNIS_CHECK(out.width == 200 && out.height == 200);
    // Aplats : l'écart d'un pixel à l'autre loin des bords a presque disparu.
    double before = 0, after = 0;
    int n = 0;
    for (int y = 20; y < 180; ++y)
        for (int x = 150; x < 190; ++x, ++n) {
            before += std::fabs(at(in, x, y) - at(in, x + 1, y));
            after += std::fabs(at(out, x, y) - at(out, x + 1, y));
        }
    std::printf("      grain moyen : %.4f -> %.4f\n", before / n, after / n);
    OMNIS_CHECK(after < 0.2 * before);
    // Trait : le côté sombre du bord est encré (plus noir que l'aplat sombre).
    float darkest = 1.0f;
    for (int x = 90; x < 100; ++x) darkest = std::min(darkest, at(out, x, 150));
    std::printf("      aplat sombre %.3f, trait %.3f\n", at(out, 40, 150), darkest);
    OMNIS_CHECK(darkest < 0.6f * at(out, 40, 150));
    // Le petit point blanc (un œil) reste clair.
    OMNIS_CHECK(at(out, 50, 100) > 0.6f);
    // Aplat clair : clair.
    OMNIS_CHECK(at(out, 170, 150) > 0.6f);
}
