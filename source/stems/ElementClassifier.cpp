// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/ElementClassifier.cpp
// =============================================================================
#include "ElementClassifier.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace omnis::stems {

namespace {

constexpr double kBandEdgesHz[kAnalysisBands + 1] = {20.0, 60.0, 150.0, 400.0, 2000.0, 6000.0, 20000.0};
constexpr double kTiny = 1e-20;
constexpr double kSilenceDb = -90.0;

double toDb(double power) { return 10.0 * std::log10(power + kTiny); }

/// Rampe montante 0 -> 1 entre a et b ; descendante pour down().
double up(double x, double a, double b) { return std::clamp((x - a) / (b - a), 0.0, 1.0); }
double down(double x, double a, double b) { return 1.0 - up(x, a, b); }
/// Plateau : monte de a à b, redescend de c à d.
double band(double x, double a, double b, double c, double d) { return up(x, a, b) * down(x, c, d); }

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

double fractionAbove(const std::vector<double>& v, double threshold) {
    if (v.empty()) return 0.0;
    return static_cast<double>(std::count_if(v.begin(), v.end(), [&](double x) { return x > threshold; })) / static_cast<double>(v.size());
}

} // namespace

void ElementClassifier::prepare(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    // ~46 ms à 44.1 / 48 kHz, la même durée aux taux plus élevés.
    size_ = sampleRate_ <= 50000.0 ? 2048 : sampleRate_ <= 100000.0 ? 4096 : sampleRate_ <= 200000.0 ? 8192 : 16384;
    hop_ = size_ / 4;
    fft_.prepare(size_);
    window_.resize(static_cast<std::size_t>(size_));
    for (int i = 0; i < size_; ++i) window_[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * (i + 0.5) / size_);
    mono_.assign(static_cast<std::size_t>(size_), 0.0);
    side_.assign(static_cast<std::size_t>(size_), 0.0);
    work_.assign(static_cast<std::size_t>(size_), {});
    const int half = size_ / 2;
    const auto bin = [&](double hz) { return std::clamp(static_cast<int>(std::lround(hz * size_ / sampleRate_)), 1, half); };
    for (int b = 0; b <= kAnalysisBands; ++b) edges_[static_cast<std::size_t>(b)] = bin(kBandEdgesHz[b]);
    flatFrom_ = bin(20.0);
    flatTo_ = bin(std::min(16000.0, 0.45 * sampleRate_));
    reset();
}

void ElementClassifier::reset() {
    std::fill(mono_.begin(), mono_.end(), 0.0);
    std::fill(side_.begin(), side_.end(), 0.0);
    filled_ = 0;
    sinceHop_ = 0;
    pos_ = 0;
    frames_.clear();
    soundFrames_ = 0;
}

double ElementClassifier::secondsHeard() const noexcept { return static_cast<double>(soundFrames_) * hop_ / sampleRate_; }

void ElementClassifier::push(const double* left, const double* right, int numFrames) {
    for (int i = 0; i < numFrames; ++i) {
        // Tampon circulaire : les size_ derniers échantillons, pos_ = le plus ancien.
        mono_[static_cast<std::size_t>(pos_)] = 0.5 * (left[i] + right[i]);
        side_[static_cast<std::size_t>(pos_)] = 0.5 * (left[i] - right[i]);
        if (++pos_ == size_) pos_ = 0;
        if (filled_ < size_) ++filled_;
        if (++sinceHop_ >= hop_ && filled_ == size_) {
            analyseFrame();
            sinceHop_ = 0;
        }
    }
}

