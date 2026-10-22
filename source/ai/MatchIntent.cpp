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
constexpr double kMaxWordDb = 4.0;

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

/// « fort » vise la sonie de la référence sans la dépasser ; au-delà de +2
/// (« club », « très fort »), un demi-LU par LU ; « dynamique » la réduit.
double loudnessAdjust(double offset) { return std::min(0.0, offset) + 0.5 * std::max(0.0, offset - 2.0); }

std::string format(const char* fmt, double a, double b = 0.0, double c = 0.0) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), fmt, a, b, c);
    return buf;
}

} // namespace

int regionOfBand(int band) noexcept {
    for (int r = 0; r < kRegionCount; ++r)
        if (band >= kRegions[r].first && band <= kRegions[r].last) return r;
    return kRegionCount - 1;
}

MatchRequest requestFromInterpretation(const Interpretation& in) {
    MatchRequest r;
    const auto& d = in.desire;
    r.resemblance = std::clamp(0.5 + d[Axis::Resemblance], 0.2, 0.8);
    r.loudnessOffsetLu = std::clamp(d[Axis::Loudness], -6.0, 6.0);
    r.targetLufs = in.lufs;
    const double clean = d[Axis::Cleanliness];
    if (clean > 0.0) r.maxTonalDeviationDb = std::max(0.75, 1.5 - 0.5 * clean);
    if (clean < 0.0) r.maxTonalDeviationDb = std::min(4.0, 1.5 - 1.5 * clean);

    // Souhaits tonals : une valeur par région, adoucie entre régions voisines.
    // Les mots donnent une direction, pas une ampleur sans fin : trois
    // remarques sur les aigus ne font pas +12 dB (±4 dB par région).
    BandArray mid{}, side{};
    for (int k = 0; k < kBands; ++k)
        mid[static_cast<std::size_t>(k)] = std::clamp(d.v[static_cast<std::size_t>(regionOfBand(k))], -kMaxWordDb, kMaxWordDb);
    mid = smooth(mid);
    for (int k = 0; k < kBands; ++k) {
        const double f = hz(k);
        const double widthShare = f >= 640.0 ? 1.0 : (f >= 226.0 ? 0.5 : 0.0);
        side[static_cast<std::size_t>(k)] = mid[static_cast<std::size_t>(k)] + widthShare * std::clamp(d[Axis::Width], -kMaxWordDb, kMaxWordDb);
    }
    r.midBiasDb = mid;
    r.sideBiasDb = side;

    // Ce qui a été compris, en clair.
    for (const auto& hit : in.hits) {
        const Concept* c = Lexicon::builtin().find(hit.conceptId);
        std::string line = "« " + hit.text + " »";
        if (hit.protect) {
            line += " : protégé";
            if (c)
                for (int a = 0; a < kAxisCount; ++a)
                    if (std::fabs(c->effect.v[static_cast<std::size_t>(a)]) > 1e-9) {
                        if (a < kRegionCount) r.protectedRegions[static_cast<std::size_t>(a)] = true;
                        if (static_cast<Axis>(a) == Axis::Width) r.widthProtected = true;
                    }
            r.understood.push_back(line);
            continue;
        }
        line += format(" (%+.1f) ->", hit.weight);
        if (c) {
            bool first = true;
            for (int a = 0; a < kAxisCount; ++a) {
                const double v = hit.weight * c->effect.v[static_cast<std::size_t>(a)];
                if (std::fabs(v) < 1e-9) continue;
                line += std::string(first ? " " : ", ") + axisLabel(static_cast<Axis>(a)) + format(" %+.1f", v) +
                        (*axisUnit(static_cast<Axis>(a)) ? std::string(" ") + axisUnit(static_cast<Axis>(a)) : std::string());
                first = false;
            }
            if (c->targetLufs && hit.weight > 0.0) line += format(" sonie %.0f LUFS", *c->targetLufs);
        }
        r.understood.push_back(line);
    }
    if (r.protectedRegions[0]) {
        r.subLimitDb = 1.0;
        r.understood.push_back("Sub / 808 protégés : ±1 dB au maximum sous 100 Hz");
    }
    if (in.lufs) r.understood.push_back(format("Sonie demandée : %.1f LUFS", *in.lufs));
    if (std::fabs(d[Axis::Resemblance]) > 1e-9) r.understood.push_back(format("Ressemblance : %.0f %% de l'écart", 100.0 * r.resemblance));
    if (std::fabs(clean) > 1e-9) r.understood.push_back(format("Tolérance tonale de la dynamique : %.1f dB", r.maxTonalDeviationDb));
    for (int a = 0; a < kAxisCount; ++a) {
        const auto axis = static_cast<Axis>(a);
        if (!axisApplied(axis) && std::fabs(d[axis]) >= 0.25)
            r.pending.push_back(std::string(axisLabel(axis)) + format(" %+.1f : noté, appliqué par l'étage dynamique / harmonique à venir", d[axis]));
    }
    return r;
}

MatchRequest parseRequest(std::string_view text) { return requestFromInterpretation(interpret(text, Lexicon::builtin())); }

