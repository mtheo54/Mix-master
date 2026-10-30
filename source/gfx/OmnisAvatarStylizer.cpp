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

/// Flou gaussien séparable d'un plan.
std::vector<float> blur(const std::vector<float>& in, int w, int h, float sigma) {
    const int r = std::max(1, static_cast<int>(std::ceil(3.0f * sigma)));
    std::vector<float> k(static_cast<std::size_t>(2 * r + 1));
    float sum = 0;
    for (int i = -r; i <= r; ++i) sum += k[static_cast<std::size_t>(i + r)] = std::exp(-static_cast<float>(i * i) / (2 * sigma * sigma));
    for (auto& v : k) v /= sum;
    std::vector<float> tmp(in.size()), out(in.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float acc = 0;
            for (int i = -r; i <= r; ++i) acc += k[static_cast<std::size_t>(i + r)] * in[static_cast<std::size_t>(y * w + std::clamp(x + i, 0, w - 1))];
            tmp[static_cast<std::size_t>(y * w + x)] = acc;
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float acc = 0;
            for (int i = -r; i <= r; ++i) acc += k[static_cast<std::size_t>(i + r)] * tmp[static_cast<std::size_t>(std::clamp(y + i, 0, h - 1) * w + x)];
            out[static_cast<std::size_t>(y * w + x)] = acc;
        }
    return out;
}

} // namespace

void OmnisAvatarStylizer::denoise(Rgb& img) {
    // Bilatéral léger (rayon 2) : efface le bruit sans toucher aux bords.
    const int w = img.width, h = img.height, r = 2;
    std::vector<float> out(img.c.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float* c = &img.c[static_cast<std::size_t>(3 * (y * w + x))];
            float acc[3] = {0, 0, 0}, wsum = 0;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    const float* p = &img.c[static_cast<std::size_t>(3 * (std::clamp(y + dy, 0, h - 1) * w + std::clamp(x + dx, 0, w - 1)))];
                    const float d2 = (p[0] - c[0]) * (p[0] - c[0]) + (p[1] - c[1]) * (p[1] - c[1]) + (p[2] - c[2]) * (p[2] - c[2]);
                    const float wgt = std::exp(-static_cast<float>(dx * dx + dy * dy) / 4.5f - d2 / 0.0128f);
                    for (int k = 0; k < 3; ++k) acc[k] += wgt * p[k];
                    wsum += wgt;
                }
            for (int k = 0; k < 3; ++k) out[static_cast<std::size_t>(3 * (y * w + x) + k)] = acc[k] / wsum;
        }
    img.c.swap(out);
}

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
                const double wgt = 1.0 / (var + 4e-4); // mélange doux : pas de taches « camouflage »
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
        const float stepped = (base + smoothstep(0.25f, 0.75f, pos - base)) / n;
        fromYcc(0.65f * stepped + 0.35f * y, cb, cr, &img.c[3 * i]);
    }
}

