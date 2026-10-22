// =============================================================================
//  Abrasion Labs // OMNIS
//  analysis/SpectralSignature.cpp
// =============================================================================
#include "SpectralSignature.h"

#include "core/Decibels.h"

#include <future>
#include <thread>

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

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    const auto k = static_cast<std::size_t>(std::clamp(p, 0.0, 1.0) * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

} // namespace

void SpectralSignature::detectCutoff() noexcept {
    trustedBands = kBands;
    if (kBands < 3 || midDb[static_cast<std::size_t>(kBands - 3)] <= -150.0) return; // rien de mesuré à 10 kHz
    const auto at = [&](int k) { return midDb[static_cast<std::size_t>(k)]; };
    const int top = kBands - 1, below = kBands - 2, ref = kBands - 3; // 20 k, 14 k, 10 k
    if (at(top) - at(ref) < -20.0) trustedBands = top;
    if (trustedBands == top && at(below) - at(ref) < -12.0) trustedBands = below;
}

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

void SignatureAccumulator::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    n_ = fftSizeFor(sampleRate);
    const int bins = n_ / 2;
    fft_.prepare(n_);
    window_.resize(static_cast<std::size_t>(n_));
    for (int i = 0; i < n_; ++i) window_[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / n_);
    pendingL_.assign(static_cast<std::size_t>(n_), 0.0);
    pendingR_.assign(static_cast<std::size_t>(n_), 0.0);
    workspace_.pm.assign(static_cast<std::size_t>(bins), 0.0);
    workspace_.ps.assign(static_cast<std::size_t>(bins), 0.0);
    workspace_.buf.assign(static_cast<std::size_t>(n_), {});
    for (int k = 0; k < kBands; ++k) {
        const double fc = params::matchBandHz(k);
        auto& w = nodes_[static_cast<std::size_t>(k)];
        w = NodeWeights{};
        // Les nœuds extrêmes couvrent tout ce qui est au-delà (pas de trou aux bords).
        const double fLo = k == 0 ? 1.0 : fc * std::exp2(-0.5);
        const double fHi = k == kBands - 1 ? 0.5 * sampleRate : fc * std::exp2(0.5);
        const int lo = std::max(1, static_cast<int>(std::ceil(fLo * n_ / sampleRate)));
        const int hi = std::min(bins - 1, static_cast<int>(std::floor(fHi * n_ / sampleRate)));
        w.firstBin = lo;
        for (int b = lo; b <= hi; ++b) {
            const double f = b * sampleRate / n_;
            double d = 2.0 * std::log2(f / fc); // en nœuds
            if ((k == 0 && d < 0.0) || (k == kBands - 1 && d > 0.0)) d = 0.0;
            const double c = std::cos(0.5 * std::numbers::pi * std::min(1.0, std::fabs(d)));
            const double g = c * c;
            w.weight.push_back(g * f);
            w.norm += g;
        }
    }
    loudness_.prepare(sampleRate, 2);
    peaks_.prepare(2);
    reset();
}

void SignatureAccumulator::reset() {
    filled_ = 0;
    parallelTruePeak_ = -1.0;
    samples_ = 0;
    frames_.clear();
    loudness_.reset();
    peaks_.reset();
}

void SignatureAccumulator::push(const double* left, const double* right, int numFrames) {
    const double* ch[2] = {left, right};
    loudness_.process(ch, numFrames);
    peaks_.process(ch, numFrames);
    samples_ += numFrames;
    const int hop = n_ / 2;
    for (int i = 0; i < numFrames;) {
        const int take = std::min(numFrames - i, n_ - filled_);
        std::copy(left + i, left + i + take, pendingL_.begin() + filled_);
        std::copy(right + i, right + i + take, pendingR_.begin() + filled_);
        filled_ += take;
        i += take;
        if (filled_ == n_) {
            frames_.emplace_back();
            analyseFrame(pendingL_.data(), pendingR_.data(), workspace_, frames_.back());
            // Recouvrement de 50 % : la seconde moitié devient le début de la trame suivante.
            std::copy(pendingL_.begin() + hop, pendingL_.end(), pendingL_.begin());
            std::copy(pendingR_.begin() + hop, pendingR_.end(), pendingR_.begin());
            filled_ = n_ - hop;
        }
    }
}

