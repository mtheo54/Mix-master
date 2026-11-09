// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/StyleRecognizer.cpp
// =============================================================================
#include "StyleRecognizer.h"

#include "core/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace omnis::ai {

namespace {

// Groupes de grandeurs (dans l'ordre de styleFeatures).
constexpr int kShapeFirst = 1, kShapeLast = 18;  // nœuds du Mid, relatifs au corps
constexpr int kWidthFirst = 6, kWidthLast = 18;  // Side − Mid
constexpr int kShapeDims = kShapeLast - kShapeFirst + 1;
constexpr int kWidthDims = kWidthLast - kWidthFirst + 1;
constexpr int kDims = kShapeDims + kWidthDims + 1 + 1 + 4 + 1; // + tempo (dernière)
constexpr int kTempo = kDims - 1;

std::vector<double> dimWeights(const StyleWeights& w) {
    // Chaque groupe pèse son poids, quel que soit son nombre de grandeurs.
    std::vector<double> out;
    for (int i = 0; i < kShapeDims; ++i) out.push_back(w.shape / std::sqrt(static_cast<double>(kShapeDims)));
    for (int i = 0; i < kWidthDims; ++i) out.push_back(w.width / std::sqrt(static_cast<double>(kWidthDims)));
    out.push_back(w.brightness);
    out.push_back(w.onsets);
    for (int i = 0; i < 4; ++i) out.push_back(w.micro / 2.0);
    out.push_back(w.tempo);
    return out;
}

void splitTitle(const std::string& title, std::string& family, std::string& style) {
    static const std::string sep = " › ";
    const auto at = title.find(sep);
    family = at == std::string::npos ? title : title.substr(0, at);
    style = at == std::string::npos ? std::string() : title.substr(at + sep.size());
}

} // namespace

std::vector<double> styleFeatures(const analysis::SpectralSignature& s) {
    std::vector<double> f;
    f.reserve(kDims);
    for (int k = kShapeFirst; k <= kShapeLast; ++k) f.push_back(s.relativeMidDb(k));
    for (int k = kWidthFirst; k <= kWidthLast; ++k) f.push_back(s.sideDb[static_cast<std::size_t>(k)] - s.midDb[static_cast<std::size_t>(k)]);
    f.push_back(s.music.centroidHz / 1000.0);
    f.push_back(s.music.onsetsPerSecond);
    for (double v : s.music.microDb) f.push_back(v);
    f.push_back(s.music.bpm > 30.0 ? std::log2(s.music.bpm) : 0.0); // 0 : pas de tempo net
    for (double& v : f)
        if (!std::isfinite(v)) v = 0.0;
    return f;
}

std::string sonicGroup(const std::string& family, const std::string& style) {
    struct Rule {
        const char* group;
        std::initializer_list<const char*> styles;
    };
    static const Rule kRules[] = {
        {"trap", {"trap", "trap fr", "dark trap", "dirty south"}},
        {"drill", {"drill", "drill fr", "uk drill"}},
        {"rage", {"rage", "rage fr", "hard clipping", "opium"}},
        {"plugg / new wave", {"plugg", "plugg fr", "pluggnb", "new wave fr", "cloud fr", "cloud"}},
        {"boom bap", {"boom bap", "boom bap fr", "east coast", "west coast", "jazz rap", "rap conscient", "kickeur", "underground", "rap hardcore", "rap alternatif"}},
        {"mélodique / pop", {"rap mélodique", "synth-pop", "rap festif"}},
        {"rock", {"nu-metal"}},
    };
    for (const auto& r : kRules)
        for (const char* st : r.styles)
            if (style == st) return r.group;
    if (family.find("Électro") != std::string::npos) return "électro";
    if (family.find("Rock") != std::string::npos) return "rock";
    if (family.find("Pop") != std::string::npos) return "mélodique / pop";
    if (family.find("Hyperpop") != std::string::npos) return "hyperpop";
    if (family.find("B.O.") != std::string::npos) return "b.o. / anime";
    return {};
}

