// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/ConsoleActions.cpp
// =============================================================================
#include "core/I18n.h"
#include "ConsoleActions.h"
#include "ReferenceDynamics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace omnis::ai {

using params::ParamId;

namespace {

std::string format(const char* f, double v) {
    char b[160];
    std::snprintf(b, sizeof b, i18n::tr(f).c_str(), v); // format traduit avant d'être rempli
    return b;
}

ParamId width(int band) { return static_cast<ParamId>(static_cast<int>(ParamId::ImgWidthFirst) + band); }

void put(ConsolePlan& p, ParamId id, double v) {
    const auto* spec = params::findParameter(id);
    if (!spec) return;
    v = params::clampPlain(*spec, v);
    for (auto& [k, value] : p.settings)
        if (k == id) {
            value = v;
            return;
        }
    p.settings.emplace_back(id, v);
}

/// L'état de la console tel que le plan le laisse (ses réglages, sinon l'état courant).
CurrentValue withPlan(const ConsolePlan& p, const CurrentValue& cur) {
    return [&p, cur](ParamId id) {
        for (const auto& [k, v] : p.settings)
            if (k == id) return v;
        return cur(id);
    };
}

/// La courbe d'intention dans le Match EQ, et ce qu'elle fait de plus marquant.
void writeCurve(ConsolePlan& p, const MatchIntent& intent) {
    put(p, ParamId::MatchAmount, 100.0);
    put(p, ParamId::MatchTilt, 0.0);
    int strongest = 0;
    for (int k = 0; k < params::kMatchBands; ++k) {
        put(p, params::matchMidId(k), intent.midDb[static_cast<std::size_t>(k)]);
        put(p, params::matchSideId(k), intent.sideDb[static_cast<std::size_t>(k)]);
        if (std::fabs(intent.midDb[static_cast<std::size_t>(k)]) > std::fabs(intent.midDb[static_cast<std::size_t>(strongest)])) strongest = k;
    }
    const double db = intent.midDb[static_cast<std::size_t>(strongest)];
    if (std::fabs(db) >= 0.3) {
        const double hz = params::matchBandHz(strongest);
        p.said.push_back("Match EQ " + format("%+.1f dB", db) + (hz >= 1000.0 ? format(" vers %.1f kHz", hz / 1000.0) : format(" vers %.0f Hz", hz)));
    } else {
        p.said.push_back("Match EQ : presque rien à changer");
    }
}

/// Les axes que le Match EQ ne porte pas : un module chacun.
void applyAxes(ConsolePlan& p, const Descriptor& d, const MatchRequest& request, const CurrentValue& cur) {
    const double punch = d[Axis::Punch], density = d[Axis::Density], saturation = d[Axis::Saturation];
    const double clean = d[Axis::Cleanliness], widthWish = d[Axis::Width];
    if (std::fabs(punch) >= 0.25) {
        const double v = std::clamp(cur(ParamId::LimPunch) + 30.0 * punch, 0.0, 100.0);
        put(p, ParamId::LimPunch, v);
        p.said.push_back("Punch " + format("%.0f %%", v));
    }
    if (std::fabs(density) >= 0.25) {
        const double up = std::clamp(cur(ParamId::LimUpward) + 3.0 * density, 0.0, 9.0);
        put(p, ParamId::LimUpward, up);
        const double glue = std::clamp(glueOf(cur) + 0.25 * density, 0.0, 1.0);
        for (const auto& [id, v] : glueSettings(glue)) put(p, id, v);
        p.said.push_back("densité : upward " + format("%.1f dB", up) + ", glue " + format("%.0f %%", 100.0 * glue));
    }
    if (std::fabs(saturation) >= 0.25 || clean <= -0.5) {
        const double s = saturation - 0.5 * std::min(0.0, clean);
        const double high = std::clamp(cur(ParamId::InflHighEffect) + 20.0 * s, 0.0, 100.0);
        put(p, ParamId::InflHighEffect, high);
        if (!request.protectedRegions[0]) put(p, ParamId::InflLowEffect, std::clamp(cur(ParamId::InflLowEffect) + 15.0 * s, 0.0, 100.0));
        p.said.push_back("Inflator " + format("%.0f %%", high) + (request.protectedRegions[0] ? " (la 808 reste propre)" : ""));
    }
    if (clean >= 0.5) {
        const double drive = std::max(0.0, cur(ParamId::ClipDrive) - 2.0 * clean);
        put(p, ParamId::ClipDrive, drive);
        put(p, ParamId::InflHighEffect, std::max(0.0, cur(ParamId::InflHighEffect) - 10.0 * clean));
        p.said.push_back("plus propre : clip " + format("%.1f dB", drive));
    } else if (clean <= -0.5) {
        const double drive = std::min(12.0, cur(ParamId::ClipDrive) + 2.0 * -clean);
        put(p, ParamId::ClipDrive, drive);
        p.said.push_back("plus sale : clip " + format("%.1f dB", drive));
    }
    if (std::fabs(widthWish) >= 0.25 && !request.widthProtected) {
        const double hm = std::clamp(cur(width(2)) + 15.0 * widthWish, 0.0, 200.0);
        const double hi = std::clamp(cur(width(3)) + 15.0 * widthWish, 0.0, 200.0);
        put(p, width(2), hm);
        put(p, width(3), hi);
        put(p, width(1), std::clamp(cur(width(1)) + 7.5 * widthWish, 0.0, 200.0));
        p.said.push_back("largeur " + format("%.0f %%", hm) + " (le bas reste centré)");
    }
}

/// Ce que la fiche complète de la référence apprend, comparée à celle de la prod :
/// le punch, le grave mono, l'écart couplet / refrain, l'écrêtage voulu.
void applyMusic(ConsolePlan& p, const analysis::MusicDescriptors& ref, const analysis::MusicDescriptors& prod, const CurrentValue& cur,
                bool punchAsked) {
    if (!ref.valid || !prod.valid) return;
    // Punch : la crête des attaques au-dessus du niveau, du bas-médium à l'aigu.
    const double refPunch = (ref.microDb[1] + ref.microDb[2] + ref.microDb[3]) / 3.0;
    const double prodPunch = (prod.microDb[1] + prod.microDb[2] + prod.microDb[3]) / 3.0;
    const double gap = refPunch - prodPunch;
    if (gap > 0.8 && !punchAsked) {
        const double v = std::clamp(cur(ParamId::LimPunch) + 15.0 * gap, 0.0, 80.0);
        put(p, ParamId::LimPunch, v);
        p.said.push_back(format("la réf attaque plus (+%.1f dB) : Punch ", gap) + format("%.0f %%", v));
    } else if (gap < -1.5 && !punchAsked) {
        // Jamais de compression vers le haut sans demande (docs/REFONTE.md §3) :
        // elle remonte tout ce qui est calme et se bat contre les fondus.
        p.said.push_back(format("la réf est plus dense (%.1f dB d'attaque en moins) : dis « plus dense » pour la suivre", -gap));
    }
    // Grave : mono dans la réf, pas dans la prod.
    if (ref.lowCorrelation >= 0.97 && prod.lowCorrelation < 0.92) {
        put(p, ParamId::MonoFreq, std::max(120.0, cur(ParamId::MonoFreq)));
        put(p, ParamId::MonoAmount, 100.0);
        p.said.push_back(format("grave mono comme la réf (le tien : corrélation %.2f)", prod.lowCorrelation));
    }
    // Couplet / refrain : un écart bien plus grand que la réf -> on remonte le calme.
    const double refSpread = ref.shortTermLufs[2] - ref.shortTermLufs[0], prodSpread = prod.shortTermLufs[2] - prod.shortTermLufs[0];
    if (prodSpread > refSpread + 4.0) {
        // Le contraste couplet / refrain est un choix de la prod : on le garde.
        p.said.push_back(format("ton calme est %.0f dB sous ton refrain, ", prodSpread) +
                         format("la réf %.0f : contraste gardé (dis « plus dense » pour le réduire)", refSpread));
    }
    // Une réf écrêtée exprès (rage) : du clip assumé.
    if (ref.clipPercent > 0.5 && prod.clipPercent < 0.1) {
        const double drive = std::clamp(cur(ParamId::ClipDrive) + 3.0, 0.0, 8.0);
        put(p, ParamId::ClipDrive, drive);
        p.said.push_back(format("la réf est écrêtée exprès (%.1f %% des échantillons) : clip ", ref.clipPercent) + format("%.1f dB", drive));
    }
    // Pour info : tempo, tonalité, note de la 808.
    std::string info;
    if (ref.bpm > 0.0 && prod.bpm > 0.0) info += format("réf %.0f BPM", ref.bpm) + format(" / toi %.0f", prod.bpm);
    if (ref.keyRoot >= 0 && prod.keyRoot >= 0 && ref.keyCode() != prod.keyCode())
        info += (info.empty() ? "" : " · ") + std::string("réf en ") + ref.keyName() + ", toi en " + prod.keyName();
    if (ref.subHz > 0.0 && prod.subHz > 0.0 && std::fabs(12.0 * std::log2(ref.subHz / prod.subHz)) >= 1.0)
        info += (info.empty() ? "" : " · ") + std::string("808 réf ") + analysis::MusicDescriptors::noteName(ref.subHz) + ", la tienne " +
                analysis::MusicDescriptors::noteName(prod.subHz);
    if (!info.empty()) p.said.push_back(info);
}

/// Le niveau choisi (NIVEAU) : la sonie visée, et la densité qui va avec. « Style » garde
/// celle du style ou de la référence, bornée à -14 … -5 LUFS. Plus le niveau est fort, plus
/// le clip et l'Inflator prennent leur part (le limiteur seul écraserait le contraste).
double applyLevel(ConsolePlan& p, const CurrentValue& cur, double styleLufs) {
    const auto level = static_cast<params::LoudnessLevel>(std::clamp(static_cast<int>(std::lround(cur(ParamId::LoudnessLevel))), 0, 4));
    static constexpr double kClip[] = {2.0, 0.0, 1.0, 2.0, 3.0};
    static constexpr double kInflate[] = {15.0, 0.0, 10.0, 15.0, 20.0};
    const auto i = static_cast<std::size_t>(level);
    if (cur(ParamId::ClipDrive) < kClip[i]) put(p, ParamId::ClipDrive, kClip[i]);
    if (cur(ParamId::InflHighEffect) < kInflate[i]) put(p, ParamId::InflHighEffect, kInflate[i]);
    const double target = level == params::LoudnessLevel::Style ? std::clamp(styleLufs, -14.0, -5.0) : params::loudnessLevelLufs(level);
    p.targetLufs = target;
    return target;
}

/// Les plaintes qui reviennent par moments (sifflantes, aigus qui piquent) : une
/// bande de l'EQ dynamique, qui ne retient que quand le défaut sort du lot. La
/// correction fixe du Match EQ reste. Une seconde plainte au même endroit creuse
/// la bande (jusqu'à -12 dB).
void applyDynamicComplaints(ConsolePlan& p, const Interpretation& in, const CurrentValue& cur0) {
    struct Recipe {
        const char* conceptId;
        int preferred; ///< bande dont le réglage d'usine est là
        double hz, q, rangeDb, thresholdDb;
        params::DynChannel channel;
        const char* said;
    };
    static constexpr Recipe kRecipes[] = {
        {"siffle", 3, 7000.0, 2.0, -6.0, 3.0, params::DynChannel::Mid, "EQ dynamique : les sifflantes (7 kHz, centre) retenues quand elles sortent, jusqu'à %.0f dB"},
        {"agressif", 2, 3500.0, 1.2, -4.0, 3.0, params::DynChannel::Stereo, "EQ dynamique : les aigus qui piquent (3,5 kHz) retenus quand ils sortent, jusqu'à %.0f dB"},
    };
    for (const auto& r : kRecipes) {
        // Une plainte a un poids négatif (le défaut est à corriger).
        const bool named = std::any_of(in.hits.begin(), in.hits.end(), [&](const ConceptHit& h) { return h.conceptId == r.conceptId && h.weight < 0.0; });
        if (!named) continue;
        const CurrentValue cur = withPlan(p, cur0);
        const auto at = [](int b, params::DynControl c) { return params::dynId(b, c); };
        // Une bande déjà allumée à moins d'une demi-octave : on la creuse.
        int band = -1;
        for (int b = 0; b < params::kDynBands && band < 0; ++b)
            if (cur(at(b, params::DynControl::On)) >= 0.5 && std::fabs(std::log2(cur(at(b, params::DynControl::Freq)) / r.hz)) < 0.5) band = b;
        double range = r.rangeDb;
        if (band >= 0) {
            range = std::max(-12.0, std::min(cur(at(band, params::DynControl::Range)), r.rangeDb) - 2.0);
        } else {
            band = r.preferred;
            if (cur(at(band, params::DynControl::On)) >= 0.5)
                for (int b = 0; b < params::kDynBands; ++b)
                    if (cur(at(b, params::DynControl::On)) < 0.5) {
                        band = b;
                        break;
                    }
            put(p, at(band, params::DynControl::Freq), r.hz);
            put(p, at(band, params::DynControl::Q), r.q);
            put(p, at(band, params::DynControl::Threshold), r.thresholdDb);
            put(p, at(band, params::DynControl::Channel), static_cast<double>(r.channel));
        }
        put(p, at(band, params::DynControl::On), 1.0);
        put(p, at(band, params::DynControl::Range), range);
        put(p, ParamId::MatchOn, 1.0);
        p.said.push_back(format(r.said, range));
    }
}

/// Un caractère de saturation nommé (« à lampe », « bande », « transistor ») : la courbe de
/// l'Inflator, et assez d'effet pour l'entendre.
void applySaturationCharacter(ConsolePlan& p, const Interpretation& in, const CurrentValue& cur0) {
    struct Choice {
        const char* conceptId;
        params::InflCharacter character;
        const char* said;
    };
    static constexpr Choice kChoices[] = {
        {"bande", params::InflCharacter::Tape, "Inflator : caractère Bande (crêtes tassées en douceur, harmoniques impaires)"},
        {"lampe", params::InflCharacter::Tube, "Inflator : caractère Lampe (harmoniques paires, plus rond)"},
        {"transistor", params::InflCharacter::Transistor, "Inflator : caractère Transistor (propre puis mordant)"},
    };
    for (const auto& c : kChoices) {
        const bool named = std::any_of(in.hits.begin(), in.hits.end(), [&](const ConceptHit& h) { return h.conceptId == c.conceptId && h.weight > 0.0; });
        if (!named) continue;
        const CurrentValue cur = withPlan(p, cur0);
        put(p, ParamId::InflCharacter, static_cast<double>(c.character));
        if (cur(ParamId::InflHighEffect) < 20.0) put(p, ParamId::InflHighEffect, 20.0);
        put(p, ParamId::InflOn, 1.0);
        p.said.push_back(c.said);
        return; // un seul caractère : le premier nommé
    }
}

/// « de la colle », ou un caractère de compresseur nommé (« opto », « comp FET »…) : le module
/// Comp, réglé pour une réduction de 2 à 3 dB sur les passages forts de la prod entendue.
void applyBusComp(ConsolePlan& p, const Interpretation& in, const analysis::SpectralSignature& prod, const CurrentValue& cur0) {
    struct Character {
        const char* conceptId;
        params::CompCharacter character;
        double ratio, attackMs, releaseMs, mix;
        const char* name;
        const char* said;
    };
    static constexpr Character kCharacters[] = {
        {"comp_opto", params::CompCharacter::Opto, 3.0, 10.0, 300.0, 100.0, "Opto", "doux, relâche selon le programme"},
        {"comp_fet", params::CompCharacter::Fet, 4.0, 2.0, 100.0, 70.0, "FET", "rapide, en parallèle à 70 % pour garder les attaques"},
        {"comp_vca", params::CompCharacter::Vca, 2.0, 10.0, 200.0, 100.0, "VCA", "la colle propre"},
        {"comp_varimu", params::CompCharacter::VariMu, 2.5, 25.0, 500.0, 100.0, "Vari-mu", "arrondi, la compression monte peu à peu"},
    };
    const auto weightOf = [&](const char* id) {
        double w = 0.0;
        for (const auto& h : in.hits)
            if (h.conceptId == id) w += h.weight;
        return w;
    };
    const Character* named = nullptr;
    for (const auto& c : kCharacters)
        if (weightOf(c.conceptId) > 0.0) {
            named = &c;
            break;
        }
    const double glue = weightOf("colle");
    if (!named && std::fabs(glue) < 1e-9) return;
    const CurrentValue cur = withPlan(p, cur0);
    const bool wasOn = cur(ParamId::CompOn) >= 0.5;
    if (glue < 0.0 && !named) {
        // « moins de colle » : le seuil remonte ; presque plus rien à faire -> éteint.
        if (!wasOn) return;
        const double thr = cur(ParamId::CompThreshold) + 4.0 * -glue;
        if (thr > -6.0) {
            put(p, ParamId::CompOn, 0.0);
            p.said.push_back("Comp éteint (plus de colle)");
        } else {
            put(p, ParamId::CompThreshold, thr);
            p.said.push_back("Comp : moins de colle, seuil " + format("%.0f dB", thr));
        }
        return;
    }
    // Sans caractère nommé : celui du module s'il tourne déjà, sinon VCA (kCharacters suit l'ordre de l'enum).
    const auto current = static_cast<std::size_t>(wasOn ? std::clamp(static_cast<int>(std::lround(cur(ParamId::CompCharacter))), 0, 3) : 2);
    const Character& c = named ? *named : kCharacters[current];
    double threshold = cur(ParamId::CompThreshold);
    if (!wasOn) {
        // Seuil quelques dB au-dessus de la sonie moyenne : seuls les passages forts sont tenus.
        threshold = prod.integratedLufs > -70.0 ? std::clamp(prod.integratedLufs + 5.0, -36.0, -6.0) : -18.0;
    } else if (glue > 0.0) {
        threshold = std::max(-40.0, threshold - 3.0 * glue); // « plus de colle »
    }
    put(p, ParamId::CompOn, 1.0);
    put(p, ParamId::CompCharacter, static_cast<double>(c.character));
    put(p, ParamId::CompThreshold, threshold);
    if (named || !wasOn) {
        put(p, ParamId::CompRatio, c.ratio);
        put(p, ParamId::CompAttack, c.attackMs);
        put(p, ParamId::CompRelease, c.releaseMs);
        put(p, ParamId::CompMix, c.mix);
        put(p, ParamId::CompMakeup, 1.0);
    }
    char b[256];
    std::snprintf(b, sizeof b, i18n::tr("Comp %s : seuil %.0f dB, %.1f:1 — %s").c_str(), c.name, threshold,
                  named || !wasOn ? c.ratio : cur(ParamId::CompRatio), i18n::tr(c.said).c_str());
    p.said.push_back(b);
}

/// « EQ analogique » / « phase linéaire » : le caractère du Match EQ.
void applyEqCharacter(ConsolePlan& p, const Interpretation& in) {
    for (const auto& h : in.hits) {
        if (h.weight <= 0.0) continue;
        if (h.conceptId == "eq_analog") {
            put(p, ParamId::MatchCharacter, 1.0);
            p.said.push_back("Match EQ Analogique (cloches plus larges, aucun pré-écho sur les attaques)");
            return;
        }
        if (h.conceptId == "eq_propre") {
            put(p, ParamId::MatchCharacter, 0.0);
            p.said.push_back("Match EQ Propre (phase linéaire, précis)");
            return;
        }
    }
}

} // namespace