void ElementClassifier::analyseFrame() {
    Frame f;
    // Énergies du dernier pas (temporel) : enveloppe, crête, largeur.
    double mid = 0.0, side = 0.0, peak = 0.0;
    const auto at = [&](const std::vector<double>& ring, int i) { return ring[static_cast<std::size_t>((pos_ + i) % size_)]; };
    for (int i = size_ - hop_; i < size_; ++i) {
        const double m = at(mono_, i), s = at(side_, i);
        mid += m * m;
        side += s * s;
        peak = std::max({peak, std::fabs(m + s), std::fabs(m - s)});
    }
    f.mid = mid;
    f.side = side;
    f.peak = peak;
    f.meanSquare = (mid + side) / hop_; // (L² + R²) / 2 en moyenne
    // Spectre du Mid sur la fenêtre complète.
    for (int i = 0; i < size_; ++i) work_[static_cast<std::size_t>(i)] = {at(mono_, i) * window_[static_cast<std::size_t>(i)], 0.0};
    fft_.forward(work_.data());
    const int half = size_ / 2;
    power_.resize(static_cast<std::size_t>(half + 1));
    double weighted = 0.0, sum = 0.0;
    for (int k = 1; k <= half; ++k) {
        const double p = std::norm(work_[static_cast<std::size_t>(k)]);
        power_[static_cast<std::size_t>(k)] = p;
        for (int b = 0; b < kAnalysisBands; ++b)
            if (k >= edges_[static_cast<std::size_t>(b)] && k < edges_[static_cast<std::size_t>(b + 1)]) f.band[static_cast<std::size_t>(b)] += p;
        weighted += p * k * sampleRate_ / size_;
        sum += p;
    }
    for (double b : f.band) f.total += b;
    f.centroid = sum > 0.0 ? weighted / sum : 0.0;
    // Bruit ou note : part de l'énergie dans des pics (bins à plus de 2x la
    // moyenne des voisins, lobe principal de ±2 bins exclu). Une note y met
    // presque tout (sinus : 1.00, accord : 0.82), un bruit, même filtré en bande
    // étroite, environ 0.4 (loi exponentielle des bins : 3/e² = 0.41).
    double peakEnergy = 0.0, rangeEnergy = 0.0;
    constexpr int kSpan = 8, kLobe = 2;
    for (int k = flatFrom_; k < flatTo_; ++k) {
        double local = 0.0;
        int count = 0;
        for (int j = k - kSpan; j <= k + kSpan; ++j) {
            if (std::abs(j - k) <= kLobe || j < 1 || j > half) continue;
            local += power_[static_cast<std::size_t>(j)];
            ++count;
        }
        local /= std::max(1, count);
        const double p = power_[static_cast<std::size_t>(k)];
        rangeEnergy += p;
        if (p > 2.0 * local) peakEnergy += p;
    }
    f.flatness = rangeEnergy > kTiny ? std::clamp((1.0 - peakEnergy / rangeEnergy) / 0.6, 0.0, 1.0) : 0.0;
    if (toDb(f.meanSquare) > kSilenceDb) ++soundFrames_;
    frames_.push_back(f);
    if (frames_.size() > kMaxFrames) frames_.pop_front();
}

