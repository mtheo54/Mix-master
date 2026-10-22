// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/MatchIntent.cpp
// =============================================================================
#include "MatchIntent.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace omnis::ai {

namespace {

constexpr int kBands = params::kMatchBands;
constexpr double kMaxResemblance = 0.85;
constexpr double kMonoBelowHz = 120.0;
constexpr double kMaxCutDb = 6.0;
constexpr double kMaxBoostDb = 4.0;
constexpr double kSideLimitDb = 4.0;
constexpr double kEmptyBandDb = -40.0;

struct Region {
    const char* name;
    int first, last; ///< nœuds inclus
};

// 20–80 Hz, 113–226, 320–640, 905–2560, 3.6–7.2 k, 10–20 k.
constexpr Region kRegions[] = {
    {"Sub (20–80 Hz)", 0, 4},        {"Bas (110–230 Hz)", 5, 7},     {"Bas-médium (320–640 Hz)", 8, 10},
    {"Médium (0.9–2.6 kHz)", 11, 14}, {"Présence (3.6–7 kHz)", 15, 17}, {"Air (10–20 kHz)", 18, 20},
};

double hz(int k) { return params::matchBandHz(k); }

BandArray smooth(const BandArray& v) {
    BandArray out{};
    for (int k = 0; k < kBands; ++k) {
        const double a = v[static_cast<std::size_t>(std::max(0, k - 1))];
        const double b = v[static_cast<std::size_t>(k)];
        const double c = v[static_cast<std::size_t>(std::min(kBands - 1, k + 1))];
        out[static_cast<std::size_t>(k)] = 0.25 * a + 0.5 * b + 0.25 * c;
    }
    return out;
}

double regionMean(const BandArray& v, const Region& r) {
    double s = 0.0;
    for (int k = r.first; k <= r.last; ++k) s += v[static_cast<std::size_t>(k)];
    return s / (r.last - r.first + 1);
}

std::string format(const char* fmt, double a, double b = 0.0, double c = 0.0) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), fmt, a, b, c);
    return buf;
}

// ---- Lecture de la demande ---------------------------------------------------

/// Minuscules ASCII et accents français retirés (UTF-8).
std::string normalize(std::string_view text) {
    static const std::pair<const char*, char> kAccents[] = {
        {"é", 'e'}, {"è", 'e'}, {"ê", 'e'}, {"ë", 'e'}, {"à", 'a'}, {"â", 'a'}, {"ç", 'c'}, {"î", 'i'},
        {"ï", 'i'}, {"ô", 'o'}, {"ù", 'u'}, {"û", 'u'}, {"É", 'e'}, {"È", 'e'}, {"À", 'a'}, {"’", '\''},
    };
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        bool replaced = false;
        for (const auto& [from, to] : kAccents) {
            const std::string_view f(from);
            if (text.substr(i, f.size()) == f) {
                out += to;
                i += f.size();
                replaced = true;
                break;
            }
        }
        if (replaced) continue;
        const char c = text[i++];
        out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

bool has(const std::string& text, std::initializer_list<const char*> words) {
    for (const char* w : words)
        if (text.find(w) != std::string::npos) return true;
    return false;
}

void addBias(BandArray& bias, double fromHz, double toHz, double db) {
    for (int k = 0; k < kBands; ++k)
        if (hz(k) >= fromHz && hz(k) <= toHz) bias[static_cast<std::size_t>(k)] += db;
}

/// « -8 lufs », « −8.5 LUFS », « 8 lufs » (sonie toujours négative).
std::optional<double> explicitLufs(const std::string& t) {
    const auto pos = t.find("lufs");
    if (pos == std::string::npos) return std::nullopt;
    std::size_t end = pos;
    while (end > 0 && t[end - 1] == ' ') --end;
    std::size_t start = end;
    while (start > 0 && (std::isdigit(static_cast<unsigned char>(t[start - 1])) || t[start - 1] == '.' || t[start - 1] == ','))
        --start;
    if (start == end) return std::nullopt;
    std::string number = t.substr(start, end - start);
    std::replace(number.begin(), number.end(), ',', '.');
    const double v = std::strtod(number.c_str(), nullptr);
    return -std::fabs(v);
}

} // namespace

