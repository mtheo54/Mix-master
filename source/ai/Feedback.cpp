// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/Feedback.cpp
// =============================================================================
#include "Feedback.h"

#include "MatchIntent.h"
#include "TextMatch.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <locale>
#include <map>
#include <sstream>

namespace omnis::ai {

namespace {

constexpr int kBands = params::kMatchBands;
constexpr double kMaxEffect = 6.0;
constexpr double kMaxLoudnessEffect = 8.0;

std::vector<std::string> splitOn(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == sep) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    return out;
}

std::string clean(std::string s) {
    for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%+.3f", v);
    return buf;
}

double toDouble(const std::string& s) {
    std::istringstream in(s);
    in.imbue(std::locale::classic());
    double v = 0.0;
    in >> v;
    return v;
}

std::string serializeDescriptor(const Descriptor& d) {
    std::string out;
    for (int a = 0; a < kAxisCount; ++a) {
        const double v = d.v[static_cast<std::size_t>(a)];
        if (std::fabs(v) < 1e-9) continue;
        out += (out.empty() ? "" : ";") + std::string(axisKey(static_cast<Axis>(a))) + "=" + number(v);
    }
    return out;
}

bool any(const Descriptor& d) {
    return std::any_of(d.v.begin(), d.v.end(), [](double v) { return std::fabs(v) > 1e-9; });
}

double limitOf(int axis) { return static_cast<Axis>(axis) == Axis::Loudness ? kMaxLoudnessEffect : kMaxEffect; }

/// Variation d'effet suggérée pour chaque concept d'un retour : la correction
/// est partagée entre les concepts au prorata de leur poids, et convertie en
/// effet (désir = poids × effet, donc Δeffet = c · signe(poids) / Σ|poids|).
/// Les mots inconnus reçoivent chacun une part entière de la correction.
struct Suggestions {
    std::vector<std::pair<std::string, Descriptor>> concepts;
    std::vector<std::pair<std::string, Descriptor>> words;
};

Suggestions suggest(const FeedbackRecord& r) {
    Suggestions s;
    if (!any(r.correction)) return s;
    double total = 0.0;
    for (const auto& h : r.hits)
        if (!h.protect) total += std::fabs(h.weight);
    const double shares = total + static_cast<double>(r.unknownWords.size());
    if (shares <= 0.0) return s;
    for (const auto& h : r.hits) {
        if (h.protect || std::fabs(h.weight) < 1e-9) continue;
        Descriptor d;
        for (int a = 0; a < kAxisCount; ++a)
            if (axisApplied(static_cast<Axis>(a)))
                d.v[static_cast<std::size_t>(a)] = r.correction.v[static_cast<std::size_t>(a)] * (h.weight > 0.0 ? 1.0 : -1.0) / shares;
        s.concepts.emplace_back(h.conceptId, d);
    }
    for (const auto& w : r.unknownWords) {
        Descriptor d;
        for (int a = 0; a < kAxisCount; ++a)
            if (axisApplied(static_cast<Axis>(a))) d.v[static_cast<std::size_t>(a)] = r.correction.v[static_cast<std::size_t>(a)] / shares;
        s.words.emplace_back(w, d);
    }
    return s;
}

/// Effet d'un mot appris : axes dont la moyenne dépasse 0.5 et dont toutes
/// les observations vont dans le même sens. Vide si rien de cohérent.
std::optional<Descriptor> consistentEffect(const std::vector<Descriptor>& observations) {
    Descriptor mean;
    bool found = false;
    for (int a = 0; a < kAxisCount; ++a) {
        double sum = 0.0;
        int positive = 0, negative = 0;
        for (const auto& o : observations) {
            const double v = o.v[static_cast<std::size_t>(a)];
            sum += v;
            positive += v > 1e-9;
            negative += v < -1e-9;
        }
        const double m = sum / static_cast<double>(observations.size());
        if (std::fabs(m) >= 0.5 && (positive == 0 || negative == 0)) {
            mean.v[static_cast<std::size_t>(a)] = m;
            found = true;
        }
    }
    if (!found) return std::nullopt;
    return mean;
}

std::string describe(const Descriptor& d) {
    std::string out;
    char buf[96];
    for (int a = 0; a < kAxisCount; ++a) {
        const double v = d.v[static_cast<std::size_t>(a)];
        if (std::fabs(v) < 0.005) continue;
        std::snprintf(buf, sizeof(buf), "%s%s %+.2f", out.empty() ? "" : ", ", axisLabel(static_cast<Axis>(a)), v);
        out += buf;
    }
    return out;
}

Concept learnedConcept(const std::string& word, const Descriptor& effect) {
    Concept c;
    c.id = "appris_" + word;
    c.expressions.push_back(word);
    c.tokenized.push_back({word});
    c.effect = effect;
    return c;
}

// ---- Agrégats ------------------------------------------------------------------

struct Tally {
    int n = 0;
    int contributors = 0;
    Descriptor sum;
};

struct Aggregate {
    int contributors = 0;
    std::map<std::string, Tally> concepts, words;
    std::map<std::string, int> references;
    std::map<int, int> ratings;
};

std::string serializeAggregate(const Aggregate& g) {
    std::string out = "# OMNIS — retours agrégés (anonymes : aucun audio, aucune phrase)\n";
    out += "contributeurs " + std::to_string(g.contributors) + "\n";
    const auto line = [&](const char* kind, const std::string& id, const Tally& t) {
        out += std::string(kind) + " " + id + " " + std::to_string(t.n) + " " + std::to_string(t.contributors);
        const std::string d = serializeDescriptor(t.sum);
        if (!d.empty()) out += " " + d;
        out += "\n";
    };
    for (const auto& [id, t] : g.concepts) line("concept", id, t);
    for (const auto& [w, t] : g.words) line("mot", w, t);
    for (const auto& [r, n] : g.references) out += "reference " + r + " " + std::to_string(n) + "\n";
    for (const auto& [note, n] : g.ratings) out += "note " + std::to_string(note) + " " + std::to_string(n) + "\n";
    return out;
}

Aggregate parseAggregate(std::string_view text) {
    Aggregate g;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream l(line);
        l.imbue(std::locale::classic());
        std::string kind;
        l >> kind;
        if (kind == "contributeurs") {
            int n = 0;
            l >> n;
            g.contributors += n;
        } else if (kind == "concept" || kind == "mot") {
            std::string id, descriptor;
            Tally t;
            l >> id >> t.n >> t.contributors >> descriptor;
            for (const auto& item : splitOn(descriptor, ';')) {
                const auto eq = item.find('=');
                if (eq == std::string::npos) continue;
                if (auto axis = axisFromKey(item.substr(0, eq))) t.sum[*axis] = toDouble(item.substr(eq + 1));
            }
            auto& target = (kind == "concept" ? g.concepts : g.words)[id];
            target.n += t.n;
            target.contributors += t.contributors;
            for (int a = 0; a < kAxisCount; ++a) target.sum.v[static_cast<std::size_t>(a)] += t.sum.v[static_cast<std::size_t>(a)];
        } else if (kind == "reference") {
            std::string name;
            int n = 0;
            l >> name >> n;
            g.references[name] += n;
        } else if (kind == "note") {
            int note = 0, n = 0;
            l >> note >> n;
            g.ratings[note] += n;
        }
    }
    return g;
}

} // namespace