std::vector<float> OmnisAvatarStylizer::inkMask(const Rgb& img) {
    const int w = img.width, h = img.height;
    const auto count = static_cast<std::size_t>(w * h);
    std::vector<float> y(count), mag(count, 0.0f);
    for (std::size_t i = 0; i < count; ++i) y[i] = luma(&img.c[3 * i]);
    y = blur(y, w, h, 1.2f); // le trait suit les formes, pas le grain qui reste
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
    const auto p94 = sorted.begin() + static_cast<std::ptrdiff_t>(0.94 * static_cast<double>(count));
    std::nth_element(sorted.begin(), p94, sorted.end());
    const float ref = std::max(*p94, 0.10f); // image presque unie : pas de trait parasite
    std::vector<float> ink(count);
    for (std::size_t i = 0; i < count; ++i) ink[i] = smoothstep(1.0f, 2.2f, mag[i] / ref);
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

std::vector<OmnisAvatarStylizer::Box> OmnisAvatarStylizer::findCornerText(const Rgb& img) {
    // Cellules de 8 px « texte noir et blanc » : surtout du noir ou du blanc sans
    // couleur, et des passages francs de l'un à l'autre (les lettres). Les amas de
    // cellules collés à un coin, de la taille d'un macaron, sont retenus.
    const int w = img.width, h = img.height, cell = 8;
    const int gw = w / cell, gh = h / cell;
    std::vector<std::uint8_t> text(static_cast<std::size_t>(gw * gh), 0);
    const auto lum = [&](int x, int y) { return luma(&img.c[static_cast<std::size_t>(3 * (y * w + x))]); };
    for (int cy = 0; cy < gh; ++cy)
        for (int cx = 0; cx < gw; ++cx) {
            const float fx = (cx + 0.5f) / static_cast<float>(gw), fy = (cy + 0.5f) / static_cast<float>(gh);
            const bool corner = (fx < 0.32f || fx > 0.68f) && (fy < 0.22f || fy > 0.70f);
            if (!corner) continue;
            int extreme = 0, edges = 0;
            for (int y = cy * cell; y < (cy + 1) * cell; ++y)
                for (int x = cx * cell; x < (cx + 1) * cell; ++x) {
                    const float* p = &img.c[static_cast<std::size_t>(3 * (y * w + x))];
                    const float chroma = std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]}), l = luma(p);
                    if (chroma < 0.14f && (l < 0.2f || l > 0.8f)) ++extreme;
                    if (x + 1 < w && std::fabs(lum(x + 1, y) - l) > 0.35f) ++edges;
                    if (y + 1 < h && std::fabs(lum(x, y + 1) - l) > 0.35f) ++edges;
                }
            const float n = static_cast<float>(cell * cell);
            if (extreme >= 0.6f * n && edges >= 0.08f * n) text[static_cast<std::size_t>(cy * gw + cx)] = 1;
        }
    // Amas (cellules voisines, un trou d'une cellule toléré entre lettres).
    std::vector<int> label(text.size(), -1);
    std::vector<Box> found;
    for (int start = 0; start < gw * gh; ++start) {
        if (!text[static_cast<std::size_t>(start)] || label[static_cast<std::size_t>(start)] >= 0) continue;
        std::vector<int> stack{start};
        label[static_cast<std::size_t>(start)] = start;
        int x0 = gw, y0 = gh, x1 = -1, y1 = -1, cells = 0;
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            const int cx = c % gw, cy = c / gw;
            x0 = std::min(x0, cx), y0 = std::min(y0, cy), x1 = std::max(x1, cx), y1 = std::max(y1, cy), ++cells;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) {
                    const int nx = cx + dx, ny = cy + dy;
                    if (nx < 0 || ny < 0 || nx >= gw || ny >= gh) continue;
                    const auto k = static_cast<std::size_t>(ny * gw + nx);
                    if (text[k] && label[k] < 0) label[k] = start, stack.push_back(ny * gw + nx);
                }
        }
        const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
        const double area = static_cast<double>(bw * bh) / (gw * gh);
        const bool nearSide = x0 <= gw * 0.12 || x1 >= gw * 0.88 - 1;
        const bool nearEdge = y0 <= gh * 0.12 || y1 >= gh * 0.88 - 1;
        if (cells >= 4 && bw >= 3 && bh >= 2 && area >= 0.002 && area <= 0.08 && nearSide && nearEdge)
            found.push_back({std::max(0, (x0 - 1) * cell), std::max(0, (y0 - 1) * cell), std::min(w, (x1 + 2) * cell), std::min(h, (y1 + 2) * cell)});
    }
    return found;
}