std::vector<const ReferenceEntry*> profilesOf(const ReferenceLibrary& library) {
    std::vector<const ReferenceEntry*> out;
    for (const auto& e : library.entries())
        if (e.isProfile() && e.measured) out.push_back(&e);
    return out;
}

StyleRecognition recognizeStyle(const analysis::SpectralSignature& prod, const std::vector<const ReferenceEntry*>& profiles,
                                const StyleWeights& weights, const StyleStats* stats) {
    StyleRecognition out;
    std::vector<const ReferenceEntry*> used;
    std::vector<std::vector<double>> feats;
    for (const auto* p : profiles)
        if (p && p->isProfile() && p->measured) {
            used.push_back(p);
            feats.push_back(styleFeatures(p->signature));
        }
    // Rien entendu (spectre vide) : pas de réponse. Les fiches de la base n'ont pas de nombre de trames.
    const bool heard = std::any_of(prod.midDb.begin(), prod.midDb.end(), [](double v) { return std::isfinite(v) && v > -150.0 && v != 0.0; });
    if (used.empty() || !heard) return out;

    // Écart-type de chaque grandeur entre les profils : ce qui les distingue.
    std::vector<double> mean(kDims, 0.0), sd(kDims, 0.0);
    for (const auto& f : feats)
        for (int d = 0; d < kDims; ++d) mean[static_cast<std::size_t>(d)] += f[static_cast<std::size_t>(d)] / static_cast<double>(feats.size());
    for (const auto& f : feats)
        for (int d = 0; d < kDims; ++d) {
            const double x = f[static_cast<std::size_t>(d)] - mean[static_cast<std::size_t>(d)];
            sd[static_cast<std::size_t>(d)] += x * x / static_cast<double>(feats.size());
        }
    for (auto& v : sd) v = std::sqrt(v) > 1e-6 ? std::sqrt(v) : 1.0;

    const auto w = dimWeights(weights);
    const auto x = styleFeatures(prod);
    const bool tempoKnown = x[kTempo] > 0.0;
    // Dispersion commune (sur tous les profils ayant des statistiques) : le plancher des petits genres.
    std::vector<double> pooled(kDims, 0.0);
    int pooledN = 0;
    if (stats)
        for (const auto& [name, st] : *stats)
            if (static_cast<int>(st.sd.size()) == kDims) {
                for (int d = 0; d < kDims; ++d) pooled[static_cast<std::size_t>(d)] += st.sd[static_cast<std::size_t>(d)] * st.sd[static_cast<std::size_t>(d)];
                ++pooledN;
            }
    for (auto& v : pooled) v = pooledN > 0 ? std::sqrt(v / pooledN) : 0.0;
    for (std::size_t i = 0; i < used.size(); ++i) {
        const StyleClassStats* st = nullptr;
        if (stats)
            if (const auto it = stats->find(used[i]->title); it != stats->end() && static_cast<int>(it->second.mean.size()) == kDims) st = &it->second;
        double cost = 0.0;
        for (int d = 0; d < kDims; ++d) {
            const auto k = static_cast<std::size_t>(d);
            if (d == kTempo && (!tempoKnown || !st)) continue; // le tempo n'a de sens qu'avec les statistiques
            if (st) {
                // Bayésien naïf : écart réduit par la dispersion du genre, rétrécie vers la commune
                // (un genre de 3 titres n'a pas une dispersion fiable).
                const double n = st->titles, k0 = 4.0;
                double var = (n * st->sd[k] * st->sd[k] + k0 * pooled[k] * pooled[k]) / (n + k0);
                var = std::max(var, 1e-4);
                const double z = (x[k] - st->mean[k]) / std::sqrt(var);
                cost += w[k] * w[k] * (z * z + std::log(var));
            } else {
                const double z = w[k] * (x[k] - feats[i][k]) / sd[k];
                cost += z * z;
            }
        }
        StyleGuess g;
        g.profile = used[i];
        splitTitle(used[i]->title, g.family, g.style);
        g.distance = st ? 0.5 * cost : std::sqrt(cost);
        out.ranked.push_back(g);
    }
    std::sort(out.ranked.begin(), out.ranked.end(), [](const StyleGuess& a, const StyleGuess& b) { return a.distance < b.distance; });
    // Probabilités : softmax des distances.
    const double best = out.ranked.front().distance, t = std::max(1e-3, weights.temperature);
    double sum = 0.0;
    for (auto& g : out.ranked) sum += (g.probability = std::exp(-(g.distance - best) / t));
    for (auto& g : out.ranked) g.probability /= sum;
    const double p1 = out.ranked[0].probability, p2 = out.ranked.size() > 1 ? out.ranked[1].probability : 0.0;
    out.confident = p1 >= 0.45 && p1 >= 2.0 * p2;
    return out;
}