std::vector<std::pair<ParamId, double>> glueSettings(double t) {
    t = std::clamp(t, 0.0, 1.0);
    std::vector<std::pair<ParamId, double>> out;
    for (int b = 0; b < params::kMbBands; ++b) {
        const double ratio = b < 2 ? 1.0 + 2.0 * t : 1.0 + 3.0 * t, thr = -24.0 * t;
        out.emplace_back(params::mbId(b, params::MbControl::Ratio), ratio);
        out.emplace_back(params::mbId(b, params::MbControl::Threshold), thr);
        out.emplace_back(params::mbId(b, params::MbControl::Makeup), -0.5 * thr * (1.0 - 1.0 / ratio));
    }
    return out;
}

double glueOf(const CurrentValue& cur) { return std::clamp((cur(params::mbId(2, params::MbControl::Ratio)) - 1.0) / 3.0, 0.0, 1.0); }

ConsolePlan planRequest(const Understanding& u, const analysis::SpectralSignature& prod, const CurrentValue& cur) {
    ConsolePlan p;
    const auto& d = u.interpretation.desire;
    const bool tonal = std::any_of(d.v.begin(), d.v.begin() + kTonalAxes, [](double v) { return std::fabs(v) > 1e-9; }) ||
                       std::fabs(d[Axis::Width]) > 1e-9 || u.hasReference();
    if (tonal) {
        const MatchIntent intent = u.hasReference() ? planMatch(prod, aggregateSignature(u.referenceEntries), u.request) : planFromWords(prod, u.request);
        writeCurve(p, intent);
        if (u.hasReference()) {
            p.said.insert(p.said.begin(), "vers " + u.referenceName);
            const auto refMusic = averageMusic(u.referenceEntries);
            applyMusic(p, refMusic, prod.music, cur, std::fabs(d[Axis::Punch]) >= 0.25);
            // Topologie et constantes de temps par la mesure ; les mots (applyAxes) passent après.
            applyReferenceDynamics(p, prod, aggregateSignature(u.referenceEntries), refMusic, withPlan(p, cur));
        }
        if (u.hasReference() || u.request.targetLufs || std::fabs(u.request.loudnessOffsetLu) > 1e-9) p.targetLufs = intent.targetLufs;
    } else if (u.request.targetLufs || std::fabs(u.request.loudnessOffsetLu) > 1e-9) {
        // Seulement plus fort / moins fort : partir de la sonie qu'on a.
        p.targetLufs = u.request.targetLufs ? *u.request.targetLufs : prod.integratedLufs + u.request.loudnessOffsetLu;
    }
    applyAxes(p, d, u.request, cur);
    applyDynamicComplaints(p, u.interpretation, cur);
    applySaturationCharacter(p, u.interpretation, cur);
    applyBusComp(p, u.interpretation, prod, cur);
    applyEqCharacter(p, u.interpretation);
    if (p.targetLufs) {
        p.targetLufs = std::clamp(*p.targetLufs, -20.0, -4.0);
        p.said.push_back("volume vers " + format("%.1f LUFS", *p.targetLufs));
    }
    if (p.settings.empty() && !p.targetLufs) p.said.push_back("rien à régler pour cette demande");
    return p;
}

