// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/HabitLearning.cpp
// =============================================================================
#include "HabitLearning.h"

#include "core/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>

namespace omnis::ai {

namespace {

constexpr const char* kKindNames[] = {"AUTO", "RETOUCHE", "ANNULE", "EXPORT"};

std::vector<std::string> splitTabs(std::string_view line) {
    std::vector<std::string> out;
    std::size_t a = 0;
    while (true) {
        const auto t = line.find('\t', a);
        out.emplace_back(line.substr(a, t == std::string_view::npos ? std::string_view::npos : t - a));
        if (t == std::string_view::npos) break;
        a = t + 1;
    }
    return out;
}

std::vector<double> numbers(const std::string& field) {
    std::vector<double> out;
    std::istringstream in(field);
    std::string v;
    while (std::getline(in, v, ',')) out.push_back(std::atof(v.c_str()));
    return out;
}

std::string joinNumbers(const double* v, std::size_t n, const char* fmt = "%.4g") {
    std::string out;
    char b[40];
    for (std::size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof b, fmt, v[i]);
        if (i) out += ',';
        out += b;
    }
    return out;
}

} // namespace

const char* habitKey(Habit h) noexcept {
    static constexpr const char* k[] = {"tilt", "sub", "inflate", "volume", "largeur", "comp"};
    return k[static_cast<int>(h)];
}

const char* habitLabel(Habit h) noexcept {
    static constexpr const char* k[] = {"pente (tilt)", "sub", "Inflator", "volume", "largeur", "seuil du Comp"};
    return k[static_cast<int>(h)];
}

double habitBound(Habit h) noexcept {
    static constexpr double k[] = {2.0, 2.0, 15.0, 2.0, 20.0, 6.0};
    return k[static_cast<int>(h)];
}

// ---------------------------------------------------------------- journal
std::string serializeJournalEvent(const JournalEvent& e) {
    std::string out = "v" + std::to_string(JournalEvent::kVersion) + "\t" + kKindNames[static_cast<int>(e.kind)] + "\t" + std::to_string(e.time) + "\t";
    // Le groupe ne contient ni tabulation ni retour à la ligne (nettoyé ici par prudence).
    for (char c : e.group) out += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    out += "\t" + joinNumbers(e.features.data(), e.features.size()) + "\t" + joinNumbers(e.values.data(), e.values.size());
    return out;
}

std::optional<JournalEvent> parseJournalEvent(std::string_view line) {
    if (line.empty() || line.front() == '#') return std::nullopt;
    const auto f = splitTabs(line);
    if (f.size() != 6 || f[0] != "v1") return std::nullopt;
    JournalEvent e;
    int kind = -1;
    for (int k = 0; k < 4; ++k)
        if (f[1] == kKindNames[k]) kind = k;
    if (kind < 0) return std::nullopt;
    e.kind = static_cast<JournalKind>(kind);
    e.time = std::atoll(f[2].c_str());
    e.group = f[3];
    e.features = numbers(f[4]);
    const auto v = numbers(f[5]);
    if (v.size() != static_cast<std::size_t>(kHabits)) return std::nullopt;
    for (int i = 0; i < kHabits; ++i) {
        if (!std::isfinite(v[static_cast<std::size_t>(i)])) return std::nullopt;
        e.values[static_cast<std::size_t>(i)] = v[static_cast<std::size_t>(i)];
    }
    for (double x : e.features)
        if (!std::isfinite(x)) return std::nullopt;
    return e;
}

bool appendJournal(const std::string& path, const JournalEvent& e) {
    std::ofstream out(path, std::ios::app | std::ios::binary);
    if (!out) return false;
    out << serializeJournalEvent(e) << '\n';
    return static_cast<bool>(out);
}

std::vector<JournalEvent> loadJournal(const std::string& path) {
    std::vector<JournalEvent> out;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (auto e = parseJournalEvent(line)) out.push_back(std::move(*e));
    }
    return out;
}

std::vector<HabitSample> samplesFromJournal(const std::vector<JournalEvent>& journal) {
    std::vector<HabitSample> out;
    for (const auto& e : journal)
        if (e.kind == JournalKind::Retouch && !e.features.empty()) {
            HabitSample s;
            s.features = e.features;
            for (int i = 0; i < kHabits; ++i) {
                const auto k = static_cast<std::size_t>(i);
                s.deltas[k] = std::clamp(e.values[k], -habitBound(static_cast<Habit>(i)), habitBound(static_cast<Habit>(i)));
            }
            out.push_back(std::move(s));
        }
    return out;
}

