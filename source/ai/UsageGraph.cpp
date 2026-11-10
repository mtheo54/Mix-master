// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/UsageGraph.cpp
// =============================================================================
#include "UsageGraph.h"

#include "core/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace omnis::ai {

namespace {

/// Sous ces écarts, un geste ne s'entend pas : pente 0,3 dB, sub 0,5 dB, Inflator 4 %, volume 0,5 dB,
/// largeur 5 %, seuil du Comp 1 dB.
constexpr double kAudible[kHabits] = {0.3, 0.5, 4.0, 0.5, 5.0, 1.0};
constexpr const char* kUp[kHabits] = {"Plus d'aigu (pente)", "Plus de sub", "Plus de densité (Inflator)", "Plus fort", "Plus large", "Moins de colle (Comp)"};
constexpr const char* kDown[kHabits] = {"Plus de grave (pente)", "Moins de sub", "Moins de densité (Inflator)", "Moins fort", "Plus étroit", "Plus de colle (Comp)"};
constexpr const char* kFiddle[kHabits] = {"Hésite sur la pente", "Hésite sur le sub", "Hésite sur l'Inflator", "Hésite sur le volume", "Hésite sur la largeur",
                                          "Hésite sur le Comp"};
constexpr const char* kModules[8] = {"Sub", "Match EQ", "Inflator", "Imager", "Multibande", "Clipper", "Limiteur", "Comp"};
constexpr const char* kInfl[4] = {"Inflate", "Bande", "Lampe", "Transistor"};
constexpr const char* kComp[4] = {"Opto", "FET", "VCA", "Vari-mu"};
constexpr const char* kMatch[2] = {"Propre", "Analogique"};

std::string clean(std::string s) {
    for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

std::string jsonText(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) < 0x20) continue;
        out += c;
    }
    return out + "\"";
}

/// La connaissance de base : le geste va-t-il contre les règles du métier ? (raison, vide sinon)
std::string risk(int h, double amount, const UsageState& kept) {
    const double v = kept.habits[static_cast<std::size_t>(h)];
    switch (static_cast<Habit>(h)) {
        case Habit::Tilt: return std::fabs(v) > 4.0 ? "pente extrême (plus de 4 dB) : le master se déséquilibre" : "";
        case Habit::Sub: return amount > 0 && v > 6.0 ? "sub poussé au-delà de +6 dB : le grave sature le master" : "";
        case Habit::Inflate: return amount > 0 && v > 85.0 ? "Inflator presque à fond : il distord" : "";
        case Habit::Loudness: return amount > 0 && v > 14.0 ? "plus de 14 dB de gain clip + limiteur : la prod s'écrase" : "";
        case Habit::Width: return amount > 0 && v > 170.0 ? "médium à plus de 170 % : le mix se creuse en mono" : "";
        case Habit::Comp: return amount < 0 && v < -30.0 && kept.modules[7] ? "seuil du Comp sous −30 dB : il écrase la dynamique" : "";
        default: return "";
    }
}

std::string lowerText(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

/// Ce que chaque style assume alors que la règle générale l'interdit (connaissance de base des genres).
struct DaRule {
    const char* gesture;
    std::vector<const char*> styles;
    const char* why; ///< « %s » : le style reconnu
};
const std::vector<DaRule>& daRules() {
    static const std::vector<DaRule> rules = {
        {"volume+", {"rage", "hard clipping", "hyperpop", "digicore", "phonk", "opium", "hardstyle", "gabber", "riddim", "brostep", "dubstep"},
         "en %s, le son s'écrase exprès : volume et saturation font la DA"},
        {"inflate+", {"rage", "hard clipping", "phonk", "hyperpop", "digicore", "opium", "dark trap", "rap metal"}, "en %s, la saturation est une couleur voulue"},
        {"sub+", {"phonk", "rage", "hard clipping", "bass music", "dubstep", "riddim", "brostep"}, "en %s, une basse énorme fait partie du son"},
        {"tilt-", {"lo-fi", "boom bap", "jazz rap", "plugg", "cloud", "trip-hop", "soul"}, "en %s, un son sombre et feutré est voulu"},
        {"tilt+", {"hyperpop", "digicore", "rage", "hard clipping"}, "en %s, un son très brillant est voulu"},
        {"largeur+", {"ambient", "shoegaze", "dream pop", "chillwave", "cloud", "psych", "trip-hop", "synthwave"}, "en %s, un espace très large fait l'ambiance"},
        {"grave:large", {"ambient", "shoegaze", "dream pop", "chillwave"}, "en %s (loin du club), un grave large et enveloppant se défend"},
        {"comp-", {"hard clipping", "rage", "phonk", "french touch", "house", "big beat"}, "en %s, la pompe du Comp est un effet voulu"},
    };
    return rules;
}

} // namespace

