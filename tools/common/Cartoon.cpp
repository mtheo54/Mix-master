// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/Cartoon.cpp
// =============================================================================
#include "Cartoon.h"

#include "stb_image.h"
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace omnis::tools {

namespace {

float luminance(const float* p) { return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]; }

/// Réduction par moyenne de boîte (la photo peut faire 3000 px).
RgbImage shrink(const RgbImage& in, int maxSide) {
    const int side = std::max(in.width, in.height);
    if (side <= maxSide) return in;
    const double k = static_cast<double>(side) / maxSide;
    RgbImage out;
    out.width = std::max(1, static_cast<int>(std::lround(in.width / k)));
    out.height = std::max(1, static_cast<int>(std::lround(in.height / k)));
    out.rgb.assign(static_cast<std::size_t>(3 * out.width * out.height), 0.0f);
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const int x0 = static_cast<int>(x * k), x1 = std::min(in.width, static_cast<int>((x + 1) * k));
            const int y0 = static_cast<int>(y * k), y1 = std::min(in.height, static_cast<int>((y + 1) * k));
            std::array<double, 3> sum{};
            int n = 0;
            for (int yy = y0; yy < std::max(y1, y0 + 1); ++yy)
                for (int xx = x0; xx < std::max(x1, x0 + 1); ++xx, ++n)
                    for (int c = 0; c < 3; ++c) sum[static_cast<std::size_t>(c)] += in.rgb[static_cast<std::size_t>(3 * (yy * in.width + xx) + c)];
            for (int c = 0; c < 3; ++c) out.rgb[static_cast<std::size_t>(3 * (y * out.width + x) + c)] = static_cast<float>(sum[static_cast<std::size_t>(c)] / n);
        }
    return out;
}

/// Filtre bilatéral : moyenne des voisins proches ET de couleur proche.
RgbImage bilateral(const RgbImage& in, int radius, float sigmaSpace, float sigmaColor) {
    RgbImage out = in;
    std::vector<float> spatial(static_cast<std::size_t>((2 * radius + 1) * (2 * radius + 1)));
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            spatial[static_cast<std::size_t>((dy + radius) * (2 * radius + 1) + dx + radius)] =
                std::exp(-static_cast<float>(dx * dx + dy * dy) / (2.0f * sigmaSpace * sigmaSpace));
    const float colorK = -1.0f / (2.0f * sigmaColor * sigmaColor);
    for (int y = 0; y < in.height; ++y)
        for (int x = 0; x < in.width; ++x) {
            const float* c = &in.rgb[static_cast<std::size_t>(3 * (y * in.width + x))];
            float sum[3] = {0, 0, 0}, wsum = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int yy = std::clamp(y + dy, 0, in.height - 1);
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int xx = std::clamp(x + dx, 0, in.width - 1);
                    const float* p = &in.rgb[static_cast<std::size_t>(3 * (yy * in.width + xx))];
                    const float d2 = (p[0] - c[0]) * (p[0] - c[0]) + (p[1] - c[1]) * (p[1] - c[1]) + (p[2] - c[2]) * (p[2] - c[2]);
                    const float w = spatial[static_cast<std::size_t>((dy + radius) * (2 * radius + 1) + dx + radius)] * std::exp(d2 * colorK);
                    for (int k = 0; k < 3; ++k) sum[k] += w * p[k];
                    wsum += w;
                }
            }
            float* o = &out.rgb[static_cast<std::size_t>(3 * (y * in.width + x))];
            for (int k = 0; k < 3; ++k) o[k] = sum[k] / wsum;
        }
    return out;
}

/// Flou gaussien séparable d'un plan.
std::vector<float> gaussian(const std::vector<float>& in, int w, int h, float sigma) {
    const int r = std::max(1, static_cast<int>(std::ceil(3.0f * sigma)));
    std::vector<float> kernel(static_cast<std::size_t>(2 * r + 1));
    float sum = 0;
    for (int i = -r; i <= r; ++i) sum += kernel[static_cast<std::size_t>(i + r)] = std::exp(-static_cast<float>(i * i) / (2 * sigma * sigma));
    for (auto& k : kernel) k /= sum;
    std::vector<float> tmp(in.size()), out(in.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float s = 0;
            for (int i = -r; i <= r; ++i) s += kernel[static_cast<std::size_t>(i + r)] * in[static_cast<std::size_t>(y * w + std::clamp(x + i, 0, w - 1))];
            tmp[static_cast<std::size_t>(y * w + x)] = s;
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float s = 0;
            for (int i = -r; i <= r; ++i) s += kernel[static_cast<std::size_t>(i + r)] * tmp[static_cast<std::size_t>(std::clamp(y + i, 0, h - 1) * w + x)];
            out[static_cast<std::size_t>(y * w + x)] = s;
        }
    return out;
}

} // namespace