std::string serializeFeedback(const FeedbackRecord& r) {
    std::string hits;
    for (const auto& h : r.hits) hits += (hits.empty() ? "" : ";") + h.conceptId + ":" + number(h.weight) + (h.protect ? "!" : "");
    std::string words, refs;
    for (const auto& w : r.unknownWords) words += (words.empty() ? "" : ";") + clean(w);
    for (const auto& w : r.unknownReferences) refs += (refs.empty() ? "" : ";") + clean(w);
    return "v1\t" + std::to_string(r.rating) + "\t" + clean(r.reference) + "\t" + clean(r.request) + "\t" + hits + "\t" +
           serializeDescriptor(r.correction) + "\t" + words + "\t" + refs;
}

std::optional<FeedbackRecord> parseFeedback(std::string_view line) {
    const auto f = splitOn(line, '\t');
    if (f.size() < 8 || f[0] != "v1") return std::nullopt;
    FeedbackRecord r;
    r.rating = std::atoi(f[1].c_str());
    r.reference = f[2];
    r.request = f[3];
    for (const auto& item : splitOn(f[4], ';')) {
        const auto colon = item.find(':');
        if (colon == std::string::npos) continue;
        ConceptHit h;
        h.conceptId = item.substr(0, colon);
        std::string w = item.substr(colon + 1);
        if (!w.empty() && w.back() == '!') {
            h.protect = true;
            w.pop_back();
        }
        h.weight = toDouble(w);
        r.hits.push_back(std::move(h));
    }
    if (auto c = parseCorrection(f[5])) r.correction = *c;
    for (const auto& w : splitOn(f[6], ';'))
        if (!w.empty()) r.unknownWords.push_back(w);
    for (const auto& w : splitOn(f[7], ';'))
        if (!w.empty()) r.unknownReferences.push_back(w);
    return r;
}

