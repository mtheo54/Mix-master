// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/Composition.cpp
// =============================================================================
#include "Composition.h"

#include "core/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <unordered_map>

namespace omnis::stems {

namespace {

using link::BandFrame;
using link::kBands;

/// Moindres carrés à coefficients positifs par descente de coordonnées (n petit : ≤ 3 × 64 régresseurs).
/// Les régresseurs d'un même groupe (une piste et ses décalages) ont un gain total borné par cap[groupe].
std::vector<double> nnls(const std::vector<std::vector<double>>& G, const std::vector<double>& c, const std::vector<bool>& enabled,
                         const std::vector<double>& cap, int groupSize, std::vector<double> g = {}) {
    const std::size_t n = c.size();
    // Départ : `g` (solution voisine, les régresseurs coupés à zéro) ou zéro.
    if (g.size() != n) g.assign(n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        if (!enabled[i]) g[i] = 0.0;
    for (int iter = 0; iter < 300; ++iter) {
        double change = 0.0, scale = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            if (!enabled[i] || G[i][i] <= 0.0) continue;
            double r = c[i];
            for (std::size_t j = 0; j < n; ++j)
                if (j != i) r -= G[i][j] * g[j];
            const std::size_t group = i / static_cast<std::size_t>(groupSize);
            double others = 0.0;
            for (std::size_t j = group * groupSize; j < (group + 1) * groupSize; ++j)
                if (j != i) others += g[j];
            const double next = std::clamp(r / G[i][i], 0.0, std::max(0.0, cap[group] - others));
            change = std::max(change, std::fabs(next - g[i]));
            scale = std::max(scale, next);
            g[i] = next;
        }
        if (change <= 1e-7 * std::max(1.0, scale)) break;
    }
    return g;
}

double residual(const std::vector<std::vector<double>>& G, const std::vector<double>& c, double yy, const std::vector<double>& g) {
    double r = yy;
    for (std::size_t i = 0; i < g.size(); ++i) {
        r -= 2.0 * g[i] * c[i];
        for (std::size_t j = 0; j < g.size(); ++j) r += g[i] * G[i][j] * g[j];
    }
    return std::max(0.0, r);
}

std::string pct(double v) {
    char b[16];
    std::snprintf(b, sizeof b, "%d %%", static_cast<int>(std::lround(100.0 * std::clamp(v, 0.0, 1.0))));
    return b;
}

} // namespace

BusFit fitBus(const std::vector<BandFrame>& bus, const std::vector<const std::vector<BandFrame>*>& tracks, const MatchOptions& options) {
    BusFit fit;
    const std::size_t n = tracks.size();
    fit.gains.assign(n, 0.0);
    fit.shares.assign(n, 0.0);
    fit.removalLoss.assign(n, 0.0);
    fit.overshoot.assign(n, 1.0);
    if (bus.empty() || n == 0) return fit;
    // Les dernières trames du bus.
    const std::size_t from = bus.size() > static_cast<std::size_t>(options.maxFrames) ? bus.size() - static_cast<std::size_t>(options.maxFrames) : 0;
    std::vector<std::unordered_map<std::int64_t, const BandFrame*>> maps(n);
    for (std::size_t i = 0; i < n; ++i)
        for (const auto& f : *tracks[i]) maps[i][f.index] = &f;
    // Chaque piste est comparée telle quelle et décalée d'une trame dans chaque sens : une latence
    // mal compensée (jusqu'à une trame, ~43 ms) répartit son énergie sur deux trames du bus.
    constexpr int kLags = 3; // décalages −1, 0, +1
    const auto has = [&](std::size_t i, std::int64_t k) { return maps[i].count(k - 1) && maps[i].count(k) && maps[i].count(k + 1); };
    // Une piste qui n'a pas joué assez de ces trames (posée plus tard) n'est pas jugée ;
    // les autres sont comparées sur les trames qu'elles ont toutes.
    std::vector<bool> enabled(n, false);
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t covered = 0;
        for (std::size_t t = from; t < bus.size(); ++t) covered += has(i, bus[t].index);
        enabled[i] = covered >= static_cast<std::size_t>(options.minCommonFrames) && 2 * covered >= bus.size() - from;
    }
    std::vector<std::size_t> rows;
    for (std::size_t t = from; t < bus.size(); ++t) {
        bool all = true;
        for (std::size_t i = 0; i < n && all; ++i)
            if (enabled[i] && !has(i, bus[t].index)) all = false;
        if (all) rows.push_back(t);
    }
    fit.common = static_cast<int>(rows.size());
    if (fit.common < options.minCommonFrames) return fit;
    const auto at = [&](std::size_t i, std::size_t t, int lag) -> const BandFrame& { return *maps[i].at(bus[t].index - (lag - 1)); };

