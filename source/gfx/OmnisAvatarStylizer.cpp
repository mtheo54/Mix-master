// =============================================================================
//  Abrasion Labs // OMNIS
//  gfx/OmnisAvatarStylizer.cpp
// =============================================================================
#include "OmnisAvatarStylizer.h"

#include "stb_image.h"
#include "stb_image_write.h"

#if defined(OMNIS_HAS_NANOVG)
#include "nanovg.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace omnis::gfx {

namespace {

constexpr float kKr = 0.299f, kKg = 0.587f, kKb = 0.114f;

float luma(const float* p) { return kKr * p[0] + kKg * p[1] + kKb * p[2]; }

float smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/// YCbCr (BT.601, Cb / Cr centrés sur 0) : la luminance bouge, la teinte reste.
void toYcc(const float* rgb, float& y, float& cb, float& cr) {
    y = luma(rgb);
    cb = (rgb[2] - y) * 0.564f;
    cr = (rgb[0] - y) * 0.713f;
}
void fromYcc(float y, float cb, float cr, float* rgb) {
    rgb[0] = std::clamp(y + 1.403f * cr, 0.0f, 1.0f);
    rgb[1] = std::clamp(y - 0.344f * cb - 0.714f * cr, 0.0f, 1.0f);
    rgb[2] = std::clamp(y + 1.773f * cb, 0.0f, 1.0f);
}

/// Réduction par moyenne de surface (plan à `channels` canaux).
std::vector<float> resample(const std::vector<float>& in, int w, int h, int channels, int ow, int oh) {
    std::vector<float> out(static_cast<std::size_t>(ow * oh * channels), 0.0f);
    const double sx = static_cast<double>(w) / ow, sy = static_cast<double>(h) / oh;
    for (int y = 0; y < oh; ++y) {
        const double y0 = y * sy, y1 = (y + 1) * sy;
        for (int x = 0; x < ow; ++x) {
            const double x0 = x * sx, x1 = (x + 1) * sx;
            std::array<double, 4> acc{};
            double area = 0;
            for (int yy = static_cast<int>(y0); yy < std::min(h, static_cast<int>(std::ceil(y1))); ++yy) {
                const double wy = std::min(y1, yy + 1.0) - std::max(y0, static_cast<double>(yy));
                for (int xx = static_cast<int>(x0); xx < std::min(w, static_cast<int>(std::ceil(x1))); ++xx) {
                    const double wgt = wy * (std::min(x1, xx + 1.0) - std::max(x0, static_cast<double>(xx)));
                    if (wgt <= 0) continue;
                    for (int c = 0; c < channels; ++c) acc[static_cast<std::size_t>(c)] += wgt * in[static_cast<std::size_t>((yy * w + xx) * channels + c)];
                    area += wgt;
                }
            }
            for (int c = 0; c < channels; ++c)
                out[static_cast<std::size_t>((y * ow + x) * channels + c)] = area > 0 ? static_cast<float>(acc[static_cast<std::size_t>(c)] / area) : 0.0f;
        }
    }
    return out;
}

/// Tables de sommes (somme sur [0, x) × [0, y)) : la moyenne d'un rectangle en O(1).
struct Integral {
    int w = 0, h = 0;
    std::vector<double> t; ///< (w + 1) × (h + 1)
    Integral(const std::vector<float>& plane, int width, int height, bool squared) : w(width), h(height), t(static_cast<std::size_t>((width + 1) * (height + 1)), 0.0) {
        for (int y = 0; y < h; ++y) {
            double row = 0;
            for (int x = 0; x < w; ++x) {
                const double v = plane[static_cast<std::size_t>(y * w + x)];
                row += squared ? v * v : v;
                t[static_cast<std::size_t>((y + 1) * (w + 1) + x + 1)] = t[static_cast<std::size_t>(y * (w + 1) + x + 1)] + row;
            }
        }
    }
    /// Somme sur [x0, x1] × [y0, y1] (inclus).
    [[nodiscard]] double sum(int x0, int y0, int x1, int y1) const {
        return t[static_cast<std::size_t>((y1 + 1) * (w + 1) + x1 + 1)] - t[static_cast<std::size_t>(y0 * (w + 1) + x1 + 1)] -
               t[static_cast<std::size_t>((y1 + 1) * (w + 1) + x0)] + t[static_cast<std::size_t>(y0 * (w + 1) + x0)];
    }
};

} // namespace