StyleStats buildStyleStats(const std::vector<const ReferenceEntry*>& titles,
                           const std::vector<std::pair<const ReferenceEntry*, std::string>>& profileOfTitle) {
    (void)titles;
    std::map<std::string, std::vector<std::vector<double>>> groups;
    for (const auto& [e, profile] : profileOfTitle)
        if (e) groups[profile].push_back(styleFeatures(e->signature));
    StyleStats out;
    for (const auto& [name, rows] : groups) {
        if (rows.size() < 3) continue;
        StyleClassStats st;
        st.titles = static_cast<int>(rows.size());
        st.mean.assign(kDims, 0.0);
        st.sd.assign(kDims, 0.0);
        std::vector<int> count(kDims, 0);
        for (const auto& r : rows)
            for (int d = 0; d < kDims; ++d)
                if (d != kTempo || r[static_cast<std::size_t>(d)] > 0.0) {
                    st.mean[static_cast<std::size_t>(d)] += r[static_cast<std::size_t>(d)];
                    ++count[static_cast<std::size_t>(d)];
                }
        for (int d = 0; d < kDims; ++d) st.mean[static_cast<std::size_t>(d)] /= std::max(1, count[static_cast<std::size_t>(d)]);
        for (const auto& r : rows)
            for (int d = 0; d < kDims; ++d)
                if (d != kTempo || r[static_cast<std::size_t>(d)] > 0.0) {
                    const double v = r[static_cast<std::size_t>(d)] - st.mean[static_cast<std::size_t>(d)];
                    st.sd[static_cast<std::size_t>(d)] += v * v;
                }
        for (int d = 0; d < kDims; ++d) st.sd[static_cast<std::size_t>(d)] = std::sqrt(st.sd[static_cast<std::size_t>(d)] / std::max(1, count[static_cast<std::size_t>(d)] - 1));
        out[name] = std::move(st);
    }
    return out;
}

std::string serializeStyleStats(const StyleStats& stats) {
    std::string out = "# OMNIS — dispersion des profils de genre (statistiques agrégées, aucune fiche de titre)\n";
    char b[64];
    for (const auto& [name, st] : stats) {
        out += name + "\t" + std::to_string(st.titles);
        for (double v : st.mean) std::snprintf(b, sizeof b, "\t%.4f", v), out += b;
        for (double v : st.sd) std::snprintf(b, sizeof b, "\t%.4f", v), out += b;
        out += "\n";
    }
    return out;
}