    // Chaque bande pèse autant (énergie du bus de la bande ramenée à 1).
    std::array<double, kBands> bandTotal{};
    for (auto t : rows)
        for (int b = 0; b < kBands; ++b) bandTotal[static_cast<std::size_t>(b)] += bus[t].energy[static_cast<std::size_t>(b)];
    const double maxBand = *std::max_element(bandTotal.begin(), bandTotal.end());
    if (!(maxBand > 0.0)) return fit;
    std::array<double, kBands> w{};
    for (int b = 0; b < kBands; ++b) w[static_cast<std::size_t>(b)] = 1.0 / std::max({bandTotal[static_cast<std::size_t>(b)], 1e-3 * maxBand, 1e-30});

    // Une piste du bus n'y pèse jamais plus que le bus : dans chaque bande où elle sonne, son gain (total
    // des trois décalages) est au plus le rapport bus / piste sur trois trames (insensible au décalage)
    // là où le bus est le plus calme (10e centile), à 25 % près. Une piste qui n'y est pas joue aussi
    // quand le bus se tait : son gain possible tombe à presque rien, elle ne peut plus « boucher le
    // trou » d'une piste du bus sans OMNIS.
    std::vector<double> upper(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        if (!enabled[i]) continue;
        std::array<double, kBands> trackTotal{}, trackMax{};
        const auto x3 = [&](std::size_t t, std::size_t bb) {
            return static_cast<double>(at(i, t, 0).energy[bb]) + at(i, t, 1).energy[bb] + at(i, t, 2).energy[bb];
        };
        for (auto t : rows)
            for (int b = 0; b < kBands; ++b) {
                const double v = x3(t, static_cast<std::size_t>(b));
                trackTotal[static_cast<std::size_t>(b)] += v;
                trackMax[static_cast<std::size_t>(b)] = std::max(trackMax[static_cast<std::size_t>(b)], v);
            }
        double all = 0.0;
        for (double v : trackTotal) all += v;
        double cap = std::numeric_limits<double>::infinity();
        std::vector<double> ratios;
        for (int b = 0; b < kBands && all > 0.0; ++b) {
            const auto bb = static_cast<std::size_t>(b);
            if (trackTotal[bb] < 0.1 * all) continue; // bande où la piste ne pèse pas
            ratios.clear();
            for (std::size_t k = 0; k < rows.size(); ++k) {
                const double v = x3(rows[k], bb);
                if (v < 0.05 * trackMax[bb]) continue;
                // Le bus sur les mêmes trois trames (voisines présentes dans le bus, sinon la trame seule).
                double y = bus[rows[k]].energy[bb], scale = 1.0;
                const std::size_t t = rows[k];
                if (t > 0 && t + 1 < bus.size() && bus[t - 1].index == bus[t].index - 1 && bus[t + 1].index == bus[t].index + 1) {
                    y += bus[t - 1].energy[bb] + bus[t + 1].energy[bb];
                } else {
                    scale = 3.0;
                }
                ratios.push_back(scale * y / v);
            }
            if (ratios.empty()) continue;
            const auto q = ratios.begin() + static_cast<std::ptrdiff_t>(ratios.size() / 10);
            std::nth_element(ratios.begin(), q, ratios.end());
            cap = std::min(cap, 1.25 * *q);
        }
        upper[i] = std::isfinite(cap) ? cap : 0.0;
    }
    // Une piste dont le gain possible est déjà sous le seuil ne peut pas être du bus : hors du calcul
    // (beaucoup moins de régresseurs dans un gros projet).
    for (std::size_t i = 0; i < n; ++i)
        if (upper[i] < 0.5 * options.minTrackGain) enabled[i] = false;

