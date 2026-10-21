// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/TonalBalance.cpp
// =============================================================================
#include "TonalBalance.h"

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

constexpr int kFftSize = 8192;

std::vector<double> octavePowers(const AudioFile& f, const std::vector<double>& centers) {
    dsp::Fft fft(kFftSize);
    std::vector<double> window(kFftSize), acc(kFftSize / 2 + 1, 0.0);
    for (int n = 0; n < kFftSize; ++n) window[static_cast<std::size_t>(n)] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * n / kFftSize);
    std::vector<std::complex<double>> buf(kFftSize);
    const int frames = f.numFrames();
    const auto& l = f.channels[0];
    const auto& r = f.channels[std::min<std::size_t>(1, f.channels.size() - 1)];
    for (int pass = 0; pass < 2; ++pass) { // Mid puis Side : toute l'énergie stéréo
        for (int start = 0; start + kFftSize <= frames; start += kFftSize / 2) {
            for (int n = 0; n < kFftSize; ++n) {
                const auto i = static_cast<std::size_t>(start + n);
                const double v = pass == 0 ? 0.5 * (l[i] + r[i]) : 0.5 * (l[i] - r[i]);
                buf[static_cast<std::size_t>(n)] = {v * window[static_cast<std::size_t>(n)], 0.0};
            }
            fft.forward(buf.data());
            for (int k = 0; k <= kFftSize / 2; ++k) acc[static_cast<std::size_t>(k)] += std::norm(buf[static_cast<std::size_t>(k)]);
        }
    }
    std::vector<double> out;
    for (double c : centers) {
        const int lo = std::max(1, static_cast<int>(std::ceil(c / std::sqrt(2.0) * kFftSize / f.sampleRate)));
        const int hi = std::min(kFftSize / 2, static_cast<int>(std::floor(c * std::sqrt(2.0) * kFftSize / f.sampleRate)));
        double p = 0.0;
        for (int k = lo; k <= hi; ++k) p += acc[static_cast<std::size_t>(k)];
        out.push_back(p);
    }
    return out;
}

} // namespace

TonalReport compareTonalBalance(const AudioFile& reference, const AudioFile& processed) {
    std::vector<double> centers;
    for (double c = 31.5; c <= 16100.0; c *= 2.0) centers.push_back(c);
    const auto a = octavePowers(reference, centers);
    const auto b = octavePowers(processed, centers);
    double totalA = 0.0, totalB = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        totalA += a[i];
        totalB += b[i];
    }
    TonalReport report;
    if (totalA <= 0.0 || totalB <= 0.0) return report;
    // Écart relatif : chaque bande comparée à sa part de l'énergie totale.
    for (std::size_t i = 0; i < a.size(); ++i) {
        TonalBand band;
        band.centerHz = centers[i];
        band.sharePercent = 100.0 * a[i] / totalA;
        band.deviationDb = (a[i] > 0.0 && b[i] > 0.0) ? powerToDb((b[i] / totalB) / (a[i] / totalA)) : 0.0;
        if (band.sharePercent >= 0.5) report.maxDeviationDb = std::max(report.maxDeviationDb, std::fabs(band.deviationDb));
        report.bands.push_back(band);
    }
    return report;
}

std::string TonalReport::toText() const {
    std::ostringstream o;
    o << "Équilibre tonal (écart par octave, dB) :";
    char buf[48];
    for (const auto& b : bands) {
        std::snprintf(buf, sizeof(buf), " %.0f:%+.1f", b.centerHz, b.deviationDb);
        o << buf;
    }
    std::snprintf(buf, sizeof(buf), " | max %.1f dB\n", maxDeviationDb);
    o << buf;
    return o.str();
}

} // namespace omnis::tools
