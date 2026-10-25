// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/ConsoleActions.cpp
// =============================================================================
#include "ConsoleActions.h"

#include <algorithm>
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
        if (u.hasReference()) p.said.insert(p.said.begin(), "vers " + u.referenceName);
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
    p.said.push_back("volume vers " + format("%.1f LUFS", *p.targetLufs));
    return p;
}

} // namespace omnis::ai