bool appendFeedback(const std::string& path, const FeedbackRecord& record) {
    std::ofstream out(path, std::ios::app | std::ios::binary);
    if (!out) return false;
    out << serializeFeedback(record) << '\n';
    return static_cast<bool>(out);
}

std::vector<FeedbackRecord> loadFeedback(const std::string& path) {
    std::vector<FeedbackRecord> out;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line))
        if (auto r = parseFeedback(line)) out.push_back(std::move(*r));
    return out;
}

std::optional<Descriptor> parseCorrection(std::string_view text) {
    Descriptor d;
    std::string t(text);
    std::replace(t.begin(), t.end(), ',', ';');
    for (auto item : splitOn(t, ';')) {
        item.erase(std::remove(item.begin(), item.end(), ' '), item.end());
        if (item.empty()) continue;
        const auto eq = item.find('=');
        if (eq == std::string::npos) return std::nullopt;
        const auto axis = axisFromKey(item.substr(0, eq));
        if (!axis) return std::nullopt;
        d[*axis] = toDouble(item.substr(eq + 1));
    }
    return d;
}

Descriptor correctionFromCurves(const MatchCurve& proposedMid, const MatchCurve& keptMid, const MatchCurve& proposedSide,
                                const MatchCurve& keptSide, double proposedLufs, double keptLufs) {
    Descriptor d;
    std::array<double, kRegionCount> sum{};
    std::array<int, kRegionCount> count{};
    double width = 0.0;
    int widthCount = 0;
    for (int k = 0; k < kBands; ++k) {
        const auto i = static_cast<std::size_t>(k);
        const int r = regionOfBand(k);
        sum[static_cast<std::size_t>(r)] += keptMid[i] - proposedMid[i];
        ++count[static_cast<std::size_t>(r)];
        if (params::matchBandHz(k) >= 640.0) {
            width += (keptSide[i] - keptMid[i]) - (proposedSide[i] - proposedMid[i]);
            ++widthCount;
        }
    }
    for (int r = 0; r < kRegionCount; ++r)
        d.v[static_cast<std::size_t>(r)] = count[static_cast<std::size_t>(r)] ? sum[static_cast<std::size_t>(r)] / count[static_cast<std::size_t>(r)] : 0.0;
    d[Axis::Width] = widthCount ? width / widthCount : 0.0;
    d[Axis::Loudness] = keptLufs - proposedLufs;
    return d;
}