MatchRequest parseRequest(std::string_view raw) {
    MatchRequest r;
    const std::string t = normalize(raw);
    auto note = [&](std::string s) { r.understood.push_back(std::move(s)); };

    // Ressemblance.
    if (has(t, {"exactement", "pareil", "identique", "copie", "le plus proche", "a fond comme"})) {
        r.resemblance = 0.8;
        note("Ressemblance forte (80 % de l'écart) : jamais une copie, la prod garde ses bornes");
    } else if (has(t, {"un peu", "legerement", "une touche", "subtil", "leger"})) {
        r.resemblance = 0.3;
        note("Ressemblance légère (30 % de l'écart)");
    } else {
        note("Ressemblance par défaut (50 % de l'écart)");
    }

    // Sonie.
    if (auto lufs = explicitLufs(t)) {
        r.targetLufs = *lufs;
        note(format("Sonie demandée : %.1f LUFS", *lufs));
    } else if (has(t, {"spotify", "streaming", "youtube", "apple music"})) {
        r.targetLufs = -14.0;
        note("Sonie streaming : -14 LUFS");
    } else if (has(t, {"dynamique", "respire", "nuance"})) {
        r.targetLufs = -12.0;
        note("Master dynamique : -12 LUFS");
    } else {
        note("Sonie : celle de la référence");
    }

    // Propreté / caractère.
    if (has(t, {"propre", "clean", "sans disto", "transparent"})) {
        r.maxTonalDeviationDb = 1.0;
        note("Propreté prioritaire : tolérance tonale 1.0 dB");
    } else if (has(t, {"sale", "agressif", "sature", "crade", "disto", "ecrase", "violent"})) {
        r.maxTonalDeviationDb = 3.0;
        note("Caractère agressif accepté : tolérance tonale 3.0 dB");
    }

    // Sub / 808.
    const bool aboutLow = has(t, {"808", "sub", "basse", "grave"});
    if (aboutLow && has(t, {"garde", "protege", "touche pas", "conserve", "laisse"})) {
        r.subLimitDb = 1.0;
        note("Sub / 808 protégés : déplacement limité à ±1 dB sous 100 Hz");
    } else if (aboutLow && has(t, {"moins de", "moins fort", "baisse", "trop de"})) {
        r.subLimitDb = 6.0;
        addBias(r.midBiasDb, 0.0, 100.0, -2.0);
        note("Moins de sub : -2 dB sous 100 Hz, bornes élargies à ±6 dB");
    } else if (aboutLow && has(t, {"plus de", "gros", "lourd", "tape", "enorme", "massif"})) {
        addBias(r.midBiasDb, 0.0, 100.0, 2.0);
        note("Sub plus présent : +2 dB sous 100 Hz");
    }

    // Couleur.
    if (has(t, {"brillant", "aerien", "de l'air", "clair", "ouvert"})) {
        addBias(r.midBiasDb, 7000.0, 21000.0, 1.5);
        addBias(r.sideBiasDb, 7000.0, 21000.0, 1.5);
        note("Plus brillant : +1.5 dB au-dessus de 7 kHz");
    } else if (has(t, {"sombre", "chaud", "moins agressif dans les aigus", "doux"})) {
        addBias(r.midBiasDb, 5000.0, 21000.0, -1.5);
        addBias(r.sideBiasDb, 5000.0, 21000.0, -1.5);
        note("Plus chaud : -1.5 dB au-dessus de 5 kHz");
    }

    // Largeur.
    if (has(t, {"large", "wide", "stereo", "ample"})) {
        addBias(r.sideBiasDb, 1500.0, 21000.0, 1.5);
        note("Plus large : Side +1.5 dB au-dessus de 1.5 kHz");
    } else if (has(t, {"mono", "serre", "centre", "etroit"})) {
        addBias(r.sideBiasDb, 120.0, 21000.0, -1.5);
        note("Plus serré : Side -1.5 dB");
    }
    return r;
}