OmnisAvatarStylizer::Rgb OmnisAvatarStylizer::cropCenter(const std::uint8_t* rgba, int width, int height, float crop, float focusUp, int side) {
    // Cadre : bords retirés, remonté vers le visage ; puis le plus grand carré centré.
    crop = std::clamp(crop, 0.0f, 0.4f);
    const float left = crop * static_cast<float>(width), right = (1.0f - crop) * static_cast<float>(width);
    const float top = std::max(0.0f, (crop - focusUp) * static_cast<float>(height));
    const float bottom = std::min(static_cast<float>(height), (1.0f - crop - focusUp) * static_cast<float>(height));
    const float sq = std::min(right - left, bottom - top);
    const int x0 = static_cast<int>(std::lround((left + right - sq) / 2)), y0 = static_cast<int>(std::lround((top + bottom - sq) / 2));
    const int n = std::max(1, static_cast<int>(std::lround(sq)));
    std::vector<float> square(static_cast<std::size_t>(n * n * 3));
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const int sx = std::clamp(x0 + x, 0, width - 1), sy = std::clamp(y0 + y, 0, height - 1);
            const std::uint8_t* p = &rgba[static_cast<std::size_t>((sy * width + sx) * 4)];
            for (int c = 0; c < 3; ++c) square[static_cast<std::size_t>((y * n + x) * 3 + c)] = p[c] / 255.0f;
        }
    Rgb out;
    out.width = out.height = side;
    out.c = n == side ? std::move(square) : resample(square, n, n, 3, side, side);
    return out;
}

void OmnisAvatarStylizer::kuwahara(Rgb& img, int r) {
    // Kuwahara lissé : chaque pixel prend la moyenne de ses quatre quadrants
    // (r × r), pondérée par leur calme (1 / variance²) : les zones unies gagnent,
    // le bord reste net, sans les blocs du Kuwahara strict.
    const int w = img.width, h = img.height;
    const auto count = static_cast<std::size_t>(w * h);
    std::vector<float> plane(count);
    std::array<std::unique_ptr<Integral>, 3> col;
    for (int c = 0; c < 3; ++c) {
        for (std::size_t i = 0; i < count; ++i) plane[i] = img.c[3 * i + static_cast<std::size_t>(c)];
        col[static_cast<std::size_t>(c)] = std::make_unique<Integral>(plane, w, h, false);
    }
    for (std::size_t i = 0; i < count; ++i) plane[i] = luma(&img.c[3 * i]);
    const Integral ly(plane, w, h, false), ly2(plane, w, h, true);
    std::vector<float> out(img.c.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::array<std::array<int, 4>, 4> quads{{{x - r, y - r, x, y}, {x, y - r, x + r, y}, {x - r, y, x, y + r}, {x, y, x + r, y + r}}};
            double acc[3] = {0, 0, 0}, wsum = 0;
            for (const auto& q : quads) {
                const int qx0 = std::max(0, q[0]), qy0 = std::max(0, q[1]), qx1 = std::min(w - 1, q[2]), qy1 = std::min(h - 1, q[3]);
                const double n = static_cast<double>((qx1 - qx0 + 1) * (qy1 - qy0 + 1));
                const double mean = ly.sum(qx0, qy0, qx1, qy1) / n;
                const double var = std::max(0.0, ly2.sum(qx0, qy0, qx1, qy1) / n - mean * mean);
                const double wgt = 1.0 / ((var + 1e-4) * (var + 1e-4));
                for (int c = 0; c < 3; ++c) acc[c] += wgt * col[static_cast<std::size_t>(c)]->sum(qx0, qy0, qx1, qy1) / n;
                wsum += wgt;
            }
            for (int c = 0; c < 3; ++c) out[static_cast<std::size_t>((y * w + x) * 3 + c)] = static_cast<float>(acc[c] / wsum);
        }
    img.c.swap(out);
}