Understanding understandRequest(std::string_view text, const Lexicon& lexicon, const ReferenceLibrary& library, bool audioReferenceProvided) {
    Understanding u;
    u.interpretation = interpret(text, lexicon);
    auto& in = u.interpretation;

    const auto addEntries = [&](const ReferenceMatch& m) {
        for (const auto* e : m.entries)
            if (std::find(u.referenceEntries.begin(), u.referenceEntries.end(), e) == u.referenceEntries.end()) u.referenceEntries.push_back(e);
        u.referenceName += (u.referenceName.empty() ? "" : " + ") + m.how;
    };
    for (const auto& mention : in.referenceMentions) {
        const auto m = library.find(mention);
        if (m.found()) addEntries(m);
        else u.unknownReferences.push_back(mention);
    }
    // Un artiste, un titre ou un genre cité sans « comme » (« un son rage bien propre »).
    std::vector<std::string> stillUnknown;
    for (std::size_t i = 0; i < in.unknownWords.size(); ++i) {
        if (i + 1 < in.unknownWords.size()) {
            const auto pair = library.find(in.unknownWords[i] + " " + in.unknownWords[i + 1]);
            if (pair.found() && pair.how.rfind("titre", 0) == 0) {
                addEntries(pair);
                ++i;
                continue;
            }
        }
        const auto m = library.find(in.unknownWords[i]);
        if (m.found()) addEntries(m);
        else stillUnknown.push_back(in.unknownWords[i]);
    }
    in.unknownWords = stillUnknown;

    u.request = requestFromInterpretation(in);
    if (!u.referenceName.empty()) u.request.understood.push_back("Référence : " + u.referenceName);

    // Ce qu'il faut demander avant d'agir.
    std::string known;
    for (const auto& a : library.artists()) known += (known.empty() ? "" : ", ") + a;
    if (!u.unknownReferences.empty() && !audioReferenceProvided) {
        std::string names;
        for (const auto& n : u.unknownReferences) names += (names.empty() ? "« " : ", « ") + n + " »";
        u.question = "Je ne connais pas encore " + names +
                     ". Pour viser juste : cite-moi un artiste proche que je connais (" + (known.empty() ? "base vide" : known) +
                     "), ou glisse-moi un son de cet artiste : je l'écouterai et je l'ajouterai à la base.";
    } else if (in.mentionsProvidedReference && !audioReferenceProvided && !u.hasReference()) {
        u.question = "Tu parles d'une référence : glisse-moi le son, je l'écoute avant de toucher à ta prod.";
    } else if (in.hits.empty() && !in.lufs && !u.hasReference() && !audioReferenceProvided) {
        u.question = "Je n'ai pas compris ce que tu veux changer. Donne-moi un artiste ou un titre (je connais : " +
                     (known.empty() ? "base vide" : known) +
                     "), ou dis-moi ce qui ne va pas : « ça manque de crispy », « trop de basse », « je veux que ce soit propre »…";
    }
    return u;
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
        double lo = sub ? -request.subLimitDb : -kMaxCutDb;
        double hi = sub ? request.subLimitDb : kMaxBoostDb;
        if (request.protectedRegions[static_cast<std::size_t>(regionOfBand(k))]) {
            lo = std::max(lo, -1.0);
            hi = std::min(hi, 1.0);
        }
        if (prod.relativeMidDb(k) < kEmptyBandDb) hi = 0.0; // rien à faire ressortir
        const double mid = std::clamp(r * intent.gapDb[i], lo, hi);

        double width = 0.0;
        if (hz(k) >= kMonoBelowHz) {
            double wHi = kSideLimitDb;
            if (prod.widthDb(k) < kEmptyBandDb + 5.0) wHi = 0.0; // bande quasi mono
            const double wLimit = request.widthProtected ? 1.0 : kSideLimitDb;
            width = std::clamp(r * intent.widthGapDb[i], -wLimit, std::min(wHi, wLimit));
        }
        // Une région protégée l'est aussi des souhaits contradictoires (« trop
        // de basse … mais garde la 808 » : la 808 l'emporte, ±1 dB).
        const double limit = request.protectedRegions[static_cast<std::size_t>(regionOfBand(k))] ? 1.0 : 12.0;
        intent.midDb[i] = std::clamp(mid + request.midBiasDb[i], -limit, limit);
        intent.sideDb[i] = std::clamp(mid + width + request.sideBiasDb[i], -limit, limit);
    }
    intent.targetLufs = std::clamp(request.targetLufs.value_or(ref.integratedLufs + loudnessAdjust(request.loudnessOffsetLu)), -16.0, -3.0);

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

MatchIntent planFromWords(const SpectralSignature& prod, const MatchRequest& request) {
    MatchIntent intent;
    for (int k = 0; k < kBands; ++k) {
        const auto i = static_cast<std::size_t>(k);
        double hi = 6.0;
        if (prod.relativeMidDb(k) < kEmptyBandDb) hi = 0.0; // rien à faire ressortir
        const double limit = request.protectedRegions[static_cast<std::size_t>(regionOfBand(k))] ? 1.0 : 6.0;
        intent.midDb[i] = std::clamp(request.midBiasDb[i], -limit, std::min(hi, limit));
        intent.sideDb[i] = std::clamp(request.sideBiasDb[i], -limit, std::min(hi, limit));
    }
    intent.targetLufs = std::clamp(request.targetLufs.value_or(-10.0 + request.loudnessOffsetLu), -16.0, -3.0);
    intent.referenceTraits.push_back("Pas de référence : je ne change que ce que tu as demandé");
    intent.protectedTraits.push_back("Tout le reste de ta prod est laissé tel quel");
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