void OmnisAvatarStylizer::inpaint(Rgb& img, const Box& b) {
    // Chaque pixel du rectangle : moyenne des bords voisins disponibles, pondérée
    // par la distance (un bord collé à l'image n'existe pas), puis lissage.
    const int w = img.width, h = img.height;
    const int x0 = std::clamp(b.x0, 0, w), x1 = std::clamp(b.x1, 0, w), y0 = std::clamp(b.y0, 0, h), y1 = std::clamp(b.y1, 0, h);
    if (x1 - x0 < 2 || y1 - y0 < 2) return;
    const auto at = [&](int x, int y) { return &img.c[static_cast<std::size_t>(3 * (y * w + x))]; };
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float acc[3] = {0, 0, 0}, wsum = 0;
            const auto add = [&](int sx, int sy, float dist) {
                const float* p = at(sx, sy);
                const float k = 1.0f / std::max(1.0f, dist);
                for (int c = 0; c < 3; ++c) acc[c] += k * p[c];
                wsum += k;
            };
            if (x0 > 0) add(x0 - 1, y, static_cast<float>(x - x0 + 1));
            if (x1 < w) add(x1, y, static_cast<float>(x1 - x));
            if (y0 > 0) add(x, y0 - 1, static_cast<float>(y - y0 + 1));
            if (y1 < h) add(x, y1, static_cast<float>(y1 - y));
            if (wsum > 0)
                for (int c = 0; c < 3; ++c) at(x, y)[c] = acc[c] / wsum;
        }
    for (int it = 0; it < 40; ++it)
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const float* l = at(std::max(0, x - 1), y);
                const float* r = at(std::min(w - 1, x + 1), y);
                const float* u = at(x, std::max(0, y - 1));
                const float* d = at(x, std::min(h - 1, y + 1));
                for (int c = 0; c < 3; ++c) at(x, y)[c] = 0.25f * (l[c] + r[c] + u[c] + d[c]);
            }
}

RgbaImage OmnisAvatarStylizer::finishHd(const Rgb& img) const {
    // 90 % d'origine, 10 % d'étalonnage (thème sombre de la console).
    Rgb graded = img;
    grade(graded, o_.shadowGamma, o_.highlightKnee);
    std::vector<float> mixed(img.c.size());
    const float keep = std::clamp(o_.keep, 0.0f, 1.0f);
    for (std::size_t i = 0; i < mixed.size(); ++i) mixed[i] = keep * img.c[i] + (1 - keep) * graded.c[i];
    const int s = std::max(16, o_.size);
    const auto small = img.width == s ? mixed : resample(mixed, img.width, img.height, 3, s, s);
    RgbaImage out;
    out.width = out.height = s;
    out.pixels.resize(static_cast<std::size_t>(s * s * 4));
    const float feather = static_cast<float>(std::max(1, o_.featherPx));
    for (int y = 0; y < s; ++y)
        for (int x = 0; x < s; ++x) {
            const auto i = static_cast<std::size_t>(y * s + x);
            // Fondu seulement sur les derniers pixels du bord.
            const float d = static_cast<float>(std::min({x, y, s - 1 - x, s - 1 - y})) + 0.5f;
            for (std::size_t c = 0; c < 3; ++c) out.pixels[4 * i + c] = static_cast<std::uint8_t>(std::lround(std::clamp(small[3 * i + c], 0.0f, 1.0f) * 255.0f));
            out.pixels[4 * i + 3] = static_cast<std::uint8_t>(std::lround(smoothstep(0.0f, feather, d) * 255.0f));
        }
    return out;
}

