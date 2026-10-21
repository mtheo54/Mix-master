// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/DistortionAnalysis.cpp
// =============================================================================
#include "DistortionAnalysis.h"

#include "analysis/KWeightingFilter.h"
#include "core/Decibels.h"
#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <sstream>

namespace omnis::tools {

namespace {

constexpr int kFftSize = 4096;
constexpr int kHop = 1024;

/// |H_K(f)|² de la pondération K (pour pondérer les bandes).
double kWeightPower(double f, double fs) {
    const auto s = analysis::kWeightingShelf(fs);
    const auto h = analysis::kWeightingHighPass(fs);
    const std::complex<double> z1 = std::polar(1.0, -2.0 * std::numbers::pi * f / fs);
    const std::complex<double> z2 = z1 * z1;
    const auto eval = [&](const analysis::BiquadCoefficients& c) {
        return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
    };
    return std::norm(eval(s) * eval(h));
}

} // namespace

DistortionReport measureDistortion(const AudioFile& reference, const AudioFile& processed) {
    DistortionReport report;
    const double fs = reference.sampleRate;
    const int frames = std::min(reference.numFrames(), processed.numFrames());
    const int channels = std::min({reference.numChannels(), processed.numChannels(), 2});

    // Bandes de tiers d'octave 31.5 Hz … 16 kHz.
    std::vector<double> centers;
    for (int i = 0;; ++i) {
        const double c = 31.5 * std::pow(2.0, i / 3.0);
        if (c > std::min(16000.0, 0.45 * fs)) break;
        centers.push_back(c);
    }
    const int numBands = static_cast<int>(centers.size());
    std::vector<int> binLo(static_cast<std::size_t>(numBands)), binHi(static_cast<std::size_t>(numBands));
    for (int b = 0; b < numBands; ++b) {
        const double c = centers[static_cast<std::size_t>(b)];
        binLo[static_cast<std::size_t>(b)] = std::max(1, static_cast<int>(std::ceil(c / std::pow(2.0, 1.0 / 6.0) * kFftSize / fs)));
        binHi[static_cast<std::size_t>(b)] = std::max(binLo[static_cast<std::size_t>(b)],
                                                      static_cast<int>(std::floor(c * std::pow(2.0, 1.0 / 6.0) * kFftSize / fs)));
    }

    dsp::Fft fft(kFftSize);
    std::vector<double> window(kFftSize);
    for (int n = 0; n < kFftSize; ++n) window[static_cast<std::size_t>(n)] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * n / kFftSize);
    std::vector<std::complex<double>> bufX(kFftSize), bufY(kFftSize);
    std::vector<double> residual(static_cast<std::size_t>(numBands), 0.0), explained(static_cast<std::size_t>(numBands), 0.0);
    const int half = kFftSize / 2;

    // Analyse en Mid/Side : le Mono Maker est linéaire sur M et sur S, pas
    // sur L et R pris isolément.
    const auto channelSignal = [&](const AudioFile& f, int which, int n) {
        const double l = f.channels[0][static_cast<std::size_t>(n)];
        const double r = f.channels[static_cast<std::size_t>(std::min(1, f.numChannels() - 1))][static_cast<std::size_t>(n)];
        return which == 0 ? 0.5 * (l + r) : 0.5 * (l - r);
    };
    const auto transform = [&](const AudioFile& f, int which, int start, std::vector<std::complex<double>>& buf) {
        for (int n = 0; n < kFftSize; ++n)
            buf[static_cast<std::size_t>(n)] = {channelSignal(f, which, start + n) * window[static_cast<std::size_t>(n)], 0.0};
        fft.forward(buf.data());
    };