MatchIntent planMatch(const SpectralSignature& prod, const SpectralSignature& ref, const MatchRequest& request) {
    MatchIntent intent;
    const double r = std::clamp(request.resemblance, 0.0, kMaxResemblance);

    BandArray gap{}, widthGap{};
    for (int k = 0; k < kBands; ++k) {
        gap[static_cast<std::size_t>(k)] = ref.relativeMidDb(k) - prod.relativeMidDb(k);
        widthGap[static_cast<std::size_t>(k)] = ref.widthDb(k) - prod.widthDb(k);
    }
    intent.gapDb = smooth(gap);
    intent.widthGapDb = smooth(widthGap);
    // Sous 80 Hz, seule l'énergie du sub compte (sa forme dépend de la note de la
    // 808) : un écart unique, en plateau.
    const double subGap = (ref.subEnergyDb - ref.bodyDb()) - (prod.subEnergyDb - prod.bodyDb());
    for (int k = 0; k < SpectralSignature::kSubBands; ++k) intent.gapDb[static_cast<std::size_t>(k)] = subGap;

    for (int k = 0; k < kBands; ++k) {
        const auto i = static_cast<std::size_t>(k);
        const bool sub = hz(k) <= 100.0;
        const double lo = sub ? -request.subLimitDb : -kMaxCutDb;
        double hi = sub ? request.subLimitDb : kMaxBoostDb;
        if (prod.relativeMidDb(k) < kEmptyBandDb) hi = 0.0; // rien à faire ressortir
        const double mid = std::clamp(r * intent.gapDb[i], lo, hi);

        double width = 0.0;
        if (hz(k) >= kMonoBelowHz) {
            double wHi = kSideLimitDb;
            if (prod.widthDb(k) < kEmptyBandDb + 5.0) wHi = 0.0; // bande quasi mono
            width = std::clamp(r * intent.widthGapDb[i], -kSideLimitDb, wHi);
        }
        intent.midDb[i] = std::clamp(mid + request.midBiasDb[i], -12.0, 12.0);
        intent.sideDb[i] = std::clamp(mid + width + request.sideBiasDb[i], -12.0, 12.0);
    }
    intent.targetLufs = request.targetLufs.value_or(ref.integratedLufs);

    // ---- Explication ----------------------------------------------------------
    for (const auto& region : kRegions) {
        const double g = regionMean(intent.gapDb, region);
        if (std::fabs(g) < 1.5) continue;
        intent.referenceTraits.push_back(std::string(region.name) +
                                         format(g > 0 ? " : %.1f dB plus présent que dans ta prod" : " : %.1f dB plus discret que dans ta prod",
                                                std::fabs(g)));
    }
    {
        double wRef = 0.0, wProd = 0.0;
        int count = 0;
        for (int k = 0; k < kBands; ++k)
            if (hz(k) >= 1000.0) {
                wRef += ref.widthDb(k);
                wProd += prod.widthDb(k);
                ++count;
            }
        const double dw = (wRef - wProd) / std::max(1, count);
        if (std::fabs(dw) >= 1.5)
            intent.referenceTraits.push_back(format(dw > 0 ? "Image stéréo plus large au-dessus de 1 kHz (Side %+.1f dB)"
                                                           : "Image stéréo plus serrée au-dessus de 1 kHz (Side %+.1f dB)",
                                                    dw));
    }
    intent.referenceTraits.push_back(format("Master à %.1f LUFS, LRA %.1f LU, PLR %.1f dB", ref.integratedLufs, ref.loudnessRangeLu, ref.plrDb));
    {
        const double sRef = regionMean(ref.midSpreadDb, kRegions[0]);
        const double sProd = regionMean(prod.midSpreadDb, kRegions[0]);
        if (sProd - sRef >= 3.0)
            intent.referenceTraits.push_back(format("Sub beaucoup plus tenu dans le temps (variation %.0f dB contre %.0f dB) : "
                                                    "compression / saturation de la 808, pas une question d'EQ",
                                                    sRef, sProd));
        const double mRef = regionMean(ref.midSpreadDb, kRegions[3]);
        const double mProd = regionMean(prod.midSpreadDb, kRegions[3]);
        if (mProd - mRef >= 3.0)
            intent.referenceTraits.push_back(format("Médiums plus denses (variation %.0f dB contre %.0f dB) : mix plus compressé", mRef, mProd));
    }

    for (const auto& region : kRegions) {
        double wanted = 0.0, applied = 0.0;
        for (int k = region.first; k <= region.last; ++k) {
            wanted += r * intent.gapDb[static_cast<std::size_t>(k)];
            applied += intent.midDb[static_cast<std::size_t>(k)] - request.midBiasDb[static_cast<std::size_t>(k)];
        }
        const int n = region.last - region.first + 1;
        wanted /= n;
        applied /= n;
        if (std::fabs(regionMean(intent.gapDb, region)) >= 1.5 && std::fabs(wanted - applied) >= 0.75)
            intent.protectedTraits.push_back(std::string(region.name) +
                                             format(" : la référence est à %+.1f dB, on ne bouge que de %+.1f dB (identité de ta prod)",
                                                    regionMean(intent.gapDb, region), applied));
    }
    intent.protectedTraits.push_back(format("Ressemblance : %.0f %% de l'écart, jamais la totalité", 100.0 * r));
    return intent;
}

double tonalDistanceDb(const SpectralSignature& a, const SpectralSignature& b) {
    double sum = 0.0;
    int count = 0;
    for (int k = 0; k < kBands; ++k) {
        if (a.relativeMidDb(k) < kEmptyBandDb && b.relativeMidDb(k) < kEmptyBandDb) continue;
        const double d = a.relativeMidDb(k) - b.relativeMidDb(k);
        sum += d * d;
        ++count;
    }
    return count > 0 ? std::sqrt(sum / count) : 0.0;
}

} // namespace omnis::ai