std::array<std::array<std::uint8_t, 3>, 2> OmnisAvatarStylizer::dominantColors(const RgbaImage& img) {
    // k-moyennes (5 groupes) sur les pixels visibles ; on garde le groupe le plus
    // peuplé (les couleurs vives comptent un peu plus), puis le plus fort qui en
    // diffère vraiment.
    std::vector<std::array<float, 3>> px;
    const std::size_t n = img.pixels.size() / 4, step = std::max<std::size_t>(1, n / 6000);
    for (std::size_t i = 0; i < n; i += step)
        if (img.pixels[4 * i + 3] > 128) px.push_back({img.pixels[4 * i] / 255.0f, img.pixels[4 * i + 1] / 255.0f, img.pixels[4 * i + 2] / 255.0f});
    std::array<std::array<std::uint8_t, 3>, 2> out{{{40, 44, 52}, {255, 170, 0}}};
    if (px.size() < 8) return out;
    std::sort(px.begin(), px.end(), [](const auto& a, const auto& b) { return luma(a.data()) < luma(b.data()); });
    constexpr int k = 5;
    std::array<std::array<float, 3>, k> centers{};
    for (int c = 0; c < k; ++c) centers[static_cast<std::size_t>(c)] = px[(px.size() - 1) * static_cast<std::size_t>(2 * c + 1) / (2 * k)];
    std::array<int, k> count{};
    const auto d2 = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
    };
    for (int it = 0; it < 10; ++it) {
        std::array<std::array<double, 3>, k> acc{};
        count.fill(0);
        for (const auto& p : px) {
            int best = 0;
            for (int c = 1; c < k; ++c)
                if (d2(p, centers[static_cast<std::size_t>(c)]) < d2(p, centers[static_cast<std::size_t>(best)])) best = c;
            for (int ch = 0; ch < 3; ++ch) acc[static_cast<std::size_t>(best)][static_cast<std::size_t>(ch)] += p[static_cast<std::size_t>(ch)];
            ++count[static_cast<std::size_t>(best)];
        }
        for (int c = 0; c < k; ++c)
            if (count[static_cast<std::size_t>(c)] > 0)
                for (int ch = 0; ch < 3; ++ch)
                    centers[static_cast<std::size_t>(c)][static_cast<std::size_t>(ch)] =
                        static_cast<float>(acc[static_cast<std::size_t>(c)][static_cast<std::size_t>(ch)] / count[static_cast<std::size_t>(c)]);
    }
    const auto score = [&](int c) {
        const auto& v = centers[static_cast<std::size_t>(c)];
        const float sat = std::max({v[0], v[1], v[2]}) - std::min({v[0], v[1], v[2]});
        return static_cast<float>(count[static_cast<std::size_t>(c)]) * (0.35f + sat);
    };
    int first = 0;
    for (int c = 1; c < k; ++c)
        if (score(c) > score(first)) first = c;
    int second = -1;
    for (int c = 0; c < k; ++c)
        if (c != first && d2(centers[static_cast<std::size_t>(c)], centers[static_cast<std::size_t>(first)]) > 0.04f && (second < 0 || score(c) > score(second))) second = c;
    const auto toByte = [](const std::array<float, 3>& v) {
        return std::array<std::uint8_t, 3>{static_cast<std::uint8_t>(std::lround(std::clamp(v[0], 0.0f, 1.0f) * 255)),
                                           static_cast<std::uint8_t>(std::lround(std::clamp(v[1], 0.0f, 1.0f) * 255)),
                                           static_cast<std::uint8_t>(std::lround(std::clamp(v[2], 0.0f, 1.0f) * 255))};
    };
    out[0] = toByte(centers[static_cast<std::size_t>(first)]);
    if (second >= 0) out[1] = toByte(centers[static_cast<std::size_t>(second)]);
    else { // une seule couleur : sa version claire (ou sombre)
        auto v = centers[static_cast<std::size_t>(first)];
        const bool dark = luma(v.data()) < 0.5f;
        for (auto& x : v) x = dark ? 0.35f + 0.65f * x : 0.4f * x;
        out[1] = toByte(v);
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
    if (o_.removeCornerText)
        for (const auto& box : findCornerText(work)) inpaint(work, box);
    denoise(work); // bruit de compression d'abord (et, en « Peint », avant le Kuwahara)
    if (o_.style == Style::HD) {
        out = finishHd(work);
        return true;
    }
    // Première passe large (texture de peau, tissu), les suivantes deux fois plus
    // fines : les petits détails (yeux, reflets) ne se fondent pas dans leur voisinage.
    for (int pass = 0, r = o_.kuwaharaRadius; pass < std::max(1, o_.kuwaharaPasses); ++pass, r = std::max(2, r / 2)) kuwahara(work, r);
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