ConsolePlan planReference(const analysis::SpectralSignature& prod, const std::vector<const ReferenceEntry*>& refs, const std::string& name,
                          const CurrentValue& cur) {
    ConsolePlan p;
    if (refs.empty()) {
        p.said.push_back(name + " : aucun titre mesuré, rien à viser");
        return p;
    }
    const auto refSignature = aggregateSignature(refs);
    const MatchIntent intent = planMatch(prod, refSignature, MatchRequest{});
    writeCurve(p, intent);
    p.said.insert(p.said.begin(), "vers " + name + format(" (%.0f titre", static_cast<double>(refs.size())) + (refs.size() > 1 ? "s)" : ")"));
    const auto refMusic = averageMusic(refs);
    applyMusic(p, refMusic, prod.music, cur, false);
    applyReferenceDynamics(p, prod, refSignature, refMusic, withPlan(p, cur));
    // Le niveau choisi (NIVEAU) ; « Style » : celui de la référence, borné à -14 … -5 LUFS.
    applyLevel(p, withPlan(p, cur), intent.targetLufs);
    p.said.push_back("volume vers " + format("%.1f LUFS", *p.targetLufs) +
                     (intent.targetLufs > *p.targetLufs + 0.5 ? format(" (la réf est à %.1f : choisis un NIVEAU plus fort pour la suivre)", intent.targetLufs) : ""));
    return p;
}