StyleStats parseStyleStats(const std::string& text) {
    StyleStats out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto end = text.find('\n', pos);
        const std::string line = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? text.size() : end + 1;
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        std::size_t a = 0;
        while (true) {
            const auto t = line.find('\t', a);
            f.push_back(line.substr(a, t == std::string::npos ? std::string::npos : t - a));
            if (t == std::string::npos) break;
            a = t + 1;
        }
        if (f.size() != static_cast<std::size_t>(2 + 2 * kDims)) continue;
        StyleClassStats st;
        st.titles = std::atoi(f[1].c_str());
        for (int d = 0; d < kDims; ++d) st.mean.push_back(std::atof(f[static_cast<std::size_t>(2 + d)].c_str()));
        for (int d = 0; d < kDims; ++d) st.sd.push_back(std::atof(f[static_cast<std::size_t>(2 + kDims + d)].c_str()));
        out[f[0]] = std::move(st);
    }
    return out;
}

StyleRecognition recognizeGroup(const analysis::SpectralSignature& prod, const StyleStats& groups, const StyleWeights& weights,
                                const std::vector<StyleExample>* personal) {
    StyleRecognition out;
    const bool heard = std::any_of(prod.midDb.begin(), prod.midDb.end(), [](double v) { return std::isfinite(v) && v > -150.0 && v != 0.0; });
    if (groups.empty() || !heard) return out;
    const auto w = dimWeights(weights);
    const auto x = styleFeatures(prod);
    const bool tempoKnown = x[kTempo] > 0.0;
    std::vector<double> pooled(kDims, 0.0);
    for (const auto& [name, st] : groups)
        for (int d = 0; d < kDims; ++d) pooled[static_cast<std::size_t>(d)] += st.sd[static_cast<std::size_t>(d)] * st.sd[static_cast<std::size_t>(d)] / groups.size();
    for (auto& v : pooled) v = std::sqrt(v);
    for (const auto& [name, st] : groups) {
        if (static_cast<int>(st.mean.size()) != kDims) continue;
        double cost = 0.0;
        for (int d = 0; d < kDims; ++d) {
            const auto k = static_cast<std::size_t>(d);
            if (d == kTempo && !tempoKnown) continue;
            const double n = st.titles, k0 = 4.0;
            const double var = std::max((n * st.sd[k] * st.sd[k] + k0 * pooled[k] * pooled[k]) / (n + k0), 1e-4);
            const double z = (x[k] - st.mean[k]) / std::sqrt(var);
            cost += w[k] * w[k] * (z * z + std::log(var));
        }
        StyleGuess g;
        g.style = name;
        g.distance = 0.5 * cost;
        out.ranked.push_back(g);
    }
    std::sort(out.ranked.begin(), out.ranked.end(), [](const StyleGuess& a, const StyleGuess& b) { return a.distance < b.distance; });
    const double best = out.ranked.front().distance, t = std::max(1e-3, weights.temperature);
    double sum = 0.0;
    for (auto& g : out.ranked) sum += (g.probability = std::exp(-(g.distance - best) / t));
    for (auto& g : out.ranked) g.probability /= sum;
    // Mémoire personnelle : noyau gaussien sur les prods étiquetées par la personne.
    if (personal && !personal->empty()) {
        std::map<std::string, double> vote;
        double total = 0.0;
        for (const auto& ex : *personal) {
            if (static_cast<int>(ex.features.size()) != kDims) continue;
            double d2 = 0.0;
            for (int d = 0; d < kDims; ++d) {
                const auto k = static_cast<std::size_t>(d);
                if (d == kTempo && (!tempoKnown || ex.features[k] <= 0.0)) continue;
                const double z = w[k] * (x[k] - ex.features[k]) / std::max(1e-3, pooled[k]);
                d2 += z * z;
            }
            const double v = std::exp(-0.5 * d2);
            vote[ex.group] += v;
            total += v;
        }
        const double n = static_cast<double>(personal->size());
        const double alpha = total > 1e-12 ? std::min(0.8, n / (n + 4.0)) : 0.0;
        if (alpha > 0.0) {
            for (const auto& [group, v] : vote)
                if (std::none_of(out.ranked.begin(), out.ranked.end(), [&](const StyleGuess& g) { return g.style == group; })) {
                    StyleGuess g;
                    g.style = group;
                    out.ranked.push_back(g); // un style que seule la personne connaît
                }
            for (auto& g : out.ranked) {
                const auto it = vote.find(g.style);
                g.probability = (1.0 - alpha) * g.probability + alpha * (it == vote.end() ? 0.0 : it->second / total);
            }
            std::sort(out.ranked.begin(), out.ranked.end(), [](const StyleGuess& a, const StyleGuess& b) { return a.probability > b.probability; });
        }
    }
    const double p1 = out.ranked[0].probability, p2 = out.ranked.size() > 1 ? out.ranked[1].probability : 0.0;
    // « Sûr » seulement quand le premier écrase les autres : mesuré, la reconnaissance seule se trompe souvent.
    out.confident = p1 >= 0.6 && p1 >= 3.0 * p2;
    return out;
}

