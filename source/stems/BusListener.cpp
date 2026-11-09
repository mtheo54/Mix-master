// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/BusListener.cpp
// =============================================================================
#include "BusListener.h"

#include "core/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace omnis::stems {

namespace {

constexpr int kTimeMedian = 21; ///< ~230 ms : un coup de kick (< 200 ms) n'y tient pas, une note de 808 si
constexpr int kFreqMedian = 17;
constexpr double kBandEdges[BusListener::kBands + 1] = {30.0, 120.0, 300.0, 4000.0, 16000.0};
constexpr double kTiny = 1e-20;
constexpr double kSilenceDb = -60.0; ///< sous −60 dBFS : du souffle, pas de la musique
constexpr double kMargin = 2.0;      ///< séparation : tenu si 2 fois plus « temps » que « fréquence », et l'inverse
constexpr std::size_t kMaxFrames = 131072; ///< ~23 min à ~11 ms : le morceau entier (52 octets par trame)

double toDb(double p) { return 10.0 * std::log10(p + kTiny); }

double percentile(std::vector<double> v, double q) {
    if (v.empty()) return -400.0;
    const auto k = static_cast<std::ptrdiff_t>(q * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[static_cast<std::size_t>(k)];
}

/// Plages consécutives où `on(t)` tient (trous de `gap` trames tolérés) : (début, fin exclue).
template <typename On>
std::vector<std::pair<std::size_t, std::size_t>> runs(std::size_t n, std::size_t gap, On&& on) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    std::size_t start = 0, last = 0;
    bool open = false;
    for (std::size_t t = 0; t < n; ++t) {
        if (!on(t)) continue;
        if (open && t - last > gap + 1) {
            out.emplace_back(start, last + 1);
            open = false;
        }
        if (!open) start = t, open = true;
        last = t;
    }
    if (open) out.emplace_back(start, last + 1);
    return out;
}

} // namespace

bool BusContent::has(Element e) const noexcept {
    return std::any_of(items.begin(), items.end(), [e](const HeardItem& i) { return i.element == e; });
}

Family BusContent::mainFamily() const noexcept {
    double weight[kFamilyCount]{};
    for (const auto& i : items) weight[static_cast<int>(familyOf(i.element))] += i.confidence;
    int best = 0;
    for (int f = 1; f < kFamilyCount; ++f)
        if (weight[f] > weight[best]) best = f;
    return weight[best] > 0.0 ? static_cast<Family>(best) : Family::Unknown;
}

std::string describeItem(const HeardItem& i) {
    char b[96];
    const std::string label = i18n::tr(i.element == Element::Snare ? "Snare / clap" : elementLabel(i.element));
    if (i.element == Element::Bass808 || i.element == Element::Pad)
        std::snprintf(b, sizeof b, i18n::tr("%s (tenue %.0f s)").c_str(), label.c_str(), i.seconds);
    else if (i.count > 0)
        std::snprintf(b, sizeof b, "%s (%d)", label.c_str(), i.count);
    else
        std::snprintf(b, sizeof b, "%s", label.c_str());
    return b;
}

void BusListener::prepare(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    // ~43 ms à 44,1 / 48 kHz, la même durée aux taux plus élevés ; pas d'un quart.
    size_ = sampleRate_ <= 50000.0 ? 2048 : sampleRate_ <= 100000.0 ? 4096 : sampleRate_ <= 200000.0 ? 8192 : 16384;
    hop_ = size_ / 4;
    fft_.prepare(size_);
    window_.resize(static_cast<std::size_t>(size_));
    for (int i = 0; i < size_; ++i) window_[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * (i + 0.5) / size_);
    mono_.assign(static_cast<std::size_t>(size_), 0.0);
    work_.assign(static_cast<std::size_t>(size_), {});
    bins_ = std::min(size_ / 2, static_cast<int>(std::ceil(kBandEdges[kBands] * size_ / sampleRate_)) + kFreqMedian);
    bandOf_.assign(static_cast<std::size_t>(bins_), -1);
    for (int k = 1; k < bins_; ++k) {
        const double hz = k * sampleRate_ / size_;
        for (int b = 0; b < kBands; ++b)
            if (hz >= kBandEdges[b] && hz < kBandEdges[b + 1]) bandOf_[static_cast<std::size_t>(k)] = b;
    }
    drums_.prepare(sampleRate_);
    reset();
}