LearningReport learnFromFeedback(Lexicon& lexicon, const std::vector<FeedbackRecord>& records, double rate) {
    LearningReport report;
    std::map<std::string, Descriptor> changes;
    std::map<std::string, std::vector<Descriptor>> wordObservations;
    for (const auto& r : records) {
        const auto s = suggest(r);
        if (s.concepts.empty() && s.words.empty()) continue;
        ++report.recordsUsed;
        for (const auto& [id, d] : s.concepts) {
            Concept* c = lexicon.find(id);
            if (!c) continue;
            for (int a = 0; a < kAxisCount; ++a) {
                const auto i = static_cast<std::size_t>(a);
                const double before = c->effect.v[i];
                c->effect.v[i] = std::clamp(before + rate * d.v[i], -limitOf(a), limitOf(a));
                changes[id].v[i] += c->effect.v[i] - before;
            }
        }
        for (const auto& [w, d] : s.words) wordObservations[w].push_back(d);
    }
    for (const auto& [id, d] : changes)
        if (const std::string text = describe(d); !text.empty()) report.changes.push_back("« " + id + " » : " + text);
    // Un mot inconnu, deux fois suivi des mêmes corrections, devient un concept.
    for (const auto& [word, observations] : wordObservations) {
        if (observations.size() < 2 || lexicon.find("appris_" + word)) continue;
        if (auto effect = consistentEffect(observations)) {
            lexicon.add(learnedConcept(word, *effect));
            report.changes.push_back("mot appris « " + word + " » : " + describe(*effect));
        }
    }
    return report;
}

std::string exportAggregate(const std::vector<FeedbackRecord>& records) {
    Aggregate g;
    g.contributors = 1;
    for (const auto& r : records) {
        ++g.ratings[std::clamp(r.rating, -1, 1)];
        for (const auto& name : r.unknownReferences) {
            std::string id = normalizeText(name);
            std::replace(id.begin(), id.end(), ' ', '_');
            ++g.references[id];
        }
        const auto s = suggest(r);
        for (const auto& [id, d] : s.concepts) {
            auto& t = g.concepts[id];
            ++t.n;
            for (int a = 0; a < kAxisCount; ++a) t.sum.v[static_cast<std::size_t>(a)] += d.v[static_cast<std::size_t>(a)];
        }
        for (const auto& [w, d] : s.words) {
            auto& t = g.words[w];
            ++t.n;
            for (int a = 0; a < kAxisCount; ++a) t.sum.v[static_cast<std::size_t>(a)] += d.v[static_cast<std::size_t>(a)];
        }
    }
    for (auto& [id, t] : g.concepts) t.contributors = 1;
    for (auto& [w, t] : g.words) t.contributors = 1;
    return serializeAggregate(g);
}

std::string mergeAggregates(const std::vector<std::string>& aggregates) {
    std::string all;
    for (const auto& a : aggregates) all += a + "\n";
    return serializeAggregate(parseAggregate(all));
}

LearningReport applyCommunity(Lexicon& lexicon, std::string_view merged, const CommunityRules& rules) {
    LearningReport report;
    const Aggregate g = parseAggregate(merged);
    for (const auto& [id, t] : g.concepts) {
        Concept* c = lexicon.find(id);
        if (!c || t.contributors < rules.minContributors || t.n < rules.minObservations) continue;
        Descriptor delta;
        for (int a = 0; a < kAxisCount; ++a) {
            const auto i = static_cast<std::size_t>(a);
            const double before = c->effect.v[i];
            c->effect.v[i] = std::clamp(before + rules.rate * t.sum.v[i] / t.n, -limitOf(a), limitOf(a));
            delta.v[i] = c->effect.v[i] - before;
        }
        ++report.recordsUsed;
        if (const std::string text = describe(delta); !text.empty())
            report.changes.push_back("« " + id + " » (" + std::to_string(t.contributors) + " personnes) : " + text);
    }
    for (const auto& [word, t] : g.words) {
        if (t.contributors < rules.minContributors || t.n < rules.minObservations || lexicon.find("appris_" + word)) continue;
        Descriptor mean;
        bool found = false;
        for (int a = 0; a < kAxisCount; ++a) {
            const double m = t.sum.v[static_cast<std::size_t>(a)] / t.n;
            if (std::fabs(m) >= 0.5) {
                mean.v[static_cast<std::size_t>(a)] = m;
                found = true;
            }
        }
        if (!found) continue;
        lexicon.add(learnedConcept(word, mean));
        ++report.recordsUsed;
        report.changes.push_back("mot appris « " + word + " » (" + std::to_string(t.contributors) + " personnes) : " + describe(mean));
    }
    for (const auto& [name, n] : g.references)
        if (n >= rules.minContributors) report.changes.push_back("référence demandée " + std::to_string(n) + " fois, à ajouter à la base : « " + name + " »");
    return report;
}

} // namespace omnis::ai