void SignatureAccumulator::analyseFrame(const double* left, const double* right, Workspace& ws, Frame& fr) const {
    const int bins = n_ / 2;
    // Mid en partie réelle, Side en partie imaginaire : une FFT pour deux.
    for (int i = 0; i < n_; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        const double w = window_[idx];
        ws.buf[idx] = {0.5 * (left[i] + right[i]) * w, 0.5 * (left[i] - right[i]) * w};
    }
    fft_.forward(ws.buf.data());
    fr = Frame{};
    for (int b = 1; b < bins; ++b) {
        const auto z = ws.buf[static_cast<std::size_t>(b)];
        const auto zc = std::conj(ws.buf[static_cast<std::size_t>(n_ - b)]);
        ws.pm[static_cast<std::size_t>(b)] = std::norm(0.5 * (z + zc));
        ws.ps[static_cast<std::size_t>(b)] = std::norm(0.5 * (z - zc));
        fr.total += ws.pm[static_cast<std::size_t>(b)] + ws.ps[static_cast<std::size_t>(b)];
    }
    for (int k = 0; k < kBands; ++k) {
        const auto& w = nodes_[static_cast<std::size_t>(k)];
        double am = 0.0, as = 0.0;
        for (std::size_t j = 0; j < w.weight.size(); ++j) {
            am += w.weight[j] * ws.pm[static_cast<std::size_t>(w.firstBin) + j];
            as += w.weight[j] * ws.ps[static_cast<std::size_t>(w.firstBin) + j];
        }
        fr.mid[static_cast<std::size_t>(k)] = w.norm > 0.0 ? am / w.norm : 0.0;
        fr.side[static_cast<std::size_t>(k)] = w.norm > 0.0 ? as / w.norm : 0.0;
    }
}

SpectralSignature SignatureAccumulator::result() const {
    SpectralSignature sig;
    sig.integratedLufs = loudness_.integratedLufs();
    sig.loudnessRangeLu = loudness_.loudnessRangeLu();
    sig.truePeakDb = gainToDb(parallelTruePeak_ >= 0.0 ? parallelTruePeak_ : peaks_.truePeakMax());
    sig.plrDb = std::isfinite(sig.integratedLufs) ? sig.truePeakDb - sig.integratedLufs : 0.0;
    if (frames_.empty()) return sig;

    // ---- Porte : seules les trames « actives » décrivent le mix ---------------
    std::vector<double> totals;
    for (const auto& f : frames_) totals.push_back(f.total);
    const double gate = percentile(totals, 0.95) * dbToPower(-kCoreGateDb); // cœur du morceau
    std::array<double, kBands> sumM{}, sumS{};
    std::array<std::vector<double>, kBands> levels;
    for (const auto& f : frames_) {
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
    sig.detectCutoff();
    return sig;
}

SpectralSignature SignatureAccumulator::analyzeParallel(const double* left, const double* right, int numFrames, double sampleRate,
                                                        unsigned threads) {
    if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
    SignatureAccumulator a;
    a.prepare(sampleRate);
    const int hop = a.n_ / 2;
    const int count = numFrames >= a.n_ ? (numFrames - a.n_) / hop + 1 : 0;
    a.frames_.resize(static_cast<std::size_t>(count));
    a.samples_ = numFrames;

    std::vector<std::future<double>> peaks;
    std::vector<std::future<void>> frames;
    const unsigned parts = std::min<unsigned>(threads, static_cast<unsigned>(std::max(1, count)));
    for (unsigned t = 0; t < parts; ++t) {
        // Trames [j0, j1) : chaque thread a son espace de travail, la FFT est partagée (lecture seule).
        const int j0 = static_cast<int>(static_cast<long long>(count) * t / parts);
        const int j1 = static_cast<int>(static_cast<long long>(count) * (t + 1) / parts);
        frames.push_back(std::async(std::launch::async, [&a, left, right, j0, j1, hop] {
            Workspace ws;
            ws.buf.assign(static_cast<std::size_t>(a.n_), {});
            ws.pm.assign(static_cast<std::size_t>(a.n_ / 2), 0.0);
            ws.ps.assign(static_cast<std::size_t>(a.n_ / 2), 0.0);
            for (int j = j0; j < j1; ++j)
                a.analyseFrame(left + static_cast<std::ptrdiff_t>(j) * hop, right + static_cast<std::ptrdiff_t>(j) * hop, ws,
                               a.frames_[static_cast<std::size_t>(j)]);
        }));
        // Crête vraie du segment [s0, s1), amorcée par les échantillons qui le précèdent.
        const int s0 = static_cast<int>(static_cast<long long>(numFrames) * t / parts);
        const int s1 = static_cast<int>(static_cast<long long>(numFrames) * (t + 1) / parts);
        peaks.push_back(std::async(std::launch::async, [left, right, s0, s1] {
            TruePeakMeter meter;
            meter.prepare(2);
            const int prime = std::min(s0, TruePeakMeter::kTapsPerPhase - 1);
            if (prime > 0) {
                const double* ch[2] = {left + s0 - prime, right + s0 - prime};
                meter.process(ch, prime);
                meter.resetPeaks();
            }
            const double* ch[2] = {left + s0, right + s0};
            meter.process(ch, s1 - s0);
            return meter.truePeakMax();
        }));
    }
    const double* ch[2] = {left, right};
    a.loudness_.process(ch, numFrames); // séquentielle (filtres et porte), et rapide
    double tp = 0.0;
    for (auto& f : peaks) tp = std::max(tp, f.get());
    for (auto& f : frames) f.get();
    a.parallelTruePeak_ = tp;
    return a.result();
}

SpectralSignature analyzeSignature(const double* left, const double* right, int numFrames, double sampleRate) {
    SignatureAccumulator a;
    a.prepare(sampleRate);
    a.push(left, right, numFrames);
    return a.result();
}

} // namespace omnis::analysis
