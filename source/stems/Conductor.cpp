// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/Conductor.cpp
// =============================================================================
#include "Conductor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace omnis::stems {

namespace {

constexpr double kTiny = 1.0e-20;

double toDb(double p) { return 10.0 * std::log10(std::max(p, kTiny)); }

bool isLowVictim(Element e) { return e == Element::Bass808 || e == Element::Bass || e == Element::Kick; }

/// Une victime du grave ne gêne pas une autre victime du grave (kick / 808 :
/// une autre affaire, la place de chacun dans le temps).
/// Un bus de drums contient presque toujours le kick : son grave n'est pas à couper.
bool canMaskLow(Element e) { return !isLowVictim(e) && e != Element::Auto && e != Element::DrumLoop; }

bool canMaskVocal(Element e) {
    const Family f = familyOf(e);
    return f == Family::Instrument || f == Family::Fx || e == Element::Backs;
}

std::string quoted(const ConductorStem& s) {
    return "« " + (s.name.empty() ? std::string(elementLabel(s.element)) : s.name) + " »";
}

std::string hzText(double hz) {
    char buffer[32];
    if (hz >= 1000.0) std::snprintf(buffer, sizeof buffer, "%.1f kHz", hz / 1000.0);
    else std::snprintf(buffer, sizeof buffer, "%.0f Hz", hz);
    std::string text = buffer;
    std::replace(text.begin(), text.end(), '.', ','); // 3,5 kHz
    if (text.size() > 6 && text.compare(text.size() - 6, 6, ",0 kHz") == 0) text.erase(text.size() - 6, 2); // 6 kHz
    return text;
}

struct Activity {
    std::vector<double> total;  ///< puissance totale par bloc
    double loud = 0.0;          ///< bloc le plus fort
    std::vector<double> bandMean; ///< puissance moyenne par bande sur les blocs actifs
    double meanTotal = 0.0;
};

Activity activityOf(const StemTimeline& t) {
    Activity a;
    a.total.resize(static_cast<std::size_t>(t.size()));
    for (int k = 0; k < t.size(); ++k) {
        a.total[static_cast<std::size_t>(k)] = t.totalPower(k);
        a.loud = std::max(a.loud, a.total[static_cast<std::size_t>(k)]);
    }
    a.bandMean.assign(kTimelineBands, 0.0);
    int n = 0;
    for (int k = 0; k < t.size(); ++k) {
        if (!(a.total[static_cast<std::size_t>(k)] > a.loud * 1.0e-3)) continue; // à 30 dB du plus fort
        for (int b = 0; b < kTimelineBands; ++b) a.bandMean[static_cast<std::size_t>(b)] += t.bandPower(k, b);
        a.meanTotal += a.total[static_cast<std::size_t>(k)];
        ++n;
    }
    if (n > 0) {
        for (auto& x : a.bandMean) x /= n;
        a.meanTotal /= n;
    }
    return a;
}

bool active(const Activity& a, int k) {
    return k < static_cast<int>(a.total.size()) && a.loud > 0.0 && a.total[static_cast<std::size_t>(k)] > a.loud * 1.0e-3;
}

double median(std::vector<double>& v) {
    if (v.empty()) return -200.0;
    const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

} // namespace

std::uint32_t Conductor::keyFor(std::uint64_t target, std::uint64_t victim, Proposal::Kind kind) noexcept {
    // FNV-1a sur les deux identifiants et la sorte.
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xffu;
            h *= 1099511628211ull;
        }
    };
    mix(target);
    mix(victim);
    mix(static_cast<std::uint64_t>(kind));
    return static_cast<std::uint32_t>(h ^ (h >> 32));
}

std::vector<Proposal> Conductor::propose(const std::vector<ConductorStem>& stems) {
    std::vector<Proposal> out;
    for (auto& p : evaluate(stems))
        if (p.correction.type != Correction::Type::None) out.push_back(std::move(p));
    return out;
}