ConsolePlan planAuto(const analysis::SpectralSignature& loud, const ReferenceLibrary& library, const std::string& style, const CurrentValue& cur) {
    std::vector<const ReferenceEntry*> refs;
    if (!style.empty()) refs = library.measuredWithGenre(style);
    std::string from = style;
    if (refs.empty()) {
        for (const auto& e : library.entries())
            if (e.measured) refs.push_back(&e);
        from = "toute la base";
    }
    return planAuto(loud, refs, from, cur);
}

ConsolePlan planAuto(const analysis::SpectralSignature& loud, const std::vector<const ReferenceEntry*>& refs, const std::string& from,
                     const CurrentValue& cur) {
    ConsolePlan p;
    if (refs.empty()) {
        p.said.push_back("aucune référence mesurée : AUTO règle seulement le volume");
        applyLevel(p, cur, -11.0);
        return p;
    }
    MatchRequest request;
    // AUTO vise un style (des profils moyens, dont une partie vient de fichiers compressés) :
    // un tiers de l'écart seulement, et le sub presque tenu. La prod garde son caractère ;
    // une couleur plus marquée se demande (référence choisie, mots).
    request.resemblance = 0.35;
    request.subLimitDb = 1.5;
    const auto refSignature = aggregateSignature(refs);
    const MatchIntent intent = planMatch(loud, refSignature, request);
    writeCurve(p, intent);
    // Un profil de genre résume des dizaines de titres : on le dit, plutôt que « 1 titres ».
    const bool profile = refs.size() == 1 && refs.front()->isProfile();
    p.said.insert(p.said.begin(), "AUTO vers " + from +
                                      (profile ? std::string(" (profil de genre)")
                                               : refs.size() == 1 ? std::string(" (1 titre)") : format(" (%.0f titres)", static_cast<double>(refs.size()))));
    // Une base de mastering rap / trap : du relief, et la densité du niveau choisi.
    if (cur(ParamId::LimPunch) < 15.0) put(p, ParamId::LimPunch, 15.0);
    put(p, ParamId::LimCeiling, -1.0);
    applyLevel(p, cur, intent.targetLufs);
    p.said.push_back("punch, densité du niveau choisi ; plafond -1 dBTP");
    // La fiche complète des références (punch, grave mono, sections, écrêtage) passe
    // après la base : elle la précise.
    const auto refMusic = averageMusic(refs);
    applyMusic(p, refMusic, loud.music, withPlan(p, cur), false);
    applyReferenceDynamics(p, loud, refSignature, refMusic, withPlan(p, cur));
    p.said.push_back("volume vers " + format("%.1f LUFS", *p.targetLufs));
    return p;
}

} // namespace omnis::ai