void BusListener::reset() {
    std::fill(mono_.begin(), mono_.end(), 0.0);
    filled_ = sinceHop_ = pos_ = 0;
    history_.clear();
    frames_.clear();
    soundFrames_ = 0;
    drums_.reset();
}

double BusListener::secondsHeard() const noexcept { return static_cast<double>(soundFrames_) * hop_ / sampleRate_; }

void BusListener::push(const double* left, const double* right, int numFrames) {
    drums_.push(left, right, numFrames);
    for (int i = 0; i < numFrames; ++i) {
        double x = 0.5 * (left[i] + right[i]);
        if (!std::isfinite(x)) x = 0.0;
        mono_[static_cast<std::size_t>(pos_)] = x;
        if (++pos_ == size_) pos_ = 0;
        if (filled_ < size_) ++filled_;
        if (++sinceHop_ >= hop_ && filled_ == size_) {
            analyse();
            sinceHop_ = 0;
        }
    }
}

void BusListener::analyse() {
    // Spectre de puissance de la fenêtre (pos_ = le plus ancien échantillon).
    for (int i = 0; i < size_; ++i)
        work_[static_cast<std::size_t>(i)] = {mono_[static_cast<std::size_t>((pos_ + i) % size_)] * window_[static_cast<std::size_t>(i)], 0.0};
    fft_.forward(work_.data());
    std::vector<float> power(static_cast<std::size_t>(bins_));
    const double scale = 4.0 / (static_cast<double>(size_) * size_);
    for (int k = 0; k < bins_; ++k) power[static_cast<std::size_t>(k)] = static_cast<float>(std::norm(work_[static_cast<std::size_t>(k)]) * scale);
    history_.push_back(std::move(power));
    if (history_.size() > static_cast<std::size_t>(kTimeMedian)) history_.pop_front();
    if (history_.size() < static_cast<std::size_t>(kTimeMedian)) return;

    // La trame du milieu : chaque bin est « tenu » si sa médiane dans le temps dépasse sa médiane
    // dans les fréquences (masque binaire de la séparation harmonique / percussive).
    const auto& centre = history_[kTimeMedian / 2];
    Frame f;
    float column[kTimeMedian], row[kFreqMedian];
    for (int k = 1; k < bins_; ++k) {
        const int band = bandOf_[static_cast<std::size_t>(k)];
        if (band < 0) continue;
        for (int j = 0; j < kTimeMedian; ++j) column[j] = history_[static_cast<std::size_t>(j)][static_cast<std::size_t>(k)];
        std::nth_element(column, column + kTimeMedian / 2, column + kTimeMedian);
        int count = 0;
        for (int d = -kFreqMedian / 2; d <= kFreqMedian / 2; ++d) {
            const int q = k + d;
            if (q >= 1 && q < bins_) row[count++] = centre[static_cast<std::size_t>(q)];
        }
        std::nth_element(row, row + count / 2, row + count);
        const double e = centre[static_cast<std::size_t>(k)], ht = column[kTimeMedian / 2], pf = row[count / 2];
        (ht >= kMargin * pf ? f.h : pf >= kMargin * ht ? f.p : f.r)[band] += static_cast<float>(e);
        f.total += static_cast<float>(e);
    }
    if (toDb(f.total) > kSilenceDb) ++soundFrames_;
    frames_.push_back(f);
    if (frames_.size() > kMaxFrames) frames_.pop_front();
}