void OmnisAvatarStylizer::grade(Rgb& img, float gamma, float knee) {
    const auto count = static_cast<std::size_t>(img.width * img.height);
    // Contraste automatique : 1 % et 99 % de la luminance.
    std::array<int, 256> hist{};
    for (std::size_t i = 0; i < count; ++i) ++hist[static_cast<std::size_t>(std::clamp(static_cast<int>(luma(&img.c[3 * i]) * 255.0f), 0, 255))];
    const auto percentile = [&](double p) {
        std::size_t acc = 0;
        for (int b = 0; b < 256; ++b)
            if ((acc += static_cast<std::size_t>(hist[static_cast<std::size_t>(b)])) >= static_cast<std::size_t>(p * static_cast<double>(count))) return b / 255.0f;
        return 1.0f;
    };
    // Étirement doux : à mi-chemin vers 1 % / 99 %, gain plafonné à 1,4 (une image
    // déjà contrastée ou à deux tons ne part pas en blanc et noir).
    float lo = 0.5f * percentile(0.01), hi = 1.0f - 0.5f * (1.0f - percentile(0.99));
    if (hi - lo < 1.0f / 1.4f) {
        const float mid = 0.5f * (lo + hi), half = 0.5f / 1.4f;
        lo = std::max(0.0f, mid - half);
        hi = lo + 2.0f * half;
    }
    for (std::size_t i = 0; i < count; ++i) {
        float y, cb, cr;
        toYcc(&img.c[3 * i], y, cb, cr);
        float v = std::clamp((y - lo) / (hi - lo), 0.0f, 1.0f);
        // Hautes lumières brûlées adoucies (genou vers 0,95), ombres densifiées.
        if (v > knee) v = knee + (0.95f - knee) * (1.0f - std::exp(-(v - knee) / (0.95f - knee)));
        v = std::pow(v, gamma);
        // La teinte reste ; sa force suit un peu la luminance (une ombre n'est pas fluo).
        const float k = y > 1e-3f ? std::clamp(std::sqrt(v / y), 0.6f, 1.3f) : 1.0f;
        fromYcc(v, cb * k, cr * k, &img.c[3 * i]);
    }
}

void OmnisAvatarStylizer::celShade(Rgb& img, int levels) {
    const auto count = static_cast<std::size_t>(img.width * img.height);
    const float n = static_cast<float>(std::clamp(levels, 2, 32));
    for (std::size_t i = 0; i < count; ++i) {
        float y, cb, cr;
        toYcc(&img.c[3 * i], y, cb, cr);
        const float pos = y * n, base = std::floor(pos);
        // Palier, relié au suivant par un smoothstep court ; un peu de l'original
        // garde le volume (cel-shading doux, pas d'escalier brut).
        const float stepped = (base + smoothstep(0.35f, 0.65f, pos - base)) / n;
        fromYcc(0.85f * stepped + 0.15f * y, cb, cr, &img.c[3 * i]);
    }
}

std::vector<float> OmnisAvatarStylizer::inkMask(const Rgb& img) {
    const int w = img.width, h = img.height;
    const auto count = static_cast<std::size_t>(w * h);
    std::vector<float> y(count), mag(count, 0.0f);
    for (std::size_t i = 0; i < count; ++i) y[i] = luma(&img.c[3 * i]);
    const auto at = [&](int xx, int yy) { return y[static_cast<std::size_t>(std::clamp(yy, 0, h - 1) * w + std::clamp(xx, 0, w - 1))]; };
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx) {
            const float gx = (at(xx + 1, yy - 1) + 2 * at(xx + 1, yy) + at(xx + 1, yy + 1)) - (at(xx - 1, yy - 1) + 2 * at(xx - 1, yy) + at(xx - 1, yy + 1));
            const float gy = (at(xx - 1, yy + 1) + 2 * at(xx, yy + 1) + at(xx + 1, yy + 1)) - (at(xx - 1, yy - 1) + 2 * at(xx, yy - 1) + at(xx + 1, yy - 1));
            mag[static_cast<std::size_t>(yy * w + xx)] = std::sqrt(gx * gx + gy * gy);
        }
    // Seuil adapté : seuls les ~10 % de bords les plus francs encrent (une photo
    // douce et une pochette contrastée gardent un trait comparable).
    std::vector<float> sorted(mag);
    const auto p90 = sorted.begin() + static_cast<std::ptrdiff_t>(0.90 * static_cast<double>(count));
    std::nth_element(sorted.begin(), p90, sorted.end());
    const float ref = std::max(*p90, 0.08f); // image presque unie : pas de trait parasite
    std::vector<float> ink(count);
    for (std::size_t i = 0; i < count; ++i) ink[i] = smoothstep(0.9f, 1.8f, mag[i] / ref);
    return ink;
}