std::string artisticReason(const std::string& gestureId, const UsageSession& s) {
    std::string where = lowerText(s.context);
    for (const auto& st : s.styles) where += " | " + lowerText(st);
    for (const auto& rule : daRules()) {
        if (gestureId != rule.gesture) continue;
        for (const char* w : rule.styles)
            if (where.find(w) != std::string::npos) {
                char b[200];
                std::snprintf(b, sizeof b, rule.why, w);
                return b;
            }
    }
    return {};
}

const char* verdictLabel(Verdict v) noexcept {
    switch (v) {
        case Verdict::Useful: return "utile";
        case Verdict::Inaudible: return "inaudible";
        case Verdict::Hesitation: return "hésitation";
        case Verdict::Risky: return "à risque";
        case Verdict::Undone: return "annulé";
        case Verdict::Creative: return "choix artistique";
    }
    return "?";
}

std::vector<Gesture> judgeSession(const UsageSession& s) {
    std::vector<Gesture> out;
    const bool compToggled = s.proposed.modules[7] != s.kept.modules[7];
    for (int h = 0; h < kHabits; ++h) {
        const auto k = static_cast<std::size_t>(h);
        if (h == static_cast<int>(Habit::Comp) && compToggled) continue; // allumer le Comp n'est pas « plus de colle »
        const double d = s.kept.habits[k] - s.proposed.habits[k], thr = kAudible[h], moved = s.movement[k];
        Gesture g;
        g.habit = h;
        g.amount = d;
        if (std::fabs(d) < thr) {
            // Presque revenu au départ après avoir beaucoup bougé : une hésitation, à ne pas apprendre.
            if (moved >= 6.0 * thr) {
                g.id = std::string(habitKey(static_cast<Habit>(h))) + "~";
                g.label = kFiddle[h];
                g.verdict = Verdict::Hesitation;
                g.why = "beaucoup bougé pour revenir presque au départ";
                out.push_back(std::move(g));
            }
            continue; // inaudible : on passe à côté
        }
        g.id = std::string(habitKey(static_cast<Habit>(h))) + (d > 0 ? "+" : "-");
        g.label = d > 0 ? kUp[h] : kDown[h];
        if (moved >= 4.0 * std::fabs(d) && std::fabs(d) < 3.0 * thr) {
            g.verdict = Verdict::Hesitation;
            g.why = "beaucoup d'allers-retours pour un petit écart";
        } else if (const std::string r = risk(h, d, s.kept); !r.empty()) {
            // Contre la règle générale : mais peut-être la DA du style visé.
            if (const std::string da = artisticReason(g.id, s); !da.empty()) {
                g.verdict = Verdict::Creative;
                g.why = da;
            } else {
                g.verdict = Verdict::Risky;
                g.why = r;
            }
        } else {
            g.verdict = Verdict::Useful;
            g.why = s.exported ? "gardé jusqu'à l'export" : "gardé";
        }
        out.push_back(std::move(g));
    }
    // Modules allumés / éteints.
    for (int m = 0; m < 8; ++m) {
        const auto k = static_cast<std::size_t>(m);
        if (s.proposed.modules[k] == s.kept.modules[k]) continue;
        Gesture g;
        g.id = std::string("mod") + std::to_string(m) + (s.kept.modules[k] ? ":on" : ":off");
        g.label = std::string(kModules[m]) + (s.kept.modules[k] ? " allumé" : " éteint");
        if (m == 6 && !s.kept.modules[k]) {
            g.verdict = Verdict::Risky;
            g.why = "sans limiteur, les crêtes passent le plafond";
        } else {
            g.why = s.exported ? "gardé jusqu'à l'export" : "gardé";
        }
        out.push_back(std::move(g));
    }
    // Caractères changés.
    const auto character = [&](const char* key, const char* module, int from, int to, const char* const* names, int count) {
        if (from == to || to < 0 || to >= count) return;
        Gesture g;
        g.id = std::string(key) + ":" + std::to_string(to);
        g.label = std::string(module) + " → " + names[to];
        g.why = s.exported ? "gardé jusqu'à l'export" : "gardé";
        out.push_back(std::move(g));
    };
    character("infl", "Inflator", s.proposed.inflCharacter, s.kept.inflCharacter, kInfl, 4);
    character("comp", "Comp", s.proposed.compCharacter, s.kept.compCharacter, kComp, 4);
    character("match", "Match EQ", s.proposed.matchCharacter, s.kept.matchCharacter, kMatch, 2);
    // Le grave élargi : contre la règle de base (le grave reste au centre).
    if (s.kept.lowWidth > 100.5 && s.kept.lowWidth > s.proposed.lowWidth + 2.0) {
        Gesture g;
        g.id = "grave:large";
        g.label = "Grave élargi";
        if (const std::string da = artisticReason(g.id, s); !da.empty()) {
            g.verdict = Verdict::Creative;
            g.why = da;
        } else {
            g.verdict = Verdict::Risky;
            g.why = "le grave doit rester au centre (mono) : club, téléphone, vinyle";
        }
        out.push_back(std::move(g));
    }
    // Proposition annulée aussitôt : rien de tout ça ne s'apprend.
    if (s.undone)
        for (auto& g : out) {
            g.verdict = Verdict::Undone;
            g.why = "proposition annulée aussitôt";
        }
    return out;
}