bool loadImage(const std::string& path, RgbImage& out, std::string& error) {
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 3);
    if (!data) {
        error = "image illisible (" + std::string(stbi_failure_reason() ? stbi_failure_reason() : "?") + ") : " + path;
        return false;
    }
    out.width = w;
    out.height = h;
    out.rgb.resize(static_cast<std::size_t>(3 * w * h));
    for (std::size_t i = 0; i < out.rgb.size(); ++i) out.rgb[i] = data[i] / 255.0f;
    stbi_image_free(data);
    return true;
}

bool savePng(const std::string& path, const RgbImage& image, std::string& error) {
    std::vector<unsigned char> bytes(image.rgb.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<unsigned char>(std::lround(std::clamp(image.rgb[i], 0.0f, 1.0f) * 255.0f));
    if (!stbi_write_png(path.c_str(), image.width, image.height, 3, bytes.data(), image.width * 3)) {
        error = "écriture impossible : " + path;
        return false;
    }
    return true;
}

RgbImage cartoonize(const RgbImage& source, const CartoonOptions& o) {
    RgbImage img = shrink(source, o.maxSide);
    if (img.empty()) return img;
    const int w = img.width, h = img.height;
    const auto count = static_cast<std::size_t>(w * h);

    // 1. Aplats : lissage qui garde les bords.
    RgbImage smooth = img;
    // Une passe large d'abord (le grain d'une photo), puis des passes fines.
    smooth = bilateral(smooth, 5, 4.0f, 0.18f);
    for (int pass = 1; pass < o.smoothPasses; ++pass) smooth = bilateral(smooth, 4, 3.0f, 0.12f);

    // 2. Ombrage cartoon : chaque zone garde sa couleur, la luminosité tombe sur
    //    quelques paliers (les petits détails, yeux, reflets, gardent la leur).
    //    Couleurs plus franches (saturation autour de la luminance).
    const int levels = std::clamp(o.colors / 2, 3, 12);
    std::vector<float> flat(3 * count);
    for (std::size_t i = 0; i < count; ++i) {
        const float* c = &smooth.rgb[3 * i];
        const float l = std::max(1e-4f, luminance(c));
        // Palier, avec une transition courte (pas d'escalier crénelé).
        const float pos = l * static_cast<float>(levels);
        const float base = std::floor(pos), frac = pos - base;
        const float t = std::clamp((frac - 0.4f) / 0.2f, 0.0f, 1.0f);
        const float stepL = (base + t * t * (3.0f - 2.0f * t)) / static_cast<float>(levels); // noir reste noir
        const float target = std::clamp(stepL, 0.03f, 1.0f);
        for (int ch = 0; ch < 3; ++ch) {
            const float v = l + (c[ch] - l) * o.vivid;           // saturation
            flat[3 * i + static_cast<std::size_t>(ch)] = std::clamp(v * target / l, 0.0f, 1.0f); // nouvelle luminance
        }
    }

    // 3. Trait : différence de gaussiennes sur la luminance (lissée), seuil doux.
    std::vector<float> lum(count);
    for (std::size_t i = 0; i < count; ++i) lum[i] = luminance(&smooth.rgb[3 * i]);
    const float scale = std::max(1.0f, static_cast<float>(std::max(w, h)) / 400.0f); // trait proportionnel à la taille
    const auto g1 = gaussian(lum, w, h, 0.9f * scale), g2 = gaussian(lum, w, h, 1.6f * 0.9f * scale);
    std::vector<float> line(count);
    const float threshold = 0.008f * (1.5f - o.ink);
    for (std::size_t i = 0; i < count; ++i) {
        const float d = g1[i] - g2[i]; // négatif du côté sombre d'un bord
        line[i] = d < -threshold ? std::clamp((-d - threshold) * 90.0f, 0.0f, 1.0f) : 0.0f;
    }
    // Trait un peu épaissi (le plus fort des voisins), proportionnel à la taille.
    if (scale >= 1.0f) {
        const int r = static_cast<int>(std::lround(scale * 0.8f));
        std::vector<float> thick(line);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float m = 0;
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx)
                        if (dx * dx + dy * dy <= r * r) m = std::max(m, line[static_cast<std::size_t>(std::clamp(y + dy, 0, h - 1) * w + std::clamp(x + dx, 0, w - 1))]);
                thick[static_cast<std::size_t>(y * w + x)] = m;
            }
        line.swap(thick);
    }

    // 4. Aplats + trait.
    RgbImage out;
    out.width = w;
    out.height = h;
    out.rgb.resize(3 * count);
    const std::array<float, 3> inkColor{0.06f, 0.05f, 0.07f};
    for (std::size_t i = 0; i < count; ++i)
        for (int ch = 0; ch < 3; ++ch)
            out.rgb[3 * i + static_cast<std::size_t>(ch)] =
                flat[3 * i + static_cast<std::size_t>(ch)] * (1 - line[i]) + inkColor[static_cast<std::size_t>(ch)] * line[i];
    return out;
}

} // namespace omnis::tools