    // Régresseurs : (piste, décalage). Par bande : Gram, produits croisés, énergie du bus.
    const std::size_t m = n * kLags;
    std::vector<bool> on(m, false);
    for (std::size_t r = 0; r < m; ++r) on[r] = enabled[r / kLags];
    std::array<std::vector<std::vector<double>>, kBands> Gb;
    std::array<std::vector<double>, kBands> cb, linb;
    std::array<double, kBands> yyb{}, ylinb{};
    for (int b = 0; b < kBands; ++b) {
        Gb[static_cast<std::size_t>(b)].assign(m, std::vector<double>(m, 0.0));
        cb[static_cast<std::size_t>(b)].assign(m, 0.0);
        linb[static_cast<std::size_t>(b)].assign(m, 0.0);
    }
    std::vector<const BandFrame*> x(m, nullptr);
    for (auto t : rows) {
        for (std::size_t r = 0; r < m; ++r) x[r] = on[r] ? &at(r / kLags, t, static_cast<int>(r % kLags)) : nullptr;
        for (int b = 0; b < kBands; ++b) {
            const auto bb = static_cast<std::size_t>(b);
            const double y = bus[t].energy[bb], wb = w[bb];
            yyb[bb] += wb * y * y;
            ylinb[bb] += wb * y;
            for (std::size_t r = 0; r < m; ++r) {
                if (!x[r]) continue;
                const double xr = x[r]->energy[bb];
                cb[bb][r] += wb * xr * y;
                linb[bb][r] += wb * xr;
                for (std::size_t q = r; q < m; ++q)
                    if (x[q]) Gb[bb][r][q] += wb * xr * x[q]->energy[bb];
            }
        }
    }
    std::vector<std::vector<double>> G(m, std::vector<double>(m, 0.0));
    std::vector<double> c(m, 0.0);
    double yy = 0.0;
    for (int b = 0; b < kBands; ++b) {
        const auto bb = static_cast<std::size_t>(b);
        for (std::size_t r = 0; r < m; ++r)
            for (std::size_t q = 0; q < r; ++q) Gb[bb][r][q] = Gb[bb][q][r];
        for (std::size_t r = 0; r < m; ++r) {
            c[r] += cb[bb][r];
            for (std::size_t q = 0; q < m; ++q) G[r][q] += Gb[bb][r][q];
        }
        yy += yyb[bb];
    }
    if (!(yy > 0.0)) return fit;