HabitVector usefulDeltas(const UsageSession& s, const std::vector<Gesture>& gestures) {
    HabitVector d{};
    for (const auto& g : gestures)
        if (g.habit >= 0 && (g.verdict == Verdict::Useful || g.verdict == Verdict::Creative)) d[static_cast<std::size_t>(g.habit)] = s.kept.habits[static_cast<std::size_t>(g.habit)] - s.proposed.habits[static_cast<std::size_t>(g.habit)];
    return d;
}

// ---------------------------------------------------------------- le graphe
UsageGraph::Node& UsageGraph::node(const std::string& id, const std::string& label, bool context) {
    for (auto& n : nodes_)
        if (n.id == id) return n;
    nodes_.push_back({id, clean(label), context});
    return nodes_.back();
}

UsageGraph::Edge& UsageGraph::edge(const std::string& a, const std::string& b) {
    for (auto& e : edges_)
        if (e.a == a && e.b == b) return e;
    edges_.push_back({a, b});
    return edges_.back();
}

const UsageGraph::Node* UsageGraph::find(std::string_view id) const {
    for (const auto& n : nodes_)
        if (n.id == id) return &n;
    return nullptr;
}

std::string UsageGraph::contextId(const UsageSession& s) {
    const bool preset = s.start == SessionStart::Preset;
    const std::string ctxName = s.context.empty() ? std::string(preset ? "sans nom" : "style inconnu") : s.context;
    return std::string(preset ? "ctx:preset:" : "ctx:auto:") + clean(ctxName);
}

void UsageGraph::reviewArtistic(const UsageSession& s, std::vector<Gesture>& gestures) const {
    if (!s.exported || s.undone) return;
    const std::string ctx = contextId(s);
    for (auto& g : gestures) {
        if (g.verdict != Verdict::Risky || g.id == "mod6:off") continue; // sans limiteur : jamais une DA
        for (const auto& e : edges_)
            if (e.a == ctx && e.b == g.id && e.daExported >= 1) {
                g.verdict = Verdict::Creative;
                g.why = "gardé jusqu'à l'export, encore une fois, dans ce contexte : ta DA";
            }
    }
}

void UsageGraph::record(const UsageSession& s, const std::vector<Gesture>& gestures) {
    ++sessions_;
    const bool preset = s.start == SessionStart::Preset;
    const std::string ctxName = s.context.empty() ? std::string(preset ? "sans nom" : "style inconnu") : s.context;
    const std::string ctx = contextId(s);
    Node& c = node(ctx, std::string(preset ? "Preset · " : "AUTO · ") + ctxName, true);
    ++c.seen;
    c.lastSession = sessions_;
    std::vector<std::string> useful;
    for (const auto& g : gestures) {
        Node& n = node(g.id, g.label, false);
        ++n.seen;
        n.lastSession = sessions_;
        const bool ok = g.verdict == Verdict::Useful || g.verdict == Verdict::Creative;
        if (ok) {
            ++n.useful;
            n.amount += std::fabs(g.amount);
            if (s.exported) ++n.exported;
            useful.push_back(g.id);
        }
        if (g.verdict == Verdict::Risky) ++n.risky;
        if (g.verdict == Verdict::Creative) ++n.creative;
        Edge& e = edge(ctx, g.id);
        ++e.count;
        if (ok) ++e.useful;
        if (s.exported && (g.verdict == Verdict::Risky || g.verdict == Verdict::Creative)) ++e.daExported;
    }
    // Les gestes utiles faits ensemble : liés (a < b).
    std::sort(useful.begin(), useful.end());
    for (std::size_t i = 0; i < useful.size(); ++i)
        for (std::size_t j = i + 1; j < useful.size(); ++j) {
            Edge& e = edge(useful[i], useful[j]);
            ++e.count;
            ++e.useful;
        }
    prune();
}