std::vector<Proposal> Conductor::evaluate(const std::vector<ConductorStem>& stems) {
    std::vector<Proposal> out;
    std::vector<Activity> act;
    act.reserve(stems.size());
    for (const auto& s : stems) act.push_back(s.timeline ? activityOf(*s.timeline) : Activity{});

    for (std::size_t v = 0; v < stems.size(); ++v) {
        const ConductorStem& victim = stems[v];
        if (!victim.timeline || act[v].loud <= 0.0) continue;
        const bool low = isLowVictim(victim.element);
        const bool vocal = victim.element == Element::Vocal;
        if (!low && !vocal) continue;

        // Zone de la victime, et seuil de gêne de chaque bande : dans son cœur
        // (à 10 dB de son maximum), un gêneur à moins de 8 dB d'elle ; dans le
        // haut de son corps (10 à 20 dB sous son maximum, ce qui la fait
        // entendre sur de petites enceintes), un gêneur plus fort qu'elle.
        std::vector<int> zone;
        std::vector<double> threshold;
        if (low) {
            double peak = 0.0;
            for (int b = 0; b < kTimelineBands; ++b)
                if (kTimelineEdges[static_cast<std::size_t>(b) + 1] <= 300.0) peak = std::max(peak, act[v].bandMean[static_cast<std::size_t>(b)]);
            for (int b = 0; b < kTimelineBands; ++b) {
                const double level = act[v].bandMean[static_cast<std::size_t>(b)];
                if (kTimelineEdges[static_cast<std::size_t>(b) + 1] > 300.0 || level <= peak * 0.01) continue;
                zone.push_back(b);
                threshold.push_back(level > peak * 0.1 ? kLowGapDb : kLowBodyGapDb);
            }
        } else {
            for (int b = 0; b < kTimelineBands; ++b)
                if (kTimelineEdges[static_cast<std::size_t>(b)] >= 1200.0 && kTimelineEdges[static_cast<std::size_t>(b) + 1] <= 6000.0) {
                    zone.push_back(b);
                    threshold.push_back(kVocalGapDb);
                }
        }
        if (zone.empty()) continue;
        const double zoneLo = kTimelineEdges[static_cast<std::size_t>(zone.front())];
        const double zoneHi = kTimelineEdges[static_cast<std::size_t>(zone.back()) + 1];

        for (std::size_t m = 0; m < stems.size(); ++m) {
            if (m == v) continue;
            const ConductorStem& masker = stems[m];
            if (!masker.timeline || act[m].loud <= 0.0) continue;
            if (low ? !canMaskLow(masker.element) : !canMaskVocal(masker.element)) continue;
            if (masker.timeline->sampleRate() != victim.timeline->sampleRate()) continue;

            // Part de la zone dans l'énergie du gêneur.
            double zoneShare = 0.0;
            for (const int b : zone) zoneShare += act[m].bandMean[static_cast<std::size_t>(b)];
            zoneShare /= std::max(act[m].meanTotal, kTiny);
            if (low && zoneShare < 0.02) continue;

            // Écart dans chaque bande, sur les blocs où les deux jouent.
            const int blocks = std::min(victim.timeline->size(), masker.timeline->size());
            int shared = 0;
            std::vector<std::vector<double>> gaps(zone.size());
            for (int k = 0; k < blocks; ++k) {
                if (!active(act[v], k) || !active(act[m], k)) continue;
                ++shared;
                for (std::size_t z = 0; z < zone.size(); ++z) {
                    const double pv = victim.timeline->bandPower(k, zone[z]);
                    const double pm = masker.timeline->bandPower(k, zone[z]);
                    // La victime doit être là dans cette bande à cet instant.
                    if (pv < act[v].bandMean[static_cast<std::size_t>(zone[z])] * 0.1) continue;
                    gaps[z].push_back(toDb(pm) - toDb(pv));
                }
            }
            const double sharedSeconds = shared * victim.timeline->blockSeconds();
            if (sharedSeconds < kMinSharedSeconds) continue;
            // La bande la plus gênante : la plus au-dessus de son seuil.
            double worst = -200.0, excess = -400.0;
            int worstBand = zone.front();
            for (std::size_t z = 0; z < zone.size(); ++z) {
                if (static_cast<double>(gaps[z].size()) * victim.timeline->blockSeconds() < kMinSharedSeconds / 2) continue;
                const double g = median(gaps[z]);
                if (g - threshold[z] > excess) {
                    excess = g - threshold[z];
                    worst = g;
                    worstBand = zone[z];
                }
            }
            Proposal p;
            p.kind = low ? Proposal::Kind::LowMasking : Proposal::Kind::VocalMasking;
            p.key = keyFor(masker.id, victim.id, p.kind);
            p.target = static_cast<int>(m);
            p.victim = static_cast<int>(v);
            p.gapDb = worst;
            p.sharedSeconds = sharedSeconds;
            p.zoneLowHz = zoneLo;
            p.zoneHighHz = zoneHi;
            p.excessDb = excess;
            for (std::size_t z = 0; z < zone.size(); ++z)
                p.bands.push_back({kTimelineEdges[static_cast<std::size_t>(zone[z])], kTimelineEdges[static_cast<std::size_t>(zone[z]) + 1],
                                   gaps[z].empty() ? -200.0 : median(gaps[z]), threshold[z], static_cast<int>(gaps[z].size())});
            if (excess <= 0.0) {
                out.push_back(std::move(p)); // bilan : pas de gêne
                continue;
            }
            // La zone citée : la bande gênante.
            const double bandLo = kTimelineEdges[static_cast<std::size_t>(worstBand)];
            const double bandHi = kTimelineEdges[static_cast<std::size_t>(worstBand) + 1];
            char detail[200];
            if (worst >= 0.0)
                std::snprintf(detail, sizeof detail, " entre %s et %s, pendant %.0f s où ils jouent ensemble (il y est même plus fort, de %.0f dB).",
                              hzText(bandLo).c_str(), hzText(bandHi).c_str(), sharedSeconds, worst);
            else
                std::snprintf(detail, sizeof detail, " entre %s et %s, pendant %.0f s où ils jouent ensemble (seulement %.0f dB d'écart).",
                              hzText(bandLo).c_str(), hzText(bandHi).c_str(), sharedSeconds, -worst);
            if (low) {
                const double hz = std::clamp(bandHi, 60.0, 200.0);
                if (zoneShare >= 0.4) {
                    p.correction = {Correction::Type::LowShelf, static_cast<float>(hz), -3.0f, 0.7071f};
                    p.text = quoted(masker) + " couvre " + quoted(victim) + detail + " Ce grave est le cœur de " + quoted(masker) +
                             " : le baisser de 3 dB sous " + hzText(hz) + " ?";
                } else {
                    p.correction = {Correction::Type::LowCut, static_cast<float>(hz), 0.0f, 0.7071f};
                    p.text = quoted(masker) + " couvre " + quoted(victim) + detail + " Couper le grave de " + quoted(masker) +
                             " sous " + hzText(hz) + " ?";
                }
            } else {
                const double hz = std::sqrt(kTimelineEdges[static_cast<std::size_t>(worstBand)] * kTimelineEdges[static_cast<std::size_t>(worstBand) + 1]);
                const float gain = worst > 0.0 ? -3.0f : -2.0f;
                p.correction = {Correction::Type::Bell, static_cast<float>(hz), gain, 1.2f};
                char g[16];
                std::snprintf(g, sizeof g, "%.0f", -gain);
                p.text = quoted(masker) + " couvre la voix " + quoted(victim) + detail + " Creuser " + quoted(masker) + " de " + g +
                         " dB vers " + hzText(hz) + " ?";
            }
            out.push_back(std::move(p));
        }
    }
    std::sort(out.begin(), out.end(), [](const Proposal& a, const Proposal& b) { return a.gapDb > b.gapDb; });
    return out;
}

} // namespace omnis::stems