    const auto g = nnls(G, c, on, upper, kLags);
    const double r0 = residual(G, c, yy, g);
    fit.explained = std::clamp(1.0 - r0 / yy, 0.0, 1.0);
    std::array<double, kBands> r0b{};
    for (int b = 0; b < kBands; ++b)
        r0b[static_cast<std::size_t>(b)] = residual(Gb[static_cast<std::size_t>(b)], cb[static_cast<std::size_t>(b)], yyb[static_cast<std::size_t>(b)], g);
    for (std::size_t i = 0; i < n; ++i) {
        double gain = 0.0;
        for (int lag = 0; lag < kLags; ++lag) gain += g[i * kLags + static_cast<std::size_t>(lag)];
        fit.gains[i] = gain;
        if (!enabled[i] || gain <= 0.0) continue;
        auto without = on;
        for (int lag = 0; lag < kLags; ++lag) without[i * kLags + static_cast<std::size_t>(lag)] = false;
        const auto g2 = nnls(G, c, without, upper, kLags, g);
        // Une piste compte dans une bande au moins (des hats discrets sont l'essentiel de l'aigu du bus) :
        // sa part et ce qu'on perd sans elle sont pris dans la bande où ils sont les plus nets.
        int best = -1;
        for (int b = 0; b < kBands; ++b) {
            const auto bb = static_cast<std::size_t>(b);
            if (!(yyb[bb] > 1e-6 * yy) || !(ylinb[bb] > 0.0)) continue;
            double contribution = 0.0;
            for (int lag = 0; lag < kLags; ++lag) contribution += g[i * kLags + static_cast<std::size_t>(lag)] * linb[bb][i * kLags + static_cast<std::size_t>(lag)];
            const double share = std::clamp(contribution / ylinb[bb], 0.0, 1.0);
            if (share > fit.shares[i]) {
                fit.shares[i] = share;
                best = b;
            }
            fit.removalLoss[i] = std::max(fit.removalLoss[i], std::max(0.0, residual(Gb[bb], cb[bb], yyb[bb], g2) - r0b[bb]) / yyb[bb]);
        }
        // Dans sa bande la plus nette : les trames où, pesée, elle dépasse le bus de 30 % (sur trois
        // trames, comme la borne : un décalage ne compte pas pour un dépassement).
        if (best >= 0) {
            const auto bb = static_cast<std::size_t>(best);
            std::vector<std::pair<double, double>> pairs; // (piste pesée, bus) sur trois trames
            double peak = 0.0;
            for (auto t : rows) {
                if (t == 0 || t + 1 >= bus.size() || bus[t - 1].index != bus[t].index - 1 || bus[t + 1].index != bus[t].index + 1) continue;
                const double v = gain * (static_cast<double>(at(i, t, 0).energy[bb]) + at(i, t, 1).energy[bb] + at(i, t, 2).energy[bb]);
                const double y = static_cast<double>(bus[t - 1].energy[bb]) + bus[t].energy[bb] + bus[t + 1].energy[bb];
                pairs.emplace_back(v, y);
                peak = std::max(peak, v);
            }
            int counted = 0, over = 0;
            for (const auto& [v, y] : pairs) {
                if (v < 0.05 * peak) continue;
                ++counted;
                if (v > 1.3 * y + 1e-4 * peak) ++over;
            }
            fit.overshoot[i] = counted > 0 ? static_cast<double>(over) / counted : 1.0;
        }
    }
    return fit;
}

Composition buildComposition(const std::vector<Heard>& instances, const MatchOptions& options) {
    Composition out;
    std::vector<const Heard*> tracks, buses;
    for (const auto& h : instances) (h.role == link::Role::Bus ? buses : tracks).push_back(&h);
    std::vector<const std::vector<BandFrame>*> trackFrames;
    for (const auto* t : tracks) trackFrames.push_back(&t->frames);
    const auto memberOf = [](const Heard& h) {
        Member m;
        m.id = h.id;
        m.name = h.name;
        m.element = h.identity.element;
        m.heard = h.identity.heard;
        m.confidence = h.identity.confidence;
        m.secondsHeard = h.identity.secondsHeard;
        m.nameDisagrees = h.identity.nameDisagrees;
        return m;
    };
    std::vector<bool> placed(tracks.size(), false);
    for (const auto* b : buses) {
        BusView v;
        v.id = b->id;
        v.name = b->name;
        v.heard = b->identity.element != Element::Auto ? b->identity.element : b->identity.heard;
        v.secondsHeard = b->identity.secondsHeard;
        v.kick = b->identity.kick;
        v.snare = b->identity.snare;
        v.hat = b->identity.hat;
        v.openHat = b->identity.openHat;
        const BusFit fit = fitBus(b->frames, trackFrames, options);
        v.enoughToMatch = fit.common >= options.minCommonFrames;
        v.explained = fit.explained;
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            if (fit.shares[i] < options.minShare || fit.removalLoss[i] < options.minGain || fit.overshoot[i] > options.maxOvershoot ||
                fit.gains[i] < options.minTrackGain)
                continue;
            Member m = memberOf(*tracks[i]);
            m.share = fit.shares[i];
            v.members.push_back(std::move(m));
            placed[i] = true;
        }
        std::sort(v.members.begin(), v.members.end(), [](const Member& a, const Member& c) { return a.share > c.share; });
        // Le groupe du bus : son nom, sinon celui de la plupart de ses pistes, sinon l'écoute.
        v.family = familyFromName(v.name);
        if (v.family == Family::Unknown && !v.members.empty()) {
            std::map<Family, double> weight;
            for (const auto& m : v.members) weight[m.family()] += m.share;
            v.family = std::max_element(weight.begin(), weight.end(), [](const auto& a, const auto& c) { return a.second < c.second; })->first;
        }
        if (v.family == Family::Unknown) v.family = familyOf(v.heard);
        out.buses.push_back(std::move(v));
    }
    for (std::size_t i = 0; i < tracks.size(); ++i)
        if (!placed[i]) out.direct.push_back(memberOf(*tracks[i]));
    return out;
}