void UsageGraph::prune() {
    // On passe à côté de ce qui n'est arrivé qu'une fois, il y a longtemps.
    std::vector<std::string> gone;
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                                [&](const Node& n) {
                                    const bool stale = n.seen <= 1 && sessions_ - n.lastSession >= kForgetAfter;
                                    if (stale) gone.push_back(n.id);
                                    return stale;
                                }),
                 nodes_.end());
    // Taille bornée : au-delà de 300 nœuds, les moins vus partent.
    if (nodes_.size() > 300) {
        std::sort(nodes_.begin(), nodes_.end(), [](const Node& a, const Node& b) { return a.seen > b.seen; });
        for (std::size_t i = 300; i < nodes_.size(); ++i) gone.push_back(nodes_[i].id);
        nodes_.resize(300);
    }
    if (gone.empty()) return;
    edges_.erase(std::remove_if(edges_.begin(), edges_.end(),
                                [&](const Edge& e) {
                                    return std::find(gone.begin(), gone.end(), e.a) != gone.end() || std::find(gone.begin(), gone.end(), e.b) != gone.end();
                                }),
                 edges_.end());
}

std::vector<const UsageGraph::Node*> UsageGraph::habits() const {
    std::vector<const Node*> out;
    for (const auto& n : nodes_)
        if (!n.context && n.useful >= kMinRepeats && n.useful >= kMinUsefulShare * n.seen) out.push_back(&n);
    // Gardé jusqu'à l'export : compte double.
    std::sort(out.begin(), out.end(), [](const Node* a, const Node* b) { return a->useful + a->exported > b->useful + b->exported; });
    return out;
}

std::vector<const UsageGraph::Node*> UsageGraph::doubtful() const {
    std::vector<const Node*> out;
    for (const auto& n : nodes_)
        if (!n.context && n.seen >= kMinRepeats && n.useful < 0.4 * n.seen) out.push_back(&n);
    std::sort(out.begin(), out.end(), [](const Node* a, const Node* b) { return a->seen > b->seen; });
    return out;
}

std::string UsageGraph::serialize() const {
    std::ostringstream o;
    o << "omnis-graphe\tv" << kVersion << "\tseances\t" << sessions_ << "\n";
    for (const auto& n : nodes_)
        o << "N\t" << n.id << "\t" << (n.context ? 1 : 0) << "\t" << n.seen << "\t" << n.useful << "\t" << n.exported << "\t" << n.risky << "\t" << n.creative << "\t" << n.amount << "\t"
          << n.lastSession << "\t" << n.label << "\n";
    for (const auto& e : edges_) o << "E\t" << e.a << "\t" << e.b << "\t" << e.count << "\t" << e.useful << "\t" << e.daExported << "\n";
    return o.str();
}

std::optional<UsageGraph> UsageGraph::parse(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::string line;
    if (!std::getline(in, line)) return std::nullopt;
    std::vector<std::string> f;
    const auto split = [&](const std::string& l) {
        f.clear();
        std::size_t start = 0;
        for (std::size_t i = 0; i <= l.size(); ++i)
            if (i == l.size() || l[i] == '\t') {
                f.push_back(l.substr(start, i - start));
                start = i + 1;
            }
    };
    split(line);
    if (f.size() < 4 || f[0] != "omnis-graphe" || (f[1] != "v1" && f[1] != "v2")) return std::nullopt;
    const bool v2 = f[1] == "v2"; // v1 : sans choix artistiques
    UsageGraph g;
    try {
        g.sessions_ = std::stoi(f[3]);
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            split(line);
            const std::size_t extra = v2 ? 1 : 0;
            if (f.size() == 10 + extra && f[0] == "N") {
                Node n;
                n.id = f[1];
                n.context = f[2] == "1";
                n.seen = std::stoi(f[3]);
                n.useful = std::stoi(f[4]);
                n.exported = std::stoi(f[5]);
                n.risky = std::stoi(f[6]);
                if (v2) n.creative = std::stoi(f[7]);
                n.amount = std::stod(f[7 + extra]);
                n.lastSession = std::stoi(f[8 + extra]);
                n.label = f[9 + extra];
                if (n.seen < 0 || n.useful < 0 || n.useful > n.seen || n.creative < 0 || n.creative > n.useful || !std::isfinite(n.amount)) return std::nullopt;
                g.nodes_.push_back(std::move(n));
            } else if (f.size() == 5 + extra && f[0] == "E") {
                g.edges_.push_back({f[1], f[2], std::stoi(f[3]), std::stoi(f[4]), v2 ? std::stoi(f[5]) : 0});
            } else if (!line.empty()) {
                return std::nullopt;
            }
        }
    } catch (...) {
        return std::nullopt;
    }
    return g;
}

