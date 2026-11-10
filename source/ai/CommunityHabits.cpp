// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/CommunityHabits.cpp
// =============================================================================
#include "CommunityHabits.h"

#include "EmbeddedData.h"
#include "core/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

namespace omnis::ai {

namespace {

std::vector<std::string> splitTabs(const std::string& l) {
    std::vector<std::string> f;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= l.size(); ++i)
        if (i == l.size() || l[i] == '\t') {
            f.push_back(l.substr(start, i - start));
            start = i + 1;
        }
    return f;
}

std::string clean(std::string s) {
    for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

/// « sub+ » -> (Sub, +1) ; un module, un caractère : rien.
bool continuousGesture(const std::string& id, Habit& h, double& sign) {
    if (id.size() < 2 || (id.back() != '+' && id.back() != '-')) return false;
    const std::string key = id.substr(0, id.size() - 1);
    for (int o = 0; o < kHabits; ++o)
        if (key == habitKey(static_cast<Habit>(o))) {
            h = static_cast<Habit>(o);
            sign = id.back() == '+' ? 1.0 : -1.0;
            return true;
        }
    return false;
}

} // namespace

std::string genericContext(std::string_view contextId) {
    if (contextId.rfind("ctx:auto:", 0) == 0) return "auto:" + std::string(contextId.substr(9));
    if (contextId.rfind("ctx:preset:style ", 0) == 0) return "style:" + std::string(contextId.substr(17));
    return {}; // un preset « titre » : le nom d'une référence, propre à la personne
}

std::string contributionText(const UsageGraph& g) {
    std::ostringstream o;
    o << "omnis-contribution\tv1\tseances\t" << g.sessions() << "\n";
    for (const auto& e : g.edges()) {
        const std::string ctx = genericContext(e.a);
        const auto* n = g.find(e.b);
        if (ctx.empty() || !n || n->context || e.count <= 0) continue;
        const int creative = std::min(e.useful, n->creative);
        char mean[32];
        std::snprintf(mean, sizeof mean, "%.3f", n->useful > 0 ? n->amount / n->useful : 0.0);
        o << "G\t" << clean(ctx) << "\t" << clean(n->id) << "\t" << e.count << "\t" << e.useful << "\t" << creative << "\t" << mean << "\n";
    }
    return o.str();
}

std::optional<std::vector<ContributionRow>> parseContribution(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::string line;
    if (!std::getline(in, line)) return std::nullopt;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto f = splitTabs(line);
    if (f.size() < 4 || f[0] != "omnis-contribution" || f[1] != "v1") return std::nullopt;
    std::vector<ContributionRow> rows;
    try {
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            f = splitTabs(line);
            if (f.size() != 7 || f[0] != "G" || f[1].empty() || f[2].empty()) return std::nullopt;
            ContributionRow r{f[1], f[2], std::stoi(f[3]), std::stoi(f[4]), std::stoi(f[5]), std::stod(f[6])};
            if (r.seen < 0 || r.useful < 0 || r.useful > r.seen || r.creative < 0 || r.creative > r.useful || !std::isfinite(r.mean) ||
                std::fabs(r.mean) > 1000.0 || r.seen > 100000)
                return std::nullopt;
            rows.push_back(std::move(r));
        }
    } catch (...) {
        return std::nullopt;
    }
    return rows;
}

CommunityHabits CommunityHabits::aggregate(const std::vector<std::vector<ContributionRow>>& contributions) {
    struct Sum {
        int people = 0;
        double mean = 0.0;
    };
    std::map<std::pair<std::string, std::string>, Sum> sums;
    std::map<std::string, int> peopleInContext;
    for (const auto& person : contributions) {
        std::set<std::string> contexts;
        std::set<std::pair<std::string, std::string>> counted; // une voix par personne
        for (const auto& r : person) {
            if (r.seen > 0) contexts.insert(r.context);
            // Une habitude dans ce contexte : revenue (2 fois au moins) et utile la plupart du temps.
            if (r.useful < 2 || r.useful < 0.6 * r.seen) continue;
            if (!counted.insert({r.context, r.gesture}).second) continue;
            auto& s = sums[{r.context, r.gesture}];
            ++s.people;
            s.mean += r.mean;
        }
        for (const auto& c : contexts) ++peopleInContext[c];
    }
    CommunityHabits out;
    out.contributions_ = static_cast<int>(contributions.size());
    for (const auto& [key, s] : sums) {
        const int all = peopleInContext[key.first];
        if (s.people < kMinPeople || all <= 0) continue; // trop peu de monde : rien de publié
        const double share = static_cast<double>(s.people) / all;
        if (share < kMinShare) continue;
        out.habits_.push_back({key.first, key.second, s.people, share, s.mean / s.people});
    }
    std::sort(out.habits_.begin(), out.habits_.end(), [](const CommunityHabit& a, const CommunityHabit& b) {
        return a.context != b.context ? a.context < b.context : a.people > b.people;
    });
    return out;
}