// ---------------------------------------------------------------- réseau
std::size_t HabitNet::weightCount() const noexcept { return w1_.size() + b1_.size() + w2_.size() + b2_.size() + w3_.size(); }

std::array<double, kHabits> HabitNet::forward(const std::vector<double>& x, std::array<double, kHidden>* hidden) const {
    std::array<double, kHidden> h{};
    for (int j = 0; j < kHidden; ++j) {
        double a = b1_[static_cast<std::size_t>(j)];
        for (int i = 0; i < inputs_; ++i) a += w1_[static_cast<std::size_t>(j * inputs_ + i)] * x[static_cast<std::size_t>(i)];
        h[static_cast<std::size_t>(j)] = std::tanh(a);
    }
    std::array<double, kHabits> y{};
    for (int o = 0; o < kHabits; ++o) {
        double a = b2_[static_cast<std::size_t>(o)];
        for (int j = 0; j < kHidden; ++j) a += w2_[static_cast<std::size_t>(o * kHidden + j)] * h[static_cast<std::size_t>(j)];
        for (int i = 0; i < inputs_; ++i) a += w3_[static_cast<std::size_t>(o * inputs_ + i)] * x[static_cast<std::size_t>(i)];
        y[static_cast<std::size_t>(o)] = a;
    }
    if (hidden) *hidden = h;
    return y;
}