RgbaImage OmnisAvatarStylizer::finish(const Rgb& img, const std::vector<float>& ink) const {
    const auto count = static_cast<std::size_t>(img.width * img.height);
    // Trait multiplié : vers un presque-noir à peine bleuté (pas un noir plat).
    std::vector<float> rgb(img.c);
    constexpr std::array<float, 3> kInk{0.05f, 0.05f, 0.08f};
    for (std::size_t i = 0; i < count; ++i) {
        const float k = std::clamp(o_.ink, 0.0f, 1.0f) * ink[i];
        for (std::size_t c = 0; c < 3; ++c) rgb[3 * i + c] = rgb[3 * i + c] * (1 - k) + kInk[c] * k;
    }
    const int s = std::max(16, o_.size);
    const auto small = img.width == s ? rgb : resample(rgb, img.width, img.height, 3, s, s);
    RgbaImage out;
    out.width = out.height = s;
    out.pixels.resize(static_cast<std::size_t>(s * s * 4));
    for (int y = 0; y < s; ++y)
        for (int x = 0; x < s; ++x) {
            const auto i = static_cast<std::size_t>(y * s + x);
            // Détourage : ellipse un peu haute (le buste descend jusqu'en bas).
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(s) * 2 - 1;
            const float v = ((static_cast<float>(y) + 0.5f) / static_cast<float>(s) * 2 - 1) * 0.92f;
            const float alpha = 1.0f - smoothstep(o_.vignetteInner, o_.vignetteOuter, std::sqrt(u * u + v * v));
            for (std::size_t c = 0; c < 3; ++c) out.pixels[4 * i + c] = static_cast<std::uint8_t>(std::lround(std::clamp(small[3 * i + c], 0.0f, 1.0f) * 255.0f));
            out.pixels[4 * i + 3] = static_cast<std::uint8_t>(std::lround(alpha * 255.0f));
        }
    return out;
}

bool OmnisAvatarStylizer::stylize(const std::uint8_t* data, std::size_t size, RgbaImage& out, std::string* error) const {
    out = {};
    int w = 0, h = 0, channels = 0;
    Rgb work;
    {
        // L'image d'origine n'existe qu'ici, en mémoire : libérée dès le cadrage.
        const std::unique_ptr<std::uint8_t, void (*)(void*)> raw(
            stbi_load_from_memory(data, static_cast<int>(std::min<std::size_t>(size, 0x7fffffff)), &w, &h, &channels, 4), stbi_image_free);
        if (!raw || w < 16 || h < 16) {
            if (error) *error = std::string("image illisible : ") + (stbi_failure_reason() ? stbi_failure_reason() : "trop petite");
            return false;
        }
        work = cropCenter(raw.get(), w, h, o_.crop, o_.focusUp, std::max(64, o_.workSize));
    }
    // Première passe large (grain, texture de peau), les suivantes plus fines : les
    // petits détails (yeux, reflets) ne se fondent pas dans leur voisinage.
    for (int pass = 0; pass < std::max(1, o_.kuwaharaPasses); ++pass) kuwahara(work, std::max(2, o_.kuwaharaRadius - 2 * pass));
    const auto ink = inkMask(work); // sur la peinture : le trait suit les formes, pas le grain
    grade(work, o_.shadowGamma, o_.highlightKnee);
    celShade(work, o_.levels);
    out = finish(work, ink);
    return true;
}

bool OmnisAvatarStylizer::savePng(const std::filesystem::path& path, const RgbaImage& image, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto u8 = path.u8string();
    const std::string utf8(u8.begin(), u8.end());
    if (image.empty() || !stbi_write_png(utf8.c_str(), image.width, image.height, 4, image.pixels.data(), image.width * 4)) {
        if (error) *error = "écriture impossible : " + utf8;
        return false;
    }
    return true;
}

#if defined(OMNIS_HAS_NANOVG)
int OmnisAvatarStylizer::toNanoVG(NVGcontext* vg, const RgbaImage& image) {
    if (!vg || image.empty()) return 0;
    return nvgCreateImageRGBA(vg, image.width, image.height, 0, image.pixels.data());
}
#endif

} // namespace omnis::gfx
