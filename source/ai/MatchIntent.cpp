// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/MatchIntent.cpp
// =============================================================================
#include "MatchIntent.h"

#include "TextMatch.h"

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

namespace {

/// « a, b, c » ; au-delà de `max` noms : « a, b, c et 12 autres ».
std::string listNames(const std::vector<std::string>& names, std::size_t max = 3) {
    std::string out;
    for (std::size_t i = 0; i < names.size() && i < max; ++i) out += (i ? ", " : "") + names[i];
    if (names.size() > max) out += " et " + std::to_string(names.size() - max) + " autres";
    return out;
}

} // namespace

Understanding understandRequest(std::string_view text, const Lexicon& lexicon, const ReferenceLibrary& library, bool audioReferenceProvided) {
    Understanding u;
    using Kind = ReferenceMatch::Kind;

    // ---- 1. Les noms de la base, repérés AVANT la lecture des mots ----------
    // (« ken carsonn » ne doit pas devenir « carton »). Garde-fous, la base
    // pouvant compter des milliers de titres :
    //  - un mot du lexique ou un mot outil seul n'est jamais un nom ;
    //  - un nom reconnu avec une faute ne commence ni ne finit par un mot outil
    //    (« comme la » n'est pas « CALME ») ;
    //  - un titre d'un seul mot (« Intro », « Love ») n'est reconnu qu'après
    //    « comme », « genre », « titre »… ; les genres passent par la lecture.
    std::vector<std::string> tokens = tokenize(text);
    std::vector<ReferenceMatch> named;
    const auto lexiconWord = [&](const std::string& t) {
        for (const auto& c : lexicon.concepts())
            for (const auto& e : c.tokenized)
                if (e.size() == 1 && e[0] == t) return true;
        return isCommonWord(t);
    };
    const auto afterTrigger = [&](std::size_t i) {
        static const char* kTriggers[] = {"comme", "genre", "style", "facon", "type", "titre", "son", "morceau", "track", "sur", "like"};
        if (i == 0) return false;
        for (const char* w : kTriggers)
            if (tokens[i - 1] == w) return true;
        return false;
    };
    for (std::size_t i = 0; i < tokens.size();) {
        bool found = false;
        for (std::size_t len = std::min<std::size_t>(5, tokens.size() - i); len >= 1 && !found; --len) {
            std::string window;
            bool words = true;
            for (std::size_t k = i; k < i + len; ++k) {
                words = words && isWord(tokens[k]);
                window += (window.empty() ? "" : " ") + tokens[k];
            }
            if (!words || (len == 1 && lexiconWord(tokens[i]))) continue;
            const auto m = library.find(window);
            // Les genres d'un mot passent par la lecture normale (mots courts,
            // ambigus) ; un genre exact en plusieurs mots est repéré ici, sinon
            // « uk bass » se lisait « uk » + « basse ».
            if (!m.found() || (m.kind == Kind::Genre && (len < 2 || !m.exact))) continue;
            if (!m.exact && (isCommonWord(tokens[i]) || isCommonWord(tokens[i + len - 1]))) continue;
            if (m.kind == Kind::Title && len == 1 && !afterTrigger(i)) continue;
            named.push_back(m);
            for (std::size_t k = i; k < i + len; ++k) tokens[k] = kReferenceMarker;
            i += len;
            found = true;
        }
        if (!found) ++i;
    }
    std::string masked;
    for (const auto& t : tokens) masked += (masked.empty() ? "" : " ") + t;
    u.interpretation = interpret(named.empty() ? text : std::string_view(masked), lexicon);
    auto& in = u.interpretation;

    // ---- 2. Mentions après « comme », « genre »… et mots isolés -------------
    std::vector<ReferenceMatch> matches = named;
    for (const auto& mention : in.referenceMentions) {
        auto m = library.find(mention);
        if (!m.found()) {
            // « fein de travis scott » : un titre de l'artiste reconnu.
            for (auto& n : matches)
                if (n.kind == Kind::Artist)
                    if (auto t = library.findTitleOf(n.entries, mention); t.found()) {
                        n = t;
                        m = t;
                        break;
                    }
            if (m.found()) continue;
            u.unknownReferences.push_back(mention);
            continue;
        }
        matches.push_back(m);
    }
    std::vector<std::string> stillUnknown;
    for (std::size_t i = 0; i < in.unknownWords.size(); ++i) {
        if (i + 1 < in.unknownWords.size()) {
            const auto pair = library.find(in.unknownWords[i] + " " + in.unknownWords[i + 1]);
            if (pair.found() && pair.kind != Kind::Artist) { // titre ou genre en deux mots (« rap fr »)
                matches.push_back(pair);
                ++i;
                continue;
            }
        }
        const auto m = library.find(in.unknownWords[i]);
        // Un mot isolé ne vaut pas un titre (trop de titres d'un mot).
        if (m.found() && m.kind != Kind::Title) matches.push_back(m);
        else stillUnknown.push_back(in.unknownWords[i]);
    }
    in.unknownWords = stillUnknown;
    // « black skinhead de kanye » : le titre suffit, l'artiste seul est un doublon.
    matches.erase(std::remove_if(matches.begin(), matches.end(),
                                 [&](const ReferenceMatch& m) {
                                     if (m.kind != Kind::Artist) return false;
                                     return std::any_of(matches.begin(), matches.end(), [&](const ReferenceMatch& t) {
                                         return t.kind == Kind::Title && namesMatch(t.entries.front()->artist, m.entries.front()->artist);
                                     });
                                 }),
                  matches.end());

    // ---- 3. Ce que l'on peut vraiment écouter --------------------------------
    // Titre ou artiste mesurés : ses sons. Connus de nom seulement : les titres
    // mesurés de SES genres. Un genre : ses titres mesurés. Rien : question.
    std::vector<std::string> names, missing, missingGenres, profiles;
    const auto addMeasured = [&](const std::vector<const ReferenceEntry*>& list) {
        for (const auto* e : list)
            if (e->measured && std::find(u.referenceEntries.begin(), u.referenceEntries.end(), e) == u.referenceEntries.end())
                u.referenceEntries.push_back(e);
    };
    for (const auto& m : matches) {
        const std::string label = m.kind == Kind::Genre ? m.how.substr(6, m.how.find(" (") - 6)
                                : m.kind == Kind::Title ? "« " + m.entries.front()->title + " » de " + m.entries.front()->artist
                                                        : m.entries.front()->artist;
        std::vector<const ReferenceEntry*> heard;
        if (m.kind == Kind::Genre) heard = library.measuredWithGenre(label);
        else
            for (const auto* e : m.entries)
                if (e->measured) heard.push_back(e);
        if (!heard.empty()) {
            addMeasured(heard);
            const std::string count = std::to_string(heard.size()) + (heard.size() > 1 ? " titres écoutés" : " titre écouté");
            u.referenceName += std::string(u.referenceName.empty() ? "" : " + ") +
                               (m.kind == Kind::Genre ? "genre " + label + " (" + count + ")"
                                : m.kind == Kind::Artist ? "artiste " + label + " (" + count + ")"
                                                         : "titre " + m.entries.front()->displayName());
            continue;
        }
        if (m.kind == Kind::Genre) {
            missingGenres.push_back(label);
            continue;
        }
        // Connu de nom : genres de l'artiste (ou du titre).
        std::vector<std::string> tags;
        for (const auto* e : m.entries)
            for (const auto& g : e->genres)
                if (std::find(tags.begin(), tags.end(), g) == tags.end()) tags.push_back(g);
        std::vector<std::string> used;
        for (const auto& g : tags) {
            const auto list = library.measuredWithGenre(g);
            if (!list.empty()) {
                addMeasured(list);
                used.push_back(g);
            }
        }
        if (used.empty()) {
            missing.push_back(label);
            continue;
        }
        names.push_back(label);
        profiles.push_back(listNames(used));
        u.referenceName += std::string(u.referenceName.empty() ? "" : " + ") + "profil " + listNames(used) + " pour " + label;
    }
    if (!names.empty())
        u.suggestion = "Je connais " + listNames(names) + " de nom : en attendant, je vise le profil moyen " + listNames(profiles) +
                       ". Glisse-moi un son de " + listNames(names) + " pour viser juste.";

    u.request = requestFromInterpretation(in);
    if (!u.referenceName.empty()) u.request.understood.push_back("Référence : " + u.referenceName);

    // ---- 4. Ce qu'il faut demander avant d'agir ------------------------------
    const auto heardArtists = library.measuredArtists();
    const std::string known = heardArtists.empty() ? "aucun pour l'instant" : listNames(heardArtists, 5);
    if (audioReferenceProvided) return u;
    if (!u.unknownReferences.empty()) {
        std::vector<std::string> quoted;
        for (const auto& n : u.unknownReferences) quoted.push_back("« " + n + " »");
        u.question = "Je ne connais pas encore " + listNames(quoted) + ". Pour viser juste : cite-moi un artiste proche que j'ai déjà écouté (" +
                     known + std::string("), ou glisse-moi un son de cet artiste : je l'écouterai et je l'ajouterai à la base.");
    } else if (!missingGenres.empty() && missing.empty() && !u.hasReference()) {
        u.question = std::string("Je n'ai encore écouté aucun son ") + listNames(missingGenres) +
                     ". Glisse-moi un titre de ce style, ou cite un artiste que j'ai déjà écouté (" + known + ").";
    } else if ((!missing.empty() || !missingGenres.empty()) && !u.hasReference()) {
        missing.insert(missing.end(), missingGenres.begin(), missingGenres.end());
        u.question = std::string("Je connais ") + listNames(missing) + " de nom, mais je n'ai encore écouté aucun son de ce style. Glisse-moi un son de " +
                     listNames(missing) + ", ou cite un artiste que j'ai déjà écouté (" + known + ").";
    } else if (in.mentionsProvidedReference && !u.hasReference()) {
        u.question = "Tu parles d'une référence : glisse-moi le son, je l'écoute avant de toucher à ta prod.";
    } else if (in.hits.empty() && !in.lufs && !u.hasReference()) {
        u.question = std::string("Je n'ai pas compris ce que tu veux changer. Donne-moi un artiste ou un titre (j'ai écouté : ") + known +
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