bool HabitNet::train(const std::vector<HabitSample>& samples, int epochs) {
    if (samples.size() < 3) return false;
    inputs_ = static_cast<int>(samples.front().features.size());
    if (inputs_ <= 0) return false;
    for (const auto& s : samples)
        if (static_cast<int>(s.features.size()) != inputs_) return false;
    const auto n = static_cast<double>(samples.size());
    // Réduction des entrées sur les exemples.
    mean_.assign(static_cast<std::size_t>(inputs_), 0.0);
    sd_.assign(static_cast<std::size_t>(inputs_), 0.0);
    for (const auto& s : samples)
        for (int i = 0; i < inputs_; ++i) mean_[static_cast<std::size_t>(i)] += s.features[static_cast<std::size_t>(i)] / n;
    for (const auto& s : samples)
        for (int i = 0; i < inputs_; ++i) {
            const double v = s.features[static_cast<std::size_t>(i)] - mean_[static_cast<std::size_t>(i)];
            sd_[static_cast<std::size_t>(i)] += v * v / n;
        }
    for (auto& v : sd_) v = std::sqrt(v) > 1e-3 ? std::sqrt(v) : 1.0;
    std::vector<std::vector<double>> X;
    std::vector<std::array<double, kHabits>> Y;
    for (const auto& s : samples) {
        std::vector<double> x(static_cast<std::size_t>(inputs_));
        for (int i = 0; i < inputs_; ++i)
            x[static_cast<std::size_t>(i)] = (s.features[static_cast<std::size_t>(i)] - mean_[static_cast<std::size_t>(i)]) / sd_[static_cast<std::size_t>(i)];
        X.push_back(std::move(x));
        std::array<double, kHabits> y{};
        for (int o = 0; o < kHabits; ++o) y[static_cast<std::size_t>(o)] = s.deltas[static_cast<std::size_t>(o)] / habitBound(static_cast<Habit>(o));
        Y.push_back(y);
    }
    // Initialisation déterministe (Glorot uniforme, graine fixe).
    std::mt19937 rng(7);
    const auto init = [&](std::vector<double>& w, std::size_t count, double fanIn, double fanOut) {
        const double r = std::sqrt(6.0 / (fanIn + fanOut));
        std::uniform_real_distribution<double> u(-r, r);
        w.resize(count);
        for (auto& v : w) v = u(rng);
    };
    init(w1_, static_cast<std::size_t>(kHidden * inputs_), inputs_, kHidden);
    init(w2_, static_cast<std::size_t>(kHabits * kHidden), kHidden, kHabits);
    b1_.assign(kHidden, 0.0);
    b2_.assign(kHabits, 0.0);
    w3_.assign(static_cast<std::size_t>(kHabits * inputs_), 0.0); // le chemin direct part de zéro
    // Adam, lot complet, erreur quadratique + L2 (petit jeu : il ne faut pas apprendre par cœur).
    struct Adam {
        std::vector<double> m, v;
    };
    std::vector<double>* params[] = {&w1_, &b1_, &w2_, &b2_, &w3_};
    Adam opt[5];
    for (int p = 0; p < 5; ++p) opt[p] = {std::vector<double>(params[p]->size(), 0.0), std::vector<double>(params[p]->size(), 0.0)};
    const double lr = 0.01, beta1 = 0.9, beta2 = 0.999, eps = 1e-8, l2 = 1e-3; // réglé par balayage (tests/TestHabitLearning.cpp) // L2 forte : peu d'exemples
    std::vector<double> g[5];
    for (int epoch = 1; epoch <= epochs; ++epoch) {
        for (int p = 0; p < 5; ++p) g[p].assign(params[p]->size(), 0.0);
        for (std::size_t s = 0; s < X.size(); ++s) {
            std::array<double, kHidden> h{};
            const auto y = forward(X[s], &h);
            std::array<double, kHabits> dy{};
            for (int o = 0; o < kHabits; ++o) dy[static_cast<std::size_t>(o)] = 2.0 * (y[static_cast<std::size_t>(o)] - Y[s][static_cast<std::size_t>(o)]) / n;
            std::array<double, kHidden> dh{};
            for (int o = 0; o < kHabits; ++o) {
                g[3][static_cast<std::size_t>(o)] += dy[static_cast<std::size_t>(o)];
                for (int i = 0; i < inputs_; ++i) g[4][static_cast<std::size_t>(o * inputs_ + i)] += dy[static_cast<std::size_t>(o)] * X[s][static_cast<std::size_t>(i)];
                for (int j = 0; j < kHidden; ++j) {
                    g[2][static_cast<std::size_t>(o * kHidden + j)] += dy[static_cast<std::size_t>(o)] * h[static_cast<std::size_t>(j)];
                    dh[static_cast<std::size_t>(j)] += dy[static_cast<std::size_t>(o)] * w2_[static_cast<std::size_t>(o * kHidden + j)];
                }
            }
            for (int j = 0; j < kHidden; ++j) {
                const double da = dh[static_cast<std::size_t>(j)] * (1.0 - h[static_cast<std::size_t>(j)] * h[static_cast<std::size_t>(j)]);
                g[1][static_cast<std::size_t>(j)] += da;
                for (int i = 0; i < inputs_; ++i) g[0][static_cast<std::size_t>(j * inputs_ + i)] += da * X[s][static_cast<std::size_t>(i)];
            }
        }
        for (int p = 0; p < 5; ++p) {
            auto& w = *params[p];
            for (std::size_t k = 0; k < w.size(); ++k) {
                const double grad = g[p][k] + (p % 2 == 0 ? l2 * w[k] : 0.0); // poids (0, 2, 4) régularisés, biais libres
                opt[p].m[k] = beta1 * opt[p].m[k] + (1 - beta1) * grad;
                opt[p].v[k] = beta2 * opt[p].v[k] + (1 - beta2) * grad * grad;
                const double mh = opt[p].m[k] / (1 - std::pow(beta1, epoch)), vh = opt[p].v[k] / (1 - std::pow(beta2, epoch));
                w[k] -= lr * mh / (std::sqrt(vh) + eps);
            }
        }
    }
    samples_ = static_cast<int>(samples.size());
    return true;
}

HabitVector HabitNet::predict(const std::vector<double>& features) const {
    HabitVector out{};
    if (!trained() || static_cast<int>(features.size()) != inputs_) return out;
    std::vector<double> x(static_cast<std::size_t>(inputs_));
    for (int i = 0; i < inputs_; ++i) {
        // Une prod très loin de tout ce qui a été appris : on ne extrapole pas (borné à ±3 écarts-types).
        x[static_cast<std::size_t>(i)] = std::clamp((features[static_cast<std::size_t>(i)] - mean_[static_cast<std::size_t>(i)]) / sd_[static_cast<std::size_t>(i)], -3.0, 3.0);
    }
    const auto y = forward(x, nullptr);
    const double confidence = samples_ / (samples_ + 8.0);
    for (int o = 0; o < kHabits; ++o) {
        const auto k = static_cast<std::size_t>(o);
        out[k] = std::clamp(y[k], -1.0, 1.0) * habitBound(static_cast<Habit>(o)) * confidence;
    }
    return out;
}

