// =============================================================================
//  Abrasion Labs // OMNIS
//  analysis/MusicDescriptors.cpp
// =============================================================================
#include "MusicDescriptors.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace omnis::analysis {

namespace {

constexpr double kTiny = 1e-20;
double toDb(double p) { return 10.0 * std::log10(std::max(p, kTiny)); }

template <typename B>
void design(B& f, int kind, double hz, double q, double fs) {
    const double w = 2.0 * std::numbers::pi * hz / fs, c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    double b0, b1, b2;
    if (kind == 0) b0 = (1 - c) / 2, b1 = 1 - c, b2 = (1 - c) / 2; // passe-bas
    else b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2;        // passe-haut
    const double a0 = 1 + alpha;
    f.b0 = b0 / a0;
    f.b1 = b1 / a0;
    f.b2 = b2 / a0;
    f.a1 = -2 * c / a0;
    f.a2 = (1 - alpha) / a0;
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = p * static_cast<double>(v.size() - 1);
    const auto i = static_cast<std::size_t>(pos);
    const double t = pos - static_cast<double>(i);
    return i + 1 < v.size() ? v[i] * (1 - t) + v[i + 1] * t : v[i];
}

// Profils de tonalité (Krumhansl-Kessler).
constexpr std::array<double, 12> kMajor{6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr std::array<double, 12> kMinor{6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
const std::array<const char*, 12> kNotesFr{"Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"};
const std::array<const char*, 12> kNotesCode{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

double correlate(const std::array<double, 12>& a, const std::array<double, 12>& profile, int shift) {
    double ma = 0, mp = 0;
    for (int i = 0; i < 12; ++i) {
        ma += a[static_cast<std::size_t>(i)];
        mp += profile[static_cast<std::size_t>(i)];
    }
    ma /= 12;
    mp /= 12;
    double num = 0, da = 0, dp = 0;
    for (int i = 0; i < 12; ++i) {
        const double x = a[static_cast<std::size_t>((i + shift) % 12)] - ma, y = profile[static_cast<std::size_t>(i)] - mp;
        num += x * y;
        da += x * x;
        dp += y * y;
    }
    return da > 0 && dp > 0 ? num / std::sqrt(da * dp) : 0.0;
}

void onShortTerm(void* user, double, double shortTerm, bool valid) {
    if (valid && std::isfinite(shortTerm)) static_cast<std::vector<double>*>(user)->push_back(shortTerm);
}

} // namespace

std::string MusicDescriptors::keyName() const {
    if (keyRoot < 0 || keyRoot > 11) return "?";
    return std::string(kNotesFr[static_cast<std::size_t>(keyRoot)]) + (keyMinor ? " mineur" : " majeur");
}

std::string MusicDescriptors::keyCode() const {
    if (keyRoot < 0 || keyRoot > 11) return {};
    return std::string(kNotesCode[static_cast<std::size_t>(keyRoot)]) + (keyMinor ? "m" : "");
}

bool MusicDescriptors::parseKeyCode(const std::string& code) {
    std::string c = code;
    bool minor = false;
    if (!c.empty() && c.back() == 'm') {
        minor = true;
        c.pop_back();
    }
    for (int i = 0; i < 12; ++i)
        if (c == kNotesCode[static_cast<std::size_t>(i)]) {
            keyRoot = i;
            keyMinor = minor;
            return true;
        }
    return false;
}

std::string MusicDescriptors::noteName(double hz) {
    if (!(hz > 0.0)) return "?";
    const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
    const int n = static_cast<int>(std::lround(midi));
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s (%.0f Hz)", kNotesFr[static_cast<std::size_t>(((n % 12) + 12) % 12)], hz);
    return buf;
}

void MusicAnalyzer::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    hopSize_ = std::max(1, static_cast<int>(std::lround(0.01 * sampleRate)));
    blockSize_ = sampleRate > 60000.0 ? 16384 : 8192;
    fft_.prepare(blockSize_);
    window_.resize(static_cast<std::size_t>(blockSize_));
    for (int i = 0; i < blockSize_; ++i) window_[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (blockSize_ - 1));
    blockMid_.assign(static_cast<std::size_t>(blockSize_), 0.0);
    work_.resize(static_cast<std::size_t>(blockSize_));
    for (int c = 0; c < 2; ++c) {
        design(lowL_[static_cast<std::size_t>(c)], 0, 120.0, 0.7071, sampleRate);
        design(lowR_[static_cast<std::size_t>(c)], 0, 120.0, 0.7071, sampleRate);
    }
    // Zones : < 120, 120 – 800, 800 – 5k, > 5k (deux filtres en cascade par zone).
    design(zone_[0][0], 0, 120.0, 0.7071, sampleRate);
    design(zone_[0][1], 0, 120.0, 0.7071, sampleRate);
    design(zone_[1][0], 1, 120.0, 0.7071, sampleRate);
    design(zone_[1][1], 0, 800.0, 0.7071, sampleRate);
    design(zone_[2][0], 1, 800.0, 0.7071, sampleRate);
    design(zone_[2][1], 0, 5000.0, 0.7071, sampleRate);
    design(zone_[3][0], 1, 5000.0, 0.7071, sampleRate);
    design(zone_[3][1], 1, 5000.0, 0.7071, sampleRate);
    loudness_.prepare(sampleRate, 2);
    loudness_.setStepCallback(&onShortTerm, &shortTerm_);
    reset();
}

void MusicAnalyzer::reset() {
    samples_ = 0;
    sLL_ = sRR_ = sLR_ = lLL_ = lRR_ = lLR_ = sMono_ = 0.0;
    peak_ = 0.0;
    clipped_ = 0;
    for (auto& f : lowL_) f.z1 = f.z2 = 0.0;
    for (auto& f : lowR_) f.z1 = f.z2 = 0.0;
    for (auto& z : zone_)
        for (auto& f : z) f.z1 = f.z2 = 0.0;
    hopAcc_.fill(0.0);
    for (auto& w : window10_) w.clear();
    for (auto& w : zoneEnv_) w.clear();
    for (auto& m : micro_) m.clear();
    for (auto& w : windowLevel_) w.clear();
    hopPos_ = hopsInWindow_ = 0;
    fullAcc_ = 0.0;
    envelope_.clear();
    blockPos_ = 0;
    chroma_.fill(0.0);
    subHist_.assign(81, 0.0);
    centroidNum_ = centroidDen_ = 0.0;
    blockPower_.clear();
    loudness_.reset();
    shortTerm_.clear();
}

void MusicAnalyzer::push(const double* left, const double* right, int numFrames) {
    const double* ch[2] = {left, right};
    loudness_.process(ch, numFrames);
    for (int i = 0; i < numFrames; ++i) {
        const double l = left[i], r = right[i];
        sLL_ += l * l;
        sRR_ += r * r;
        sLR_ += l * r;
        const double m = 0.5 * (l + r);
        sMono_ += m * m;
        const double al = std::fabs(l), ar = std::fabs(r);
        peak_ = std::max({peak_, al, ar});
        if (al >= 0.98855 || ar >= 0.98855) ++clipped_; // −0,1 dBFS
        double ll = l, rr = r;
        for (auto& f : lowL_) ll = f.run(ll);
        for (auto& f : lowR_) rr = f.run(rr);
        lLL_ += ll * ll;
        lRR_ += rr * rr;
        lLR_ += ll * rr;
        for (int z = 0; z < 4; ++z) {
            double y = m;
            for (auto& f : zone_[static_cast<std::size_t>(z)]) y = f.run(y);
            hopAcc_[static_cast<std::size_t>(z)] += y * y;
        }
        fullAcc_ += m * m;
        if (++hopPos_ == hopSize_) endHop();
        blockMid_[static_cast<std::size_t>(blockPos_)] = m;
        if (++blockPos_ == blockSize_) endBlock();
    }
    samples_ += numFrames;
}

void MusicAnalyzer::endHop() {
    hopPos_ = 0;
    envelope_.push_back(toDb(fullAcc_ / hopSize_));
    fullAcc_ = 0.0;
    for (int z = 0; z < 4; ++z) {
        const auto k = static_cast<std::size_t>(z);
        window10_[k].push_back(hopAcc_[k] / hopSize_);
        zoneEnv_[k].push_back(toDb(hopAcc_[k] / hopSize_));
        hopAcc_[k] = 0.0;
    }
    // Une écoute qui dure des heures (le master d'une session) : on garde les
    // 20 dernières minutes, l'analyse reste rapide et la mémoire bornée.
    constexpr std::size_t kMaxHops = 120000;
    if (envelope_.size() > kMaxHops) {
        const auto drop = static_cast<std::ptrdiff_t>(kMaxHops / 2);
        envelope_.erase(envelope_.begin(), envelope_.begin() + drop);
        for (auto& z : zoneEnv_) z.erase(z.begin(), z.begin() + std::min<std::ptrdiff_t>(drop, static_cast<std::ptrdiff_t>(z.size())));
        for (int z = 0; z < 4; ++z) {
            auto& m = micro_[static_cast<std::size_t>(z)];
            auto& w = windowLevel_[static_cast<std::size_t>(z)];
            const auto n = std::min<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(m.size()), drop / 40);
            m.erase(m.begin(), m.begin() + n);
            w.erase(w.begin(), w.begin() + n);
        }
        if (shortTerm_.size() > 12000) shortTerm_.erase(shortTerm_.begin(), shortTerm_.begin() + 6000);
    }
    if (++hopsInWindow_ == 40) { // 400 ms
        hopsInWindow_ = 0;
        for (int z = 0; z < 4; ++z) {
            const auto k = static_cast<std::size_t>(z);
            double sum = 0.0, mx = 0.0;
            for (double e : window10_[k]) {
                sum += e;
                mx = std::max(mx, e);
            }
            const double level = toDb(sum / static_cast<double>(window10_[k].size()));
            windowLevel_[k].push_back(level);
            micro_[k].push_back(toDb(mx) - level);
            window10_[k].clear();
        }
    }
}

void MusicAnalyzer::endBlock() {
    blockPos_ = 0;
    double power = 0.0;
    for (int i = 0; i < blockSize_; ++i) {
        const double x = blockMid_[static_cast<std::size_t>(i)];
        power += x * x;
        work_[static_cast<std::size_t>(i)] = {x * window_[static_cast<std::size_t>(i)], 0.0};
    }
    blockPower_.push_back(power / blockSize_);
    if (power / blockSize_ < 1e-7) return; // silence (−70 dBFS)
    fft_.forward(work_.data());
    const double binHz = sampleRate_ / blockSize_;
    std::array<double, 12> chroma{};
    double subPeak = 0.0, subPeakHz = 0.0;
    for (int k = 1; k < blockSize_ / 2; ++k) {
        const double f = k * binHz, p = std::norm(work_[static_cast<std::size_t>(k)]);
        if (f >= 200.0) {
            const double a = std::sqrt(p);
            centroidNum_ += f * a;
            centroidDen_ += a;
        }
        if (f >= 30.0 && f <= 110.0 && p > subPeak) {
            subPeak = p;
            subPeakHz = f;
            // Interpolation parabolique sur le pic (au bin près, c'est ±3 Hz à 48 kHz).
            const double a = std::norm(work_[static_cast<std::size_t>(k - 1)]), b = p, c = std::norm(work_[static_cast<std::size_t>(k + 1)]);
            const double d = a - 2 * b + c;
            if (d < 0.0) subPeakHz = f + binHz * std::clamp(0.5 * (a - c) / d, -0.5, 0.5);
        }
        if (f >= 120.0 && f <= 2000.0) { // au-dessous, un bin couvre plus d'un demi-ton
            const double midi = 69.0 + 12.0 * std::log2(f / 440.0);
            const int pc = ((static_cast<int>(std::lround(midi)) % 12) + 12) % 12;
            chroma[static_cast<std::size_t>(pc)] += std::sqrt(p);
        }
    }
    double total = 0.0;
    for (double c : chroma) total += c;
    if (total > 0.0)
        for (int i = 0; i < 12; ++i) chroma_[static_cast<std::size_t>(i)] += chroma[static_cast<std::size_t>(i)] / total;
    if (subPeakHz >= 30.0 && subPeakHz <= 110.0) {
        const auto bin = static_cast<std::size_t>(std::lround(subPeakHz - 30.0));
        if (bin < subHist_.size()) subHist_[bin] += subPeak;
    }
}

MusicDescriptors MusicAnalyzer::result() const {
    MusicDescriptors d;
    if (samples_ <= 0) return d;
    d.valid = true;
    d.seconds = static_cast<double>(samples_) / sampleRate_;
    d.samplePeakDb = 20.0 * std::log10(std::max(peak_, 1e-10));
    d.clipPercent = 100.0 * static_cast<double>(clipped_) / static_cast<double>(samples_);
    d.correlation = sLL_ > 0 && sRR_ > 0 ? sLR_ / std::sqrt(sLL_ * sRR_) : 1.0;
    d.lowCorrelation = lLL_ > 0 && lRR_ > 0 ? lLR_ / std::sqrt(lLL_ * lRR_) : 1.0;
    d.monoLossDb = sLL_ + sRR_ > 0 ? toDb(sMono_ / (0.5 * (sLL_ + sRR_))) : 0.0;
    // Sections : le son qui joue (à 20 dB du refrain).
    if (!shortTerm_.empty()) {
        const double top = percentile(shortTerm_, 0.95);
        std::vector<double> active;
        for (double v : shortTerm_)
            if (v > top - 20.0) active.push_back(v);
        d.shortTermLufs = {percentile(active, 0.10), percentile(active, 0.50), percentile(active, 0.95)};
    }
    // Micro-dynamique : fenêtres où la zone joue (à 20 dB de sa fenêtre la plus forte).
    for (int z = 0; z < 4; ++z) {
        const auto k = static_cast<std::size_t>(z);
        if (micro_[k].empty()) continue;
        const double loud = percentile(windowLevel_[k], 0.95);
        std::vector<double> v;
        for (std::size_t i = 0; i < micro_[k].size(); ++i)
            if (windowLevel_[k][i] > loud - 20.0) v.push_back(micro_[k][i]);
        d.microDb[k] = percentile(v, 0.5);
    }
    // Flux des attaques : dans chaque zone, la montée (dB) au-dessus des 50 ms d'avant,
    // si la zone joue (à 40 dB de son plus fort) ; un coup = une zone qui monte de 6 dB.
    const std::size_t hops = zoneEnv_[0].size();
    std::vector<double> flux(hops, 0.0);
    std::vector<bool> hit(hops, false);
    for (int z = 0; z < 4; ++z) {
        const auto& e = zoneEnv_[static_cast<std::size_t>(z)];
        if (e.empty()) continue;
        const double loud = percentile(e, 0.95);
        for (std::size_t i = 5; i < e.size(); ++i) {
            if (e[i] < loud - 40.0) continue;
            double mean = 0.0;
            for (std::size_t j = i - 5; j < i; ++j) mean += std::pow(10.0, e[j] / 10.0);
            const double rise = e[i] - toDb(mean / 5.0);
            flux[i] += std::clamp(rise, 0.0, 30.0);
            if (rise > 6.0) hit[i] = true;
        }
    }
    int onsets = 0, activeHops = 0;
    const double loudEnv = envelope_.empty() ? -200.0 : percentile(envelope_, 0.95);
    for (std::size_t i = 1; i < hops && i < envelope_.size(); ++i) {
        if (envelope_[i] < loudEnv - 30.0) continue;
        ++activeHops;
        if (hit[i] && !hit[i - 1]) ++onsets;
    }
    d.onsetsPerSecond = activeHops > 0 ? onsets / (activeHops * 0.01) : 0.0;
    // Tempo : autocorrélation du flux ; un tempo vaut sa période + la moitié de son double
    // et de sa moitié (les mesures et les croches le confirment), cherché entre 85 et 170 BPM.
    if (flux.size() > 1000) {
        // Les 60 dernières secondes suffisent (et gardent le calcul court).
        if (flux.size() > 6000) flux.erase(flux.begin(), flux.end() - 6000);
        double mean = 0.0;
        for (double v : flux) mean += v;
        mean /= static_cast<double>(flux.size());
        constexpr int kMaxLag = 220;
        std::vector<double> r(kMaxLag + 2, 0.0);
        for (int lag = 10; lag <= kMaxLag + 1; ++lag) {
            double acc = 0.0;
            for (std::size_t i = static_cast<std::size_t>(lag); i < flux.size(); ++i) acc += (flux[i] - mean) * (flux[i - static_cast<std::size_t>(lag)] - mean);
            r[static_cast<std::size_t>(lag)] = acc / static_cast<double>(flux.size() - static_cast<std::size_t>(lag));
        }
        const auto at = [&](double lag) {
            if (lag < 10.0 || lag > kMaxLag) return 0.0;
            const auto i = static_cast<std::size_t>(lag);
            const double t = lag - static_cast<double>(i);
            return r[i] * (1.0 - t) + r[i + 1] * t;
        };
        double best = -1e300;
        for (double bpm = 85.0; bpm <= 170.0; bpm += 0.5) {
            const double lag = 6000.0 / bpm;
            const double score = at(lag) + 0.5 * at(2.0 * lag) + 0.5 * at(0.5 * lag) + 0.25 * at(4.0 * lag);
            if (score > best) {
                best = score;
                d.bpm = bpm;
            }
        }
    }
    // Tonalité.
    double best = -2.0, second = -2.0;
    for (int root = 0; root < 12; ++root)
        for (int minor = 0; minor < 2; ++minor) {
            const double c = correlate(chroma_, minor ? kMinor : kMajor, root);
            if (c > best) {
                second = best;
                best = c;
                d.keyRoot = root;
                d.keyMinor = minor != 0;
            } else if (c > second) {
                second = c;
            }
        }
    d.keyConfidence = std::clamp(best - second, 0.0, 1.0) * 4.0;
    d.keyConfidence = std::min(d.keyConfidence, 1.0);
    if (best < 0.3 || d.keyConfidence < 0.05) d.keyRoot = -1; // pas de tonalité nette (atonal, très chargé)
    // Note de la 808.
    const auto mode = std::max_element(subHist_.begin(), subHist_.end());
    double subTotal = 0.0;
    for (double v : subHist_) subTotal += v;
    if (mode != subHist_.end() && *mode > 0.0 && *mode > 0.08 * subTotal) d.subHz = 30.0 + static_cast<double>(mode - subHist_.begin());
    d.centroidHz = centroidDen_ > 0.0 ? centroidNum_ / centroidDen_ : 0.0;
    return d;
}

MusicDescriptors analyzeMusic(const double* left, const double* right, int numFrames, double sampleRate) {
    MusicAnalyzer a;
    a.prepare(sampleRate);
    for (int pos = 0; pos < numFrames; pos += 8192) a.push(left + pos, right + pos, std::min(8192, numFrames - pos));
    return a.result();
}

} // namespace omnis::analysis