Character ElementClassifier::measure(std::vector<double>& attackLow, std::vector<double>& attackHigh) const {
    Character c;
    const std::size_t n = frames_.size();
    if (n < 4) return c;
    // Enveloppe : énergie de la fenêtre entière (~46 ms), stable même pour un
    // sub de 40 Hz dont une période dépasse le pas de 12 ms.
    std::vector<double> e(n);
    std::vector<double> audible;
    double meanTotal = 0.0;
    std::size_t sounding = 0;
    for (std::size_t t = 0; t < n; ++t) {
        const bool sound = toDb(frames_[t].meanSquare) > kSilenceDb;
        e[t] = sound ? toDb(frames_[t].total) : -400.0;
        if (sound) {
            audible.push_back(e[t]);
            meanTotal += frames_[t].total;
            ++sounding;
        }
    }
    // Plancher du flux : 50 dB sous l'énergie moyenne (les bandes presque vides
    // ne fabriquent pas de faux coups).
    const double floor = 1e-5 * meanTotal / static_cast<double>(std::max<std::size_t>(1, sounding));
    if (audible.empty()) return c;
    std::sort(audible.begin(), audible.end());
    const double ref = audible[static_cast<std::size_t>(0.95 * static_cast<double>(audible.size() - 1))];
    const auto active = [&](std::size_t t) { return e[t] > ref - 40.0; };

    // ---- Coups : montée de 8 dB sur ~35 ms, ou changement spectral franc.
    std::vector<std::size_t> onsets;
    // Après un coup, il faut que le son retombe de 3 dB avant le suivant : une
    // montée lente (syllabe, nappe qui s'ouvre) ne compte qu'une fois.
    double peakSince = -400.0;
    bool rearmed = true;
    for (std::size_t t = 3; t < n; ++t) {
        peakSince = std::max(peakSince, e[t]);
        if (e[t] < peakSince - 3.0) rearmed = true;
        if (!active(t) || e[t] < ref - 30.0) continue;
        // Un coup : l'énergie monte de 8 dB, ou une bande qui pèse (5 % de la
        // trame) monte de 12 dB d'un coup (un hat sur la queue d'un kick).
        const double before = std::min({e[t - 1], e[t - 2], e[t - 3]});
        bool bandHit = false;
        for (int b = 0; b < kAnalysisBands; ++b) {
            const auto bb = static_cast<std::size_t>(b);
            const double now = frames_[t].band[bb], prev = std::min(frames_[t - 1].band[bb], frames_[t - 2].band[bb]);
            bandHit = bandHit || (toDb(now + floor) - toDb(prev + floor) >= 12.0 && now >= 0.05 * frames_[t].total);
        }
        const bool hit = e[t] - before >= 8.0 || bandHit;
        if (hit && rearmed && (onsets.empty() || t - onsets.back() >= 4)) {
            onsets.push_back(t);
            rearmed = false;
            peakSince = e[t];
        }
    }

    // ---- Chaque coup : attaque, corps, décroissance de 20 dB, crête / RMS.
    const double frameMs = 1000.0 * hop_ / sampleRate_;
    const std::size_t maxLength = static_cast<std::size_t>(1500.0 / frameMs);
    std::vector<double> decays, crests, attacks;
    for (std::size_t i = 0; i < onsets.size(); ++i) {
        const std::size_t o = onsets[i];
        const std::size_t end = std::min({n, i + 1 < onsets.size() ? onsets[i + 1] : n, o + maxLength});
        std::size_t p = o;
        for (std::size_t t = o; t < std::min(end, o + 4); ++t)
            if (e[t] > e[p]) p = t;
        // Temps de montée : du coup au maximum de l'événement (dans les 300 ms).
        std::size_t top = o;
        for (std::size_t k = o; k < std::min(end, o + static_cast<std::size_t>(300.0 / frameMs)); ++k)
            if (e[k] > e[top]) top = k;
        attacks.push_back(static_cast<double>(top - o) * frameMs);
        std::size_t t = p + 1;
        while (t < end && e[t] > e[p] - 20.0) ++t;
        decays.push_back(static_cast<double>(t - p) * frameMs);
        // Attaque : ce que le coup AJOUTE au son d'avant (dans une boucle, le hat
        // arrive sur la queue du kick).
        std::array<double, kAnalysisBands> attack{};
        double attackSum = 0.0, peak = 0.0, ms = 0.0;
        const Frame& prior = frames_[o > 0 ? o - 1 : o];
        for (std::size_t k = o; k < std::min(end, o + 3); ++k)
            for (int b = 0; b < kAnalysisBands; ++b) {
                const auto bb = static_cast<std::size_t>(b);
                const double added = std::max(0.0, frames_[k].band[bb] - (o > 0 ? prior.band[bb] : 0.0));
                attack[bb] += added;
                attackSum += added;
            }
        for (std::size_t k = o; k < end; ++k) {
            peak = std::max(peak, frames_[k].peak);
            ms += frames_[k].meanSquare;
        }
        ms /= static_cast<double>(std::max<std::size_t>(1, end - o));
        if (ms > kTiny) crests.push_back(20.0 * std::log10(peak / std::sqrt(ms) + kTiny));
        attackLow.push_back(attackSum > 0.0 ? (attack[0] + attack[1]) / attackSum : 0.0);
        attackHigh.push_back(attackSum > 0.0 ? (attack[4] + attack[5]) / attackSum : 0.0);
    }

    // ---- Tout le son actif : répartition, planéité, largeur, éclat.
    double total = 0.0, mid = 0.0, side = 0.0, centroid = 0.0, peak = 0.0, ms = 0.0;
    std::size_t activeCount = 0;
    std::vector<double> flats;
    for (std::size_t t = 0; t < n; ++t) {
        if (!active(t)) continue;
        const Frame& f = frames_[t];
        for (int b = 0; b < kAnalysisBands; ++b) c.bandShare[static_cast<std::size_t>(b)] += f.band[static_cast<std::size_t>(b)];
        total += f.total;
        mid += f.mid;
        side += f.side;
        centroid += f.centroid * f.total;
        flats.push_back(f.flatness);
        peak = std::max(peak, f.peak);
        ms += f.meanSquare;
        ++activeCount;
    }
    if (total > 0.0)
        for (double& s : c.bandShare) s /= total;
    c.brightnessHz = total > 0.0 ? centroid / total : 0.0;
    c.noisiness = median(flats);
    c.sideToMidDb = toDb(side) - toDb(mid);
    const double activeSeconds = static_cast<double>(activeCount) * frameMs / 1000.0;
    c.onsetsPerSecond = activeSeconds > 0.0 ? static_cast<double>(onsets.size()) / activeSeconds : 0.0;
    // Sans coup (nappe), la « décroissance » est la durée de son tenu.
    c.decayMs = decays.empty() ? activeSeconds * 1000.0 : median(decays);
    c.attackMs = attacks.empty() ? 0.0 : median(attacks);
    ms /= static_cast<double>(std::max<std::size_t>(1, activeCount));
    c.crestDb = !crests.empty() ? median(crests) : (ms > kTiny ? 20.0 * std::log10(peak / std::sqrt(ms) + kTiny) : 0.0);
    return c;
}

