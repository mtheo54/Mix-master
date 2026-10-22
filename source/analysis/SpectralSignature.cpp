// =============================================================================
//  Abrasion Labs // OMNIS
//  analysis/SpectralSignature.cpp
// =============================================================================
#include "SpectralSignature.h"

#include "LoudnessMeter.h"
#include "TruePeakMeter.h"

#include "core/Decibels.h"
#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace omnis::analysis {

namespace {

constexpr int kBands = params::kMatchBands;

int fftSizeFor(double sampleRate) {
    int n = 1024;
    while (n < 0.37 * sampleRate) n *= 2; // 16384 à 44.1 / 48 kHz
    return n;
}

/// Poids d'un nœud sur les bins de la FFT : cos²(π/2 · d), d = écart en nœuds
/// (|d| < 1). Deux nœuds voisins se partagent chaque bin (somme = 1).
struct NodeWeights {
    int firstBin = 0;
    std::vector<double> weight; ///< déjà multiplié par f (puissance par octave)
    double norm = 0.0;
};

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    const auto k = static_cast<std::size_t>(std::clamp(p, 0.0, 1.0) * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

} // namespace

double SpectralSignature::relativeMidDb(int band) const noexcept { return midDb[static_cast<std::size_t>(band)] - bodyDb(); }

double SpectralSignature::bodyDb() const noexcept {
    double sum = 0.0;
    int count = 0;
    for (int k = 0; k < kBands; ++k) {
        const double f = params::matchBandHz(k);
        if (f >= 200.0 && f <= 5000.0) {
            sum += midDb[static_cast<std::size_t>(k)];
            ++count;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

SpectralSignature analyzeSignature(const double* left, const double* right, int numFrames, double sampleRate) {
    SpectralSignature sig;

    // ---- Sonie et crêtes ----------------------------------------------------
    {
        LoudnessMeter loudness;
        loudness.prepare(sampleRate, 2);
        TruePeakMeter peaks;
        peaks.prepare(2);
        const double* ch[2] = {left, right};
        loudness.process(ch, numFrames);
        peaks.process(ch, numFrames);
        sig.integratedLufs = loudness.integratedLufs();
        sig.loudnessRangeLu = loudness.loudnessRangeLu();
        sig.truePeakDb = gainToDb(peaks.truePeakMax());
        sig.plrDb = std::isfinite(sig.integratedLufs) ? sig.truePeakDb - sig.integratedLufs : 0.0;
    }

    // ---- Spectres par trame -------------------------------------------------
    const int n = fftSizeFor(sampleRate);
    const int hop = n / 2;
    const int bins = n / 2;
    dsp::Fft fft(n);
    std::vector<double> window(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / n);

    std::array<NodeWeights, kBands> nodes;
    for (int k = 0; k < kBands; ++k) {
        const double fc = params::matchBandHz(k);
        auto& w = nodes[static_cast<std::size_t>(k)];
        // Les nœuds extrêmes couvrent tout ce qui est au-delà (pas de trou aux bords).
        const double fLo = k == 0 ? 1.0 : fc * std::exp2(-0.5);
        const double fHi = k == kBands - 1 ? 0.5 * sampleRate : fc * std::exp2(0.5);
        const int lo = std::max(1, static_cast<int>(std::ceil(fLo * n / sampleRate)));
        const int hi = std::min(bins - 1, static_cast<int>(std::floor(fHi * n / sampleRate)));
        w.firstBin = lo;
        for (int b = lo; b <= hi; ++b) {
            const double f = b * sampleRate / n;
            double d = 2.0 * std::log2(f / fc); // en nœuds
            if ((k == 0 && d < 0.0) || (k == kBands - 1 && d > 0.0)) d = 0.0;
            const double c = std::cos(0.5 * std::numbers::pi * std::min(1.0, std::fabs(d)));
            const double g = c * c;
            w.weight.push_back(g * f);
            w.norm += g;
        }
    }

    struct Frame {
        std::array<double, kBands> mid{}, side{};
        double total = 0.0;
    };
    std::vector<Frame> frames;
    std::vector<std::complex<double>> buf(static_cast<std::size_t>(n));
    std::vector<double> pm(static_cast<std::size_t>(bins)), ps(static_cast<std::size_t>(bins));
    for (int start = 0; start + n <= numFrames; start += hop) {
        // Mid en partie réelle, Side en partie imaginaire : une FFT pour deux.
        for (int i = 0; i < n; ++i) {
            const auto idx = static_cast<std::size_t>(start + i);
            const double w = window[static_cast<std::size_t>(i)];
            buf[static_cast<std::size_t>(i)] = {0.5 * (left[idx] + right[idx]) * w, 0.5 * (left[idx] - right[idx]) * w};
        }
        fft.forward(buf.data());
        Frame fr;
        for (int b = 1; b < bins; ++b) {
            const auto z = buf[static_cast<std::size_t>(b)];
            const auto zc = std::conj(buf[static_cast<std::size_t>(n - b)]);
            pm[static_cast<std::size_t>(b)] = std::norm(0.5 * (z + zc));
            ps[static_cast<std::size_t>(b)] = std::norm(0.5 * (z - zc));
            fr.total += pm[static_cast<std::size_t>(b)] + ps[static_cast<std::size_t>(b)];
        }
        for (int k = 0; k < kBands; ++k) {
            const auto& w = nodes[static_cast<std::size_t>(k)];
            double am = 0.0, as = 0.0;
            for (std::size_t j = 0; j < w.weight.size(); ++j) {
                am += w.weight[j] * pm[static_cast<std::size_t>(w.firstBin) + j];
                as += w.weight[j] * ps[static_cast<std::size_t>(w.firstBin) + j];
            }
            fr.mid[static_cast<std::size_t>(k)] = w.norm > 0.0 ? am / w.norm : 0.0;
            fr.side[static_cast<std::size_t>(k)] = w.norm > 0.0 ? as / w.norm : 0.0;
        }
        frames.push_back(fr);
    }
    if (frames.empty()) return sig;

    // ---- Porte : seules les trames « actives » décrivent le mix ---------------
    std::vector<double> totals;
    for (const auto& f : frames) totals.push_back(f.total);
    const double gate = percentile(totals, 0.95) * 0.01; // −20 dB
    std::array<double, kBands> sumM{}, sumS{};
    std::array<std::vector<double>, kBands> levels;
    for (const auto& f : frames) {
        if (f.total <= gate || f.total <= 0.0) continue;
        ++sig.activeFrames;
        for (std::size_t k = 0; k < static_cast<std::size_t>(kBands); ++k) {
            sumM[k] += f.mid[k];
            sumS[k] += f.side[k];
            levels[k].push_back(powerToDb(f.mid[k], -200.0));
        }
    }
    const double count = std::max(1, sig.activeFrames);
    for (std::size_t k = 0; k < static_cast<std::size_t>(kBands); ++k) {
        sig.midDb[k] = powerToDb(sumM[k] / count, -200.0);
        sig.sideDb[k] = powerToDb(sumS[k] / count, -200.0);
        sig.midSpreadDb[k] = percentile(levels[k], 0.9) - percentile(levels[k], 0.1);
    }
    double sub = 0.0;
    for (std::size_t k = 0; k < static_cast<std::size_t>(SpectralSignature::kSubBands); ++k) sub += sumM[k] / count;
    sig.subEnergyDb = powerToDb(sub, -200.0);
    return sig;
}

} // namespace omnis::analysis