BusContent BusListener::content() const {
    BusContent c;
    c.secondsHeard = secondsHeard();
    if (c.secondsHeard < kMinSeconds) return c;
    const std::size_t n = frames_.size();
    const double frameMs = 1000.0 * hop_ / sampleRate_;
    std::vector<double> totals;
    totals.reserve(n);
    for (const auto& f : frames_)
        if (toDb(f.total) > kSilenceDb) totals.push_back(toDb(f.total));
    const double ref = percentile(totals, 0.95);
    if (ref < kSilenceDb + 10.0) return c; // rien d'assez fort pour être de la musique
    const auto active = [&](std::size_t t) { return toDb(frames_[t].total) > std::max(ref - 40.0, kSilenceDb); };
    std::size_t activeFrames = 0;
    for (std::size_t t = 0; t < n; ++t) activeFrames += active(t);
    if (activeFrames == 0) return c;
    const auto at = [&](std::size_t t) -> const Frame& { return frames_[t]; };
    const auto low = [&](std::size_t t) { const auto& f = at(t); return f.h[kSub] + f.p[kSub] + f.r[kSub] + f.h[kLow] + f.p[kLow] + f.r[kLow]; };
    const auto ms = [&](std::size_t frames) { return static_cast<double>(frames) * frameMs; };

    // ---- 808 / basse : du grave harmonique, fort, qui tient à 12 dB près de son sommet au moins 250 ms
    // par note (un kick, même grave et tonal, retombe en moins de 150 ms).
    bool has808 = false;
    {
        const auto notes = runs(n, 2, [&](std::size_t t) { const auto& f = at(t); return toDb(f.h[kSub]) > ref - 22.0 && f.h[kSub] >= 0.25 * f.total; });
        int count = 0;
        double held = 0.0;
        for (const auto& [a, b] : notes) {
            double peak = 0.0;
            for (std::size_t t = a; t < b; ++t) peak = std::max(peak, static_cast<double>(at(t).h[kSub]));
            std::size_t near = 0;
            for (std::size_t t = a; t < b; ++t) near += toDb(at(t).h[kSub]) >= toDb(peak) - 12.0;
            if (ms(near) >= 250.0) {
                ++count;
                held += ms(near) / 1000.0;
            }
        }
        if (count >= 3 && held >= 2.5) {
            c.items.push_back({Element::Bass808, std::clamp(held / 4.0, 0.3, 1.0), count, held});
            has808 = true;
        }
    }

    // ---- DRUMS : le détecteur de coups du bus de drums, confirmé par la forme des attaques (sinon
    // l'attaque d'une 808 ferait un kick, celle d'un lead une snare).
    {
        int fastKicks = 0, clickKicks = 0, snareHits = 0, hatHits = 0;
        std::size_t lastLow = 0, lastMid = 0, lastHigh = 0;
        for (std::size_t t = 3; t + 1 < n; ++t) {
            const double l = toDb(low(t));
            if (l - std::min({toDb(low(t - 1)), toDb(low(t - 2)), toDb(low(t - 3))}) >= 8.0 && l > ref - 20.0 && (lastLow == 0 || ms(t - lastLow) >= 80.0)) {
                lastLow = t;
                std::size_t p = t;
                for (std::size_t k = t; k < std::min(n, t + 4); ++k)
                    if (low(k) > low(p)) p = k;
                bool fast = false;
                for (std::size_t k = p; k < std::min(n, p + static_cast<std::size_t>(200.0 / frameMs)) && !fast; ++k) fast = toDb(low(k)) <= toDb(low(p)) - 12.0;
                // Le clic de la batte : le bruit du médium et de l'aigu saute en même temps que le grave.
                const auto noise = [&](std::size_t k) { const auto& f = at(k); return f.p[kMid] + f.r[kMid] + f.p[kHigh] + f.r[kHigh]; };
                // (assez fort pour s'entendre : le petit clic d'une 808 qui démarre n'en est pas un).
                double jump = -400.0;
                for (std::size_t k = std::max<std::size_t>(t - 1, 3); k <= std::min(n - 1, t + 1); ++k)
                    if (noise(k) >= 0.02 * at(k).total && toDb(noise(k)) > ref - 30.0)
                        jump = std::max(jump, toDb(noise(k)) - std::min({toDb(noise(k - 1)), toDb(noise(k - 2)), toDb(noise(k - 3))}));
                // Un kick a son énergie dans le sub (40–100 Hz) ; l'attaque d'une note grave du médium, non.
                const auto& fp = at(p);
                const bool subby = toDb(fp.h[kSub] + fp.p[kSub] + fp.r[kSub]) >= toDb(fp.h[kLow] + fp.p[kLow] + fp.r[kLow]) - 3.0;
                fast = fast && subby;
                fastKicks += fast;
                clickKicks += subby && jump >= 8.0;
            }
            const auto& f = at(t);
            // Un coup de hat : l'aigu bruité saute de 8 dB (un riser, qui monte doucement, n'en fait pas).
            const auto hn = [&](std::size_t k) { return at(k).p[kHigh] + at(k).r[kHigh]; };
            if (toDb(hn(t)) - std::min({toDb(hn(t - 1)), toDb(hn(t - 2)), toDb(hn(t - 3))}) >= 8.0 && toDb(hn(t)) > ref - 35.0 &&
                (lastHigh == 0 || ms(t - lastHigh) >= 60.0)) {
                lastHigh = t;
                ++hatHits;
            }
            const double m = f.p[kMid] + f.r[kMid], mPrev = std::min({at(t - 1).p[kMid] + at(t - 1).r[kMid], at(t - 2).p[kMid] + at(t - 2).r[kMid]});
            // Un coup de snare / clap : le médium bruité saute de 10 dB, est fort, et c'est surtout du bruit
            // (pas l'attaque d'une note, qui reste « tenue »).
            // Et le bruit tient (≥ 3 des 5 trames suivantes) : l'attaque d'une note pincée, elle, redevient « tenue ».
            int noisy = 0;
            for (std::size_t k = t + 1; k < std::min(n, t + 6); ++k) noisy += at(k).p[kMid] + at(k).r[kMid] >= 0.5 * at(k).h[kMid];
            // Une snare, un clap : un bruit LARGE, qui saute aussi dans l'aigu (au-dessus de 4 kHz) et y pèse
            // (une note pincée du mélo a un peu de souffle à l'attaque, mais presque pas d'aigu).
            const auto hiNoise = [&](std::size_t k) { return at(k).p[kHigh] + at(k).r[kHigh]; };
            const bool broad = hiNoise(t) >= 0.3 * m && toDb(hiNoise(t)) - std::min(toDb(hiNoise(t - 1)), toDb(hiNoise(t - 2))) >= 6.0;
            if (toDb(m) - toDb(mPrev) >= 10.0 && m >= 0.5 * f.h[kMid] && noisy >= 3 && broad && m >= 0.05 * f.total && toDb(m) > ref - 25.0 &&
                (lastMid == 0 || ms(t - lastMid) >= 80.0)) {
                lastMid = t;
                ++snareHits;
            }
        }
        // Assez de coups pour un vrai élément de drums : au moins 4, et au moins un toutes les ~7 s d'écoute
        // (un toutes les 20 s pour un open hat) ; quelques attaques dans un bus de mélo n'en font pas.
        const double activeSeconds = ms(activeFrames) / 1000.0;
        const auto enough = [&](int hits, double perSecond) { return hits >= std::max(4, static_cast<int>(std::ceil(perSecond * activeSeconds))); };
        const DrumContent& d = drums_.content();
        const auto drum = [&](const DrumContent::Item& i, Element e, bool confirmed, double perSecond) {
            if (i.present && confirmed && enough(i.hits, perSecond)) c.items.push_back({e, std::clamp(i.hits / 16.0, 0.3, 1.0), i.hits, 0.0});
        };
        drum(d.kick, Element::Kick, enough(fastKicks, 0.15) || (has808 && enough(clickKicks, 0.15)), 0.15);
        drum(d.snare, Element::Snare, enough(snareHits, 0.15), 0.15);
        drum(d.hat, Element::HiHat, enough(hatHits, 0.15), 0.15);
        drum(d.openHat, Element::OpenHat, enough(hatHits, 0.15), 0.05);
    }

    // ---- MÉLO : de l'harmonique dans le médium, une fois retiré ce qui suit le grave (les harmoniques
    // d'une 808 saturée montent et descendent avec elle : droite des moindres carrés médium ~ grave).
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0, m = 0;
        for (std::size_t t = 0; t < n; ++t) {
            if (!active(t)) continue;
            const double x = at(t).h[kSub], y = at(t).h[kMid];
            sx += x, sy += y, sxx += x * x, sxy += x * y, m += 1.0;
        }
        const double var = sxx - sx * sx / m;
        const double slope = var > 0.0 ? std::max(0.0, (sxy - sx * sy / m) / var) : 0.0;
        std::size_t melodic = 0;
        std::vector<double> levels;
        int onsets = 0;
        double prev[3] = {-400.0, -400.0, -400.0};
        for (std::size_t t = 0; t < n; ++t) {
            const auto& f = at(t);
            const double own = std::max(0.0, f.h[kMid] - slope * f.h[kSub]);
            const double hm = toDb(own);
            const bool on = active(t) && hm > ref - 30.0 && own >= 0.3 * (f.h[kMid] + f.p[kMid] + f.r[kMid]);
            if (on) {
                ++melodic;
                // Une attaque : le médium tenu monte de 6 dB en moins de ~35 ms.
                if (hm - std::min({prev[0], prev[1], prev[2]}) >= 6.0) ++onsets;
            }
            if (active(t)) levels.push_back(on ? hm : -400.0);
            prev[t % 3] = hm;
        }
        const double share = static_cast<double>(melodic) / static_cast<double>(activeFrames);
        if (share >= 0.25) {
            const double activeSeconds = ms(activeFrames) / 1000.0;
            const double rate = onsets / std::max(1.0, activeSeconds);
            const double heldSeconds = ms(melodic) / 1000.0;
            // Une nappe tient presque tout le temps (son 20e centile reste fort) ; un lead attaque ses notes.
            const bool sustained = percentile(levels, 0.2) > ref - 32.0;
            const double conf = std::clamp(share, 0.3, 1.0);
            if (rate >= 0.7) c.items.push_back({Element::Lead, conf, onsets, heldSeconds});
            if (sustained || rate < 0.7) c.items.push_back({Element::Pad, conf, 0, heldSeconds});
        }
    }

    // ---- EFX : les montées de l'aigu (moyenne sur ~250 ms : les hats n'en font pas), les impacts.
    {
        std::vector<double> high(n), raw(n);
        for (std::size_t t = 0; t < n; ++t) raw[t] = at(t).h[kHigh] + at(t).p[kHigh] + at(t).r[kHigh];
        double sum = 0.0;
        constexpr std::size_t kSmooth = 24; // ~250 ms : les hats et les coups de snare s'y fondent
        for (std::size_t t = 0; t < n; ++t) {
            sum += raw[t];
            if (t >= kSmooth) sum -= raw[t - kSmooth];
            high[t] = std::max(toDb(sum / static_cast<double>(std::min(t + 1, kSmooth))), kSilenceDb - 20.0);
        }
        std::vector<double> highLevels;
        for (std::size_t t = 0; t < n; ++t)
            if (active(t)) highLevels.push_back(high[t]);
        const double refHigh = percentile(highLevels, 0.98);
        const std::size_t back = static_cast<std::size_t>(6000.0 / frameMs);
        int reverse = 0, riser = 0;
        for (std::size_t t = 0; t + 1 < n;) {
            std::size_t end = t, top = t;
            while (end + 1 < n && high[end + 1] >= high[top] - 1.5) {
                ++end;
                if (high[end] >= high[top]) top = end;
            }
            if (top > t && high[top] > refHigh - 12.0) {
                // Le plancher des 6 s d'avant (les hats, le silence), puis le pied de la montée : là où
                // elle était 20 dB plus bas (ou au plancher), et le premier passage à 1,5 dB du sommet.
                const std::size_t from = top > back ? top - back : 0;
                double floor = high[top];
                for (std::size_t k = from; k < top; ++k) floor = std::min(floor, high[k]);
                std::size_t reach = top;
                while (reach > from && high[reach - 1] >= high[top] - 1.5) --reach;
                // En remontant le temps, la courbe doit vraiment descendre : au moins 0,3 dB par ~100 ms
                // (un plateau de hats n'est pas une montée), jusqu'à 20 dB sous le sommet au plus.
                const std::size_t step = std::max<std::size_t>(1, static_cast<std::size_t>(100.0 / frameMs));
                std::size_t foot = reach;
                while (foot >= from + step && high[foot - step] <= high[foot] - 0.3 && high[foot - step] > high[top] - 20.0) foot -= step;
                const double rise = high[top] - high[foot], length = ms(reach - foot);
                // Une reverse cymbal, un riser : un souffle continu. Une rentrée de hats, un roll, un
                // roulement de snare avant un break : des coups, avec des creux entre eux. Dans la
                // dernière partie de la montée (là où elle domine), l'aigu brut ne doit presque jamais
                // retomber de 6 dB sous sa moyenne.
                std::size_t dips = 0, seen = 0;
                for (std::size_t k = foot + (top - foot) * 2 / 5; k <= top; ++k) {
                    ++seen;
                    dips += toDb(raw[k]) < high[k] - 6.0;
                }
                const bool smooth = seen > 0 && static_cast<double>(dips) <= 0.2 * static_cast<double>(seen);
                // Et elle pèse dans le bus (l'aigu d'une 808 qui glisse ne fait pas un riser).
                if (rise >= 8.0 && length >= 450.0 && smooth && high[top] > ref - 30.0) { // (le lissage seul fait ~250 ms à une attaque franche)
                    // Coupée net : l'aigu (non lissé) perd 10 dB dans les ~120 ms après le sommet.
                    double peak = 0.0;
                    for (std::size_t k = top >= kSmooth ? top - kSmooth : 0; k <= top; ++k) peak = std::max(peak, raw[k]);
                    bool abrupt = false;
                    for (std::size_t k = top + 1; k < std::min(n, top + 1 + static_cast<std::size_t>(120.0 / frameMs)); ++k)
                        if (toDb(raw[k]) <= toDb(peak) - 10.0) abrupt = true;
                    if (length >= 1500.0) ++riser;
                    else if (abrupt) ++reverse;
                }
            }
            t = std::max(top, t) + 1;
        }
        if (reverse > 0) c.items.push_back({Element::ReverseCymbal, std::clamp(reverse / 3.0, 0.4, 1.0), reverse, 0.0});
        if (riser > 0) c.items.push_back({Element::Riser, std::clamp(riser / 2.0, 0.5, 1.0), riser, 0.0});

        // Impact : l'ensemble monte de 12 dB d'un coup, grave ET aigu, puis tous deux tiennent (à 15 dB
        // près de leur sommet) plus de 0,7 s. Un kick retombe trop vite, une 808 n'a pas d'aigu.
        int impacts = 0;
        const std::size_t hold = static_cast<std::size_t>(700.0 / frameMs);
        for (std::size_t t = 4; t + hold < n; ++t) {
            const double now = toDb(at(t).total), before = std::min({toDb(at(t - 2).total), toDb(at(t - 3).total), toDb(at(t - 4).total)});
            if (now - before < 12.0 || now < ref - 12.0) continue;
            std::size_t p = t;
            for (std::size_t k = t; k < std::min(n, t + 5); ++k)
                if (at(k).total > at(p).total) p = k;
            const double lowTop = toDb(low(p)), highTop = toDb(raw[p]);
            // Un coup (du bruit large à l'attaque), pas une nappe qui entre après un silence.
            const auto& fp = at(p);
            double noise = 0.0;
            for (int b = 0; b < kBands; ++b) noise += fp.p[b] + fp.r[b];
            if (lowTop < toDb(fp.total) - 10.0 || highTop < toDb(fp.total) - 15.0 || noise < 0.3 * fp.total) continue;
            bool held = true;
            for (std::size_t k = p; k < p + hold && k < n && held; ++k) held = toDb(low(k)) > lowTop - 15.0 && toDb(raw[k]) > highTop - 15.0;
            // Et il retombe : 4 dB plus bas au bout de 0,7 s, sans jamais repasser au-dessus du coup (un mélo
            // qui démarre après un silence, lui, tient ou monte).
            bool decays = p + hold < n && toDb(at(p + hold).total) <= toDb(fp.total) - 4.0;
            for (std::size_t k = p + 1; k < p + hold && k < n && decays; ++k) decays = at(k).total <= 1.25 * fp.total;
            held = held && decays;
            if (held) {
                ++impacts;
                t = p + hold;
            }
        }
        if (impacts > 0) c.items.push_back({Element::Impact, std::clamp(impacts / 2.0, 0.5, 1.0), impacts, 0.0});
    }
    // Dans l'ordre des groupes : DRUMS, 808, MÉLO, EFX.
    std::stable_sort(c.items.begin(), c.items.end(), [](const HeardItem& a, const HeardItem& b) {
        const auto rank = [](Element e) { const Family f = familyOf(e); return f == Family::Drums ? 0 : f == Family::Bass ? 1 : f == Family::Instrument ? 2 : 3; };
        return rank(a.element) < rank(b.element);
    });
    return c;
}

} // namespace omnis::stems