    for (int ch = 0; ch < channels; ++ch) {
        // Passe 1 : filtre linéaire invariant bin par bin, H_k = Σ Y·X* / Σ |X|²
        // (absorbe EQ, retard, rotation de phase, Mono Maker).
        std::vector<std::complex<double>> cross(static_cast<std::size_t>(half + 1));
        std::vector<double> autoX(static_cast<std::size_t>(half + 1), 0.0);
        for (int start = 0; start + kFftSize <= frames; start += kHop) {
            transform(reference, ch, start, bufX);
            transform(processed, ch, start, bufY);
            for (int k = 0; k <= half; ++k) {
                cross[static_cast<std::size_t>(k)] += bufY[static_cast<std::size_t>(k)] * std::conj(bufX[static_cast<std::size_t>(k)]);
                autoX[static_cast<std::size_t>(k)] += std::norm(bufX[static_cast<std::size_t>(k)]);
            }
        }
        std::vector<std::complex<double>> h(static_cast<std::size_t>(half + 1));
        for (int k = 0; k <= half; ++k)
            h[static_cast<std::size_t>(k)] = autoX[static_cast<std::size_t>(k)] > 0.0
                                                 ? cross[static_cast<std::size_t>(k)] / autoX[static_cast<std::size_t>(k)]
                                                 : std::complex<double>{};

        // Passe 2 : en plus, un gain complexe par bande et par trame (variations
        // lentes de niveau : limiteur, compresseur). Le reste est non linéaire.
        for (int start = 0; start + kFftSize <= frames; start += kHop) {
            transform(reference, ch, start, bufX);
            transform(processed, ch, start, bufY);
            for (int b = 0; b < numBands; ++b) {
                std::complex<double> c{};
                double ex = 0.0, ey = 0.0;
                for (int k = binLo[static_cast<std::size_t>(b)]; k <= binHi[static_cast<std::size_t>(b)]; ++k) {
                    const auto X = h[static_cast<std::size_t>(k)] * bufX[static_cast<std::size_t>(k)];
                    const auto Y = bufY[static_cast<std::size_t>(k)];
                    c += Y * std::conj(X);
                    ex += std::norm(X);
                    ey += std::norm(Y);
                }
                if (ex <= 1e-24) {
                    residual[static_cast<std::size_t>(b)] += ey; // rien à expliquer : tout est ajouté
                    continue;
                }
                const double fit = std::norm(c) / ex; // énergie de g·X, g optimal
                explained[static_cast<std::size_t>(b)] += fit;
                residual[static_cast<std::size_t>(b)] += std::max(0.0, ey - fit);
            }
        }
    }

    const auto ratioDb = [](double r, double e) { return e > 0.0 ? powerToDb(std::max(r, 1e-30) / e) : 0.0; };
    double wr = 0.0, we = 0.0;
    struct Range {
        double lo, hi;
        double r = 0.0, e = 0.0;
    };
    Range ranges[4] = {{30, 120}, {120, 500}, {500, 5000}, {5000, 16500}};
    for (int b = 0; b < numBands; ++b) {
        const auto i = static_cast<std::size_t>(b);
        report.bands.push_back({centers[i], ratioDb(residual[i], explained[i])});
        const double k = kWeightPower(centers[i], fs);
        wr += k * residual[i];
        we += k * explained[i];
        for (auto& r : ranges)
            if (centers[i] >= r.lo && centers[i] < r.hi) {
                r.r += residual[i];
                r.e += explained[i];
            }
    }
    report.weightedDb = ratioDb(wr, we);
    report.subDb = ratioDb(ranges[0].r, ranges[0].e);
    report.lowMidDb = ratioDb(ranges[1].r, ranges[1].e);
    report.midDb = ratioDb(ranges[2].r, ranges[2].e);
    report.highDb = ratioDb(ranges[3].r, ranges[3].e);
    return report;
}

std::string DistortionReport::toText(const std::string& title) const {
    std::ostringstream o;
    char line[160];
    if (!title.empty()) o << "== " << title << " ==\n";
    std::snprintf(line, sizeof(line),
                  "Distorsion (pondérée K) : %6.1f dB | sub %6.1f | bas-médium %6.1f | médium %6.1f | aigu %6.1f\n",
                  weightedDb, subDb, lowMidDb, midDb, highDb);
    o << line;
    return o.str();
}

} // namespace omnis::tools
