// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/ConsoleActions.cpp
// =============================================================================
#include "ConsoleActions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace omnis::ai {

using params::ParamId;

namespace {

std::string format(const char* f, double v) {
    char b[96];
    std::snprintf(b, sizeof b, f, v);
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

/// La fiche moyenne de plusieurs références (celles qui ont la fiche complète).
analysis::MusicDescriptors averageMusic(const std::vector<const ReferenceEntry*>& refs) {
    analysis::MusicDescriptors m;
    int n = 0;
    std::array<int, 24> keys{};
    for (const auto* e : refs) {
        const auto& x = e->signature.music;
        if (!x.valid) continue;
        ++n;
        for (int z = 0; z < 4; ++z) m.microDb[static_cast<std::size_t>(z)] += x.microDb[static_cast<std::size_t>(z)];
        for (int i = 0; i < 3; ++i) m.shortTermLufs[static_cast<std::size_t>(i)] += x.shortTermLufs[static_cast<std::size_t>(i)];
        m.lowCorrelation += x.lowCorrelation;
        m.correlation += x.correlation;
        m.clipPercent += x.clipPercent;
        m.bpm += x.bpm;
        m.subHz += x.subHz;
        if (x.keyRoot >= 0) ++keys[static_cast<std::size_t>(x.keyRoot + (x.keyMinor ? 12 : 0))];
    }
    if (n == 0) return m;
    m.valid = true;
    for (auto& v : m.microDb) v /= n;
    for (auto& v : m.shortTermLufs) v /= n;
    m.lowCorrelation /= n;
    m.correlation /= n;
    m.clipPercent /= n;
    if (n == 1) {
        m.bpm /= n;
        m.subHz /= n;
        const auto k = std::max_element(keys.begin(), keys.end()) - keys.begin();
        if (keys[static_cast<std::size_t>(k)] > 0) {
            m.keyRoot = static_cast<int>(k % 12);
            m.keyMinor = k >= 12;
        }
    } else {
        m.bpm = m.subHz = 0.0; // une moyenne de tempos ou de notes ne veut rien dire
    }
    return m;
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
        const double up = std::clamp(cur(ParamId::LimUpward) + std::min(4.0, -gap), 0.0, 9.0);
        put(p, ParamId::LimUpward, up);
        p.said.push_back(format("la réf est plus dense (%.1f dB d'attaque en moins) : upward ", -gap) + format("%.1f dB", up));
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
        const double up = std::clamp(cur(ParamId::LimUpward) + std::min(6.0, 0.5 * (prodSpread - refSpread)), 0.0, 9.0);
        put(p, ParamId::LimUpward, up);
        p.said.push_back(format("ton calme est %.0f dB sous ton refrain, ", prodSpread) + format("la réf %.0f : upward ", refSpread) +
                         format("%.1f dB", up));
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
            applyMusic(p, averageMusic(u.referenceEntries), prod.music, cur, std::fabs(d[Axis::Punch]) >= 0.25);
        }
        if (u.hasReference() || u.request.targetLufs || std::fabs(u.request.loudnessOffsetLu) > 1e-9) p.targetLufs = intent.targetLufs;
    } else if (u.request.targetLufs || std::fabs(u.request.loudnessOffsetLu) > 1e-9) {
        // Seulement plus fort / moins fort : partir de la sonie qu'on a.
        p.targetLufs = u.request.targetLufs ? *u.request.targetLufs : prod.integratedLufs + u.request.loudnessOffsetLu;
    }
    applyAxes(p, d, u.request, cur);
    if (p.targetLufs) {
        p.targetLufs = std::clamp(*p.targetLufs, -20.0, -4.0);
        p.said.push_back("volume vers " + format("%.1f LUFS", *p.targetLufs));
    }
    if (p.settings.empty() && !p.targetLufs) p.said.push_back("rien à régler pour cette demande");
    return p;
}

ConsolePlan planAuto(const analysis::SpectralSignature& loud, const ReferenceLibrary& library, const std::string& style, const CurrentValue& cur) {
    ConsolePlan p;
    std::vector<const ReferenceEntry*> refs;
    if (!style.empty()) refs = library.measuredWithGenre(style);
    std::string from = style;
    if (refs.empty()) {
        for (const auto& e : library.entries())
            if (e.measured && e.signature.activeFrames > 0) refs.push_back(&e);
        from = "toute la base";
    }
    if (refs.empty()) {
        p.said.push_back("aucune référence mesurée : AUTO règle seulement le volume");
        p.targetLufs = -9.0;
        return p;
    }
    MatchRequest request;
    request.resemblance = 0.5; // la moitié de l'écart : la prod garde son caractère
    const MatchIntent intent = planMatch(loud, aggregateSignature(refs), request);
    writeCurve(p, intent);
    p.said.insert(p.said.begin(), "AUTO vers " + from + format(" (%.0f titres)", static_cast<double>(refs.size())));
    // Une base de mastering rap / trap : un peu de densité et de relief, rien de forcé.
    if (cur(ParamId::LimPunch) < 15.0) put(p, ParamId::LimPunch, 15.0);
    if (cur(ParamId::InflHighEffect) < 15.0) put(p, ParamId::InflHighEffect, 15.0);
    if (cur(ParamId::ClipDrive) < 2.0) put(p, ParamId::ClipDrive, 2.0);
    put(p, ParamId::LimCeiling, -1.0);
    p.targetLufs = std::clamp(intent.targetLufs, -14.0, -5.0);
    p.said.push_back("punch, Inflator et clip doux ; plafond -1 dBTP");
    // La fiche complète des références (punch, grave mono, sections, écrêtage) passe
    // après la base : elle la précise.
    applyMusic(p, averageMusic(refs), loud.music, [&](ParamId id) {
        for (const auto& [k, v] : p.settings)
            if (k == id) return v;
        return cur(id);
    }, false);
    p.said.push_back("volume vers " + format("%.1f LUFS", *p.targetLufs));
    return p;
}

} // namespace omnis::ai