Identification ElementClassifier::identify(Element manual, std::string_view trackName) const {
    const NameHint hint = elementFromName(trackName);
    const double seconds = secondsHeard();
    std::array<double, kElementCount> s{};
    Character c;
    if (seconds >= kMinSeconds) {
        std::vector<double> attackLow, attackHigh;
        c = measure(attackLow, attackHigh);
        const auto& b = c.bandShare;
        const double low = b[0] + b[1], sub = b[0], lowMid = b[2], midBand = b[3], pres = b[4], air = b[5], high = pres + air;
        const double subOfLow = low > 1e-9 ? sub / low : 0.0;
        const double bodyRatio = lowMid / (midBand + pres + 1e-9);
        const double F = c.noisiness, D = c.decayMs, R = c.onsetsPerSecond, W = c.sideToMidDb;
        const double eLow = median(attackLow);
        const double kickHits = fractionAbove(attackLow, 0.5), hatHits = fractionAbove(attackHigh, 0.55);
        const auto set = [&](Element e, double v) { s[static_cast<std::size_t>(e)] = v; };
        // Planéité (0 note … 1 bruit) : une note est sous 0.4, un bruit au-dessus de 0.75.
        const double tonal = down(F, 0.4, 0.65), noisy = up(F, 0.5, 0.75);
        // Drums
        set(Element::Kick, up(R, 0.15, 0.5) * up(eLow, 0.35, 0.6) * down(D, 450, 900) * down(hatHits, 0.25, 0.5) * down(high, 0.25, 0.4));
        set(Element::Snare, up(R, 0.15, 0.5) * noisy * up(lowMid + midBand + pres, 0.55, 0.75) * up(bodyRatio, 0.12, 0.3) *
                                down(D, 450, 800) * down(low, 0.3, 0.45));
        set(Element::Clap, up(R, 0.15, 0.5) * up(F, 0.55, 0.8) * up(midBand + pres, 0.5, 0.7) * down(bodyRatio, 0.1, 0.25) *
                               down(D, 450, 800) * down(air, 0.35, 0.5));
        set(Element::HiHat, up(high, 0.55, 0.8) * down(D, 110, 220) * up(R, 0.3, 1.0));
        set(Element::OpenHat, up(high, 0.5, 0.75) * up(D, 140, 260) * down(D, 900, 1400) * up(R, 0.1, 0.4));
        set(Element::Cymbal, up(high, 0.45, 0.7) * up(D, 800, 1300));
        // Attaque : une percussion atteint son maximum en moins de 30 ms, une syllabe en 50 à 150 ms.
        const double A = c.attackMs;
        set(Element::Perc, up(R, 0.3, 0.8) * up(lowMid + midBand + pres, 0.55, 0.8) * down(D, 200, 400) * down(low, 0.25, 0.4) *
                               down(high, 0.5, 0.7) * down(A, 30.0, 60.0));
        set(Element::DrumLoop, up(kickHits, 0.08, 0.25) * up(hatHits, 0.1, 0.3) * up(R, 1.5, 3.0));
        // Basses : du vrai grave, tenu
        set(Element::Bass808, up(low, 0.45, 0.7) * up(D, 300, 700) * tonal * up(subOfLow, 0.3, 0.5));
        set(Element::Bass, up(low, 0.25, 0.45) * up(low + lowMid, 0.5, 0.75) * down(subOfLow, 0.3, 0.5) * tonal * up(D, 200, 500));
        // Instruments (familles proches : le nom les départage)
        set(Element::Keys, tonal * up(R, 0.2, 0.6) * up(lowMid + midBand, 0.4, 0.65) * up(D, 200, 450) * down(low, 0.3, 0.45));
        set(Element::Pad, tonal * down(R, 0.25, 0.7) * down(low, 0.3, 0.45) * up(lowMid + midBand, 0.4, 0.6));
        set(Element::Lead, 0.8 * tonal * up(midBand + pres, 0.45, 0.7) * down(low, 0.25, 0.4) * up(D, 150, 400));
        set(Element::Pluck, tonal * up(R, 0.5, 1.5) * down(D, 200, 400) * up(midBand + pres, 0.4, 0.65) * down(low, 0.25, 0.4));
        // Voix : médium dominant, entre note et bruit (souffle, consonnes), syllabes, plutôt au centre
        const double voice = up(midBand, 0.25, 0.45) * band(F, 0.2, 0.3, 0.65, 0.8) * band(R, 0.8, 1.5, 8.0, 14.0) * down(low, 0.2, 0.35) *
                             up(A, 15.0, 40.0);
        set(Element::Vocal, voice * down(W, -14.0, -8.0));
        set(Element::Backs, voice * up(W, -14.0, -8.0));
        set(Element::Fx, up(F, 0.6, 0.8) * down(R, 0.2, 0.6));
    }
    Identification id = combine(s, hint, manual, seconds);
    id.character = c;
    return id;
}