std::string CommunityHabits::serialize() const {
    std::ostringstream o;
    o << "# OMNIS — ce que font les utilisateurs (moyenne de contributions anonymes, généré :\n"
         "# omnis_refdb usages). Un geste n'apparaît que s'il est une habitude chez au moins "
      << kMinPeople << "\n# personnes dans ce contexte. Ne pas modifier à la main.\n";
    o << "omnis-communaute\tv1\tcontributions\t" << contributions_ << "\n";
    for (const auto& h : habits_) {
        char b[96];
        std::snprintf(b, sizeof b, "%d\t%.3f\t%.3f", h.people, h.share, h.mean);
        o << "H\t" << clean(h.context) << "\t" << clean(h.gesture) << "\t" << b << "\n";
    }
    return o.str();
}

std::optional<CommunityHabits> CommunityHabits::parse(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::string line;
    CommunityHabits out;
    bool header = false;
    try {
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            const auto f = splitTabs(line);
            if (!header) {
                if (f.size() < 4 || f[0] != "omnis-communaute" || f[1] != "v1") return std::nullopt;
                out.contributions_ = std::stoi(f[3]);
                header = true;
                continue;
            }
            if (f.size() != 6 || f[0] != "H") return std::nullopt;
            CommunityHabit h{f[1], f[2], std::stoi(f[3]), std::stod(f[4]), std::stod(f[5])};
            if (h.people < kMinPeople || !(h.share > 0.0 && h.share <= 1.0) || !std::isfinite(h.mean)) return std::nullopt;
            out.habits_.push_back(std::move(h));
        }
    } catch (...) {
        return std::nullopt;
    }
    if (!header) return std::nullopt;
    return out;
}

const CommunityHabits& CommunityHabits::shipped() {
    static const CommunityHabits c = [] {
        auto p = parse(data::communaute());
        return p ? *p : CommunityHabits{};
    }();
    return c;
}

HabitVector CommunityHabits::delta(std::string_view context, double personalConfidence) const {
    HabitVector d{};
    const double weight = kMaxWeight * std::clamp(1.0 - personalConfidence, 0.0, 1.0);
    for (const auto& h : habits_) {
        if (h.context != context) continue;
        Habit which{};
        double sign = 0.0;
        if (!continuousGesture(h.gesture, which, sign)) continue;
        d[static_cast<std::size_t>(which)] += sign * std::fabs(h.mean) * h.share * weight;
    }
    for (int o = 0; o < kHabits; ++o) {
        const double bound = 0.5 * habitBound(static_cast<Habit>(o));
        auto& v = d[static_cast<std::size_t>(o)];
        v = std::clamp(v, -bound, bound);
    }
    return d;
}

std::string describeCommunity(const HabitVector& d, std::string_view context, int people) {
    std::string parts;
    char b[200];
    for (int o = 0; o < kHabits; ++o) {
        const auto h = static_cast<Habit>(o);
        const double v = d[static_cast<std::size_t>(o)];
        if (std::fabs(v) < 0.1 * habitBound(h)) continue; // seulement ce qui s'entend
        const char* unit = (h == Habit::Inflate || h == Habit::Width) ? " %" : " dB";
        std::snprintf(b, sizeof b, "%s %+.1f%s", i18n::tr(habitLabel(h)).c_str(), v, unit);
        parts += (parts.empty() ? "" : ", ") + std::string(b);
    }
    if (parts.empty()) return {};
    std::string name(context);
    if (const auto colon = name.find(':'); colon != std::string::npos) name = name.substr(colon + 1);
    std::snprintf(b, sizeof b, i18n::tr("ce que font les autres en %s (%d personnes) : ").c_str(), i18n::tr(name).c_str(), people);
    return b + parts;
}

int CommunityHabits::peopleIn(std::string_view context) const {
    int n = 0;
    for (const auto& h : habits_)
        if (h.context == context) n = std::max(n, static_cast<int>(std::lround(h.people / h.share)));
    return n;
}

} // namespace omnis::ai