std::string serializeStyleExamples(const std::vector<StyleExample>& examples) {
    std::string out = "# OMNIS — styles nommés par toi (grandeurs mesurées, aucun audio)\n";
    char b[48];
    for (const auto& ex : examples) {
        out += ex.group;
        for (double v : ex.features) std::snprintf(b, sizeof b, "\t%.4f", v), out += b;
        out += "\n";
    }
    return out;
}

std::vector<StyleExample> parseStyleExamples(const std::string& text) {
    std::vector<StyleExample> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto end = text.find('\n', pos);
        const std::string line = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? text.size() : end + 1;
        if (line.empty() || line[0] == '#') continue;
        StyleExample ex;
        std::size_t a = 0;
        bool first = true;
        while (true) {
            const auto t = line.find('\t', a);
            const std::string f = line.substr(a, t == std::string::npos ? std::string::npos : t - a);
            if (first) ex.group = f, first = false;
            else ex.features.push_back(std::atof(f.c_str()));
            if (t == std::string::npos) break;
            a = t + 1;
        }
        if (!ex.group.empty() && static_cast<int>(ex.features.size()) == kDims) out.push_back(std::move(ex));
    }
    return out;
}

std::string styleForGroup(const std::string& group) {
    static const std::pair<const char*, const char*> kMap[] = {
        {"trap", "trap"}, {"drill", "drill fr"}, {"rage", "rage fr"}, {"plugg / new wave", "new wave fr"}, {"boom bap", "boom bap"},
        {"mélodique / pop", "rap mélodique"}, {"rock", "Rock & Metal"}, {"électro", "Électro, House & Dance"},
        {"hyperpop", "Hyperpop & Expérimental"}, {"b.o. / anime", "B.O., Anime & Jeux Vidéo"},
    };
    for (const auto& [g, st] : kMap)
        if (group == g) return st;
    return group;
}

std::string describeRecognition(const StyleRecognition& r) {
    if (r.empty()) return i18n::tr("Je n'ai pas encore assez écouté pour reconnaître le style.");
    char b[320];
    const auto& g = r.ranked;
    const auto pct = [](double p) { return static_cast<int>(std::lround(100.0 * p)); };
    if (r.confident) {
        std::snprintf(b, sizeof b, i18n::tr("J'entends : %s (%d %%).").c_str(), i18n::tr(g[0].name()).c_str(), pct(g[0].probability));
    } else if (g.size() >= 3) {
        std::snprintf(b, sizeof b, i18n::tr("J'hésite : %s (%d %%), %s (%d %%) ou %s (%d %%). Dis-moi le style si tu le sais.").c_str(),
                      i18n::tr(g[0].name()).c_str(), pct(g[0].probability), i18n::tr(g[1].name()).c_str(), pct(g[1].probability),
                      i18n::tr(g[2].name()).c_str(), pct(g[2].probability));
    } else {
        std::snprintf(b, sizeof b, i18n::tr("J'hésite : %s (%d %%). Dis-moi le style si tu le sais.").c_str(), i18n::tr(g[0].name()).c_str(),
                      pct(g[0].probability));
    }
    return b;
}

} // namespace omnis::ai