std::string HabitNet::serialize() const {
    std::string out = "# OMNIS — réseau des habitudes (appris sur ce poste)\nv1\n";
    out += "entrees " + std::to_string(inputs_) + " exemples " + std::to_string(samples_) + "\n";
    out += "moyenne " + joinNumbers(mean_.data(), mean_.size(), "%.9g") + "\n";
    out += "ecart " + joinNumbers(sd_.data(), sd_.size(), "%.9g") + "\n";
    out += "w1 " + joinNumbers(w1_.data(), w1_.size(), "%.9g") + "\n";
    out += "b1 " + joinNumbers(b1_.data(), b1_.size(), "%.9g") + "\n";
    out += "w2 " + joinNumbers(w2_.data(), w2_.size(), "%.9g") + "\n";
    out += "b2 " + joinNumbers(b2_.data(), b2_.size(), "%.9g") + "\n";
    out += "w3 " + joinNumbers(w3_.data(), w3_.size(), "%.9g") + "\n";
    return out;
}

std::optional<HabitNet> HabitNet::parse(std::string_view text) {
    HabitNet net;
    std::istringstream in{std::string(text)};
    std::string line;
    bool version = false;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line == "v1") {
            version = true;
            continue;
        }
        const auto sp = line.find(' ');
        const std::string key = line.substr(0, sp), rest = sp == std::string::npos ? std::string() : line.substr(sp + 1);
        if (key == "entrees") {
            std::istringstream r(rest);
            std::string word;
            r >> net.inputs_ >> word >> net.samples_;
        } else if (key == "moyenne") net.mean_ = numbers(rest);
        else if (key == "ecart") net.sd_ = numbers(rest);
        else if (key == "w1") net.w1_ = numbers(rest);
        else if (key == "b1") net.b1_ = numbers(rest);
        else if (key == "w2") net.w2_ = numbers(rest);
        else if (key == "b2") net.b2_ = numbers(rest);
        else if (key == "w3") net.w3_ = numbers(rest);
    }
    const auto n = static_cast<std::size_t>(std::max(0, net.inputs_));
    if (!version || n == 0 || net.mean_.size() != n || net.sd_.size() != n || net.w1_.size() != n * kHidden || net.b1_.size() != kHidden ||
        net.w2_.size() != static_cast<std::size_t>(kHidden * kHabits) || net.b2_.size() != static_cast<std::size_t>(kHabits) || net.w3_.size() != n * kHabits || net.samples_ <= 0)
        return std::nullopt;
    return net;
}

std::string exportAnonymous(const std::vector<JournalEvent>& journal) {
    std::string out = "# OMNIS — export anonyme : groupe, grandeurs de la prod, écarts gardés (aucun audio, aucun nom)\n";
    out += "# jour\tgroupe\tgrandeurs\tecarts(tilt,sub,inflate,volume,largeur,comp)\n";
    for (const auto& e : journal) {
        if (e.kind != JournalKind::Retouch) continue;
        out += std::to_string(e.time / 86400 * 86400) + "\t";
        for (char c : e.group) out += (c == '\t' || c == '\n') ? ' ' : c;
        out += "\t" + joinNumbers(e.features.data(), e.features.size()) + "\t" + joinNumbers(e.values.data(), e.values.size()) + "\n";
    }
    return out;
}

std::string describeHabits(const HabitVector& deltas, int samples) {
    std::string parts;
    char b[64];
    for (int o = 0; o < kHabits; ++o) {
        const auto h = static_cast<Habit>(o);
        const double v = deltas[static_cast<std::size_t>(o)];
        // Seulement ce qui s'entend : plus de 10 % de la borne.
        if (std::fabs(v) < 0.1 * habitBound(h)) continue;
        const char* unit = (h == Habit::Inflate || h == Habit::Width) ? " %" : " dB";
        std::snprintf(b, sizeof b, "%s %+.1f%s", i18n::tr(habitLabel(h)).c_str(), v, unit);
        parts += (parts.empty() ? "" : ", ") + std::string(b);
    }
    if (parts.empty()) return {};
    std::snprintf(b, sizeof b, i18n::tr(" (appris sur %d séance(s))").c_str(), samples);
    return i18n::tr("ton habitude : ") + parts + b;
}

} // namespace omnis::ai