Identification combine(const std::array<double, kElementCount>& scores, const NameHint& name, Element manual, double seconds) {
    Identification id;
    id.scores = scores;
    id.name = name;
    id.secondsHeard = seconds;
    // Ce que dit l'écoute : le meilleur score, s'il est assez net.
    int best = -1, second = -1;
    for (int e = 1; e < kElementCount; ++e) {
        if (best < 0 || scores[static_cast<std::size_t>(e)] > scores[static_cast<std::size_t>(best)]) {
            second = best;
            best = e;
        } else if (second < 0 || scores[static_cast<std::size_t>(e)] > scores[static_cast<std::size_t>(second)]) {
            second = e;
        }
    }
    const double top = best >= 0 ? scores[static_cast<std::size_t>(best)] : 0.0;
    const double next = second >= 0 ? scores[static_cast<std::size_t>(second)] : 0.0;
    if (seconds >= ElementClassifier::kMinSeconds && top >= 0.15) {
        id.heard = static_cast<Element>(best);
        id.confidence = (top - next) / top;
    }
    if (manual != Element::Auto) {
        id.element = manual;
        id.manual = true;
        return id;
    }
    if (id.heard == Element::Auto) {
        id.element = name.element; // rien d'entendu : le nom (ou Auto)
        return id;
    }
    if (name.element == Element::Auto) {
        id.element = id.heard;
        return id;
    }
    const Family heardFamily = familyOf(id.heard), nameFamily = familyOf(name.element);
    if (heardFamily == nameFamily) {
        // Même famille : le nom précise (« guitare » plutôt que « piano »), sauf
        // pour les drums et les basses, où l'écoute sait mieux (open hat ou hat
        // fermé, 808 ou basse) quand elle est nette.
        const bool audioKnowsBetter = (heardFamily == Family::Drums || heardFamily == Family::Bass) && id.confidence >= 0.3;
        id.element = audioKnowsBetter ? id.heard : name.element;
        return id;
    }
    // Familles différentes : une écoute nette l'emporte, et on le signale.
    if (id.confidence >= 0.4) {
        id.element = id.heard;
        id.nameDisagrees = true;
    } else {
        id.element = name.element;
    }
    return id;
}

} // namespace omnis::stems