std::string UsageGraph::exportJson() const {
    const auto habitual = habits();
    const auto isHabit = [&](const Node& n) { return std::find(habitual.begin(), habitual.end(), &n) != habitual.end(); };
    std::ostringstream o;
    o << "{\"format\":\"omnis-graphe\",\"version\":" << kVersion << ",\"seances\":" << sessions_ << ",\"noeuds\":[";
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const auto& n = nodes_[i];
        char mean[32];
        std::snprintf(mean, sizeof mean, "%.2f", n.useful > 0 ? n.amount / n.useful : 0.0);
        o << (i ? "," : "") << "{\"id\":" << jsonText(n.id) << ",\"label\":" << jsonText(n.label) << ",\"type\":\"" << (n.context ? "contexte" : "geste")
          << "\",\"vu\":" << n.seen << ",\"utile\":" << n.useful << ",\"exporte\":" << n.exported << ",\"risque\":" << n.risky << ",\"creatif\":" << n.creative << ",\"moyenne\":" << mean
          << ",\"habitude\":" << (isHabit(n) ? "true" : "false") << "}";
    }
    o << "],\"liens\":[";
    for (std::size_t i = 0; i < edges_.size(); ++i)
        o << (i ? "," : "") << "{\"a\":" << jsonText(edges_[i].a) << ",\"b\":" << jsonText(edges_[i].b) << ",\"n\":" << edges_[i].count << ",\"utile\":" << edges_[i].useful << ",\"da\":" << edges_[i].daExported << "}";
    o << "]}\n";
    return o.str();
}

std::string UsageGraph::describe() const {
    if (sessions_ == 0) return {};
    char b[160];
    std::snprintf(b, sizeof b, i18n::tr("Sur %d séance(s) après AUTO ou un preset").c_str(), sessions_);
    std::string s = b;
    const auto h = habits();
    if (h.empty()) {
        s += i18n::tr(" : pas encore d'habitude nette (un geste doit revenir 3 fois et être utile).");
    } else {
        s += i18n::tr(", ce que tu fais le plus : ");
        for (std::size_t i = 0; i < h.size() && i < 5; ++i) {
            std::snprintf(b, sizeof b, i18n::tr("%s (%d fois)").c_str(), i18n::tr(h[i]->label).c_str(), h[i]->useful);
            s += (i ? ", " : "") + std::string(b);
        }
        s += ".";
    }
    // Les choix artistiques : contre la règle générale, mais voulus (la DA de la personne).
    std::vector<const Node*> da;
    for (const auto& n : nodes_)
        if (!n.context && n.creative >= 2) da.push_back(&n);
    std::sort(da.begin(), da.end(), [](const Node* x, const Node* y) { return x->creative > y->creative; });
    if (!da.empty()) {
        s += i18n::tr(" Tes choix artistiques (DA), appris pour leur style : ");
        for (std::size_t i = 0; i < da.size() && i < 3; ++i) {
            std::snprintf(b, sizeof b, "%s (%d)", i18n::tr(da[i]->label).c_str(), da[i]->creative);
            s += (i ? ", " : "") + std::string(b);
        }
        s += ".";
    }
    const auto d = doubtful();
    if (!d.empty()) {
        s += i18n::tr(" Je ne l'apprends pas : ");
        for (std::size_t i = 0; i < d.size() && i < 3; ++i) {
            std::snprintf(b, sizeof b, "%s (%d)", i18n::tr(d[i]->label).c_str(), d[i]->seen);
            s += (i ? ", " : "") + std::string(b);
        }
        s += i18n::tr(" : inaudible, hésitant ou contre les règles du mastering.");
    }
    return s;
}

bool saveUsageGraph(const std::string& path, const UsageGraph& g) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << g.serialize();
    return static_cast<bool>(out);
}

UsageGraph loadUsageGraph(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    auto g = UsageGraph::parse(ss.str());
    return g ? *g : UsageGraph{};
}

} // namespace omnis::ai