std::vector<Heard> readInstances(const link::Observer& observer) {
    std::vector<Heard> out;
    for (auto& v : observer.instances()) {
        Heard h;
        h.id = v.slot;
        h.role = v.role;
        h.name = std::move(v.name);
        h.identity = v.identity;
        h.frames = observer.frames(v.slot, v.owner);
        out.push_back(std::move(h));
    }
    return out;
}

std::string describeMember(const Member& m) {
    std::string s = "« " + (m.name.empty() ? i18n::tr("piste sans nom") : m.name) + " » → ";
    if (m.element == Element::Auto) return s + i18n::tr("écoute en cours");
    s += i18n::tr(elementLabel(m.element)) + " (" + i18n::tr(familyLabel(m.family()));
    if (m.confidence > 0.0) s += ", " + pct(m.confidence);
    s += ")";
    if (m.nameDisagrees && m.heard != Element::Auto) s += i18n::tr(" : le nom dit autre chose, je me fie à l'écoute");
    else if (m.heard != Element::Auto && m.heard != m.element) s += i18n::tr(" ; l'écoute seule : ") + i18n::tr(elementLabel(m.heard));
    return s;
}

std::string describeComposition(const Composition& c) {
    if (c.empty()) return {};
    std::string s;
    for (const auto& b : c.buses) {
        if (!s.empty()) s += "\n";
        s += i18n::tr("BUS « ") + (b.name.empty() ? i18n::tr("sans nom") : b.name) + " » (" + i18n::tr(familyLabel(b.family)) + ")";
        if (!b.members.empty()) {
            s += " :";
            for (const auto& m : b.members) s += "\n  " + describeMember(m);
        } else if (!b.enoughToMatch) {
            s += i18n::tr(" : lance la lecture, je retrouve ses pistes en quelques secondes.");
        } else {
            s += i18n::tr(" : aucune piste OMNIS retrouvée dedans.");
        }
        std::vector<std::string> inside;
        if (b.kick) inside.push_back("Kick");
        if (b.snare) inside.push_back("Snare / clap");
        if (b.hat) inside.push_back("Hi-hat");
        if (b.openHat) inside.push_back("Open hat");
        if (!inside.empty()) {
            s += "\n  " + i18n::tr("entendu dans le bus : ");
            for (std::size_t i = 0; i < inside.size(); ++i) s += (i ? ", " : "") + inside[i];
        } else if (b.members.empty() && b.heard != Element::Auto) {
            s += "\n  " + i18n::tr("entendu dans le bus : ") + i18n::tr(elementLabel(b.heard));
        }
    }
    if (!c.direct.empty()) {
        if (!s.empty()) s += "\n";
        s += c.buses.empty() ? i18n::tr("PISTES :") : i18n::tr("HORS BUS (vers le master) :");
        for (const auto& m : c.direct) s += "\n  " + describeMember(m);
    }
    return s;
}

} // namespace omnis::stems
