// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/ReferenceDynamics.cpp
// =============================================================================
#include "ReferenceDynamics.h"

#include "core/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace omnis::ai {

using params::MbControl;
using params::ParamId;

namespace {

constexpr int kHard = 0, kSoftKnee = 1, kDiode = 2; // ordre de clip_mode
constexpr int kLinear = 0, kAnalog = 1;             // ordre de sub_phase

/// Rampe continue : 0 en `from`, 1 en `to` (dans un sens ou dans l'autre), bornée.
double ramp(double x, double from, double to) {
    if (from == to) return x >= to ? 1.0 : 0.0;
    return std::clamp((x - from) / (to - from), 0.0, 1.0);
}

std::string fmt(const char* f, double v) {
    char b[200];
    std::snprintf(b, sizeof b, i18n::tr(f).c_str(), v); // format traduit avant d'être rempli
    return b;
}

void put(ConsolePlan& p, ParamId id, double v) {
    const auto* spec = params::findParameter(id);
    if (!spec || !std::isfinite(v)) return;
    v = params::clampPlain(*spec, v);
    for (auto& [k, value] : p.settings)
        if (k == id) {
            value = v;
            return;
        }
    p.settings.emplace_back(id, v);
}

double defaultOf(ParamId id) {
    const auto* spec = params::findParameter(id);
    return spec ? spec->defaultValue : 0.0;
}

/// Bande du multibande d'un nœud, selon les coupures en vigueur.
int mbBandOf(double hz, const std::array<double, 3>& splits) {
    int b = 0;
    while (b < 3 && hz >= splits[static_cast<std::size_t>(b)]) ++b;
    return b;
}

/// Part de l'énergie Mid sous `hz` (rapport de puissances : indépendant du niveau
/// absolu, donc valable aussi pour une signature agrégée, relative à son corps).
double shareBelow(const analysis::SpectralSignature& s, double hz) {
    double below = 0.0, total = 0.0;
    for (int k = 0; k < params::kMatchBands; ++k) {
        if (!s.trusts(k)) continue;
        const double p = std::pow(10.0, s.midDb[static_cast<std::size_t>(k)] / 10.0);
        total += p;
        if (params::matchBandHz(k) < hz) below += p;
    }
    return total > 0.0 ? below / total : 0.0;
}


} // namespace

ReferenceMeasures measureReferenceGap(const analysis::SpectralSignature& prod, const analysis::SpectralSignature& ref,
                                      const analysis::MusicDescriptors& refMusic, const CurrentValue& cur) {
    ReferenceMeasures m;
    m.refPlr = ref.plrDb;
    m.prodPlr = prod.plrDb;
    m.plrGap = prod.plrDb - ref.plrDb;
    m.refLufs = ref.integratedLufs;
    // Ancrages de la spécification : 8,5 dB (réf aérée) -> 6,5 dB (mur de briques).
    m.wall = ramp(ref.plrDb, 8.5, 6.5);
    m.gapToClose = ramp(m.plrGap, 1.0, 3.0);

    const double clipSplit = cur(ParamId::ClipSplit);
    m.prodSubShare = shareBelow(prod, clipSplit);
    m.refSubShare = shareBelow(ref, clipSplit);
    // Sub de chaque côté relativement à son corps (200 Hz – 5 kHz). Une signature
    // agrégée est déjà relative à son corps (son bodyDb() vaut 0) : même formule.
    m.subGapDb = (prod.subEnergyDb - prod.bodyDb()) - (ref.subEnergyDb - ref.bodyDb());

    // Dynamique (P90 − P10 des trames) des nœuds regroupés selon les bandes du
    // multibande. Les nœuds 20 et 28 Hz décrivent surtout le bruit de fond : exclus.
    const std::array<double, 3> splits{cur(ParamId::MbSplitLow), cur(ParamId::MbSplitMid), cur(ParamId::MbSplitHigh)};
    std::array<int, params::kMbBands> count{};
    for (int k = 0; k < params::kMatchBands; ++k) {
        const double hz = params::matchBandHz(k);
        if (hz < 35.0 || !prod.trusts(k) || !ref.trusts(k)) continue;
        const auto b = static_cast<std::size_t>(mbBandOf(hz, splits));
        m.prodSpread[b] += prod.midSpreadDb[static_cast<std::size_t>(k)];
        m.refSpread[b] += ref.midSpreadDb[static_cast<std::size_t>(k)];
        ++count[b];
    }
    for (std::size_t b = 0; b < m.prodSpread.size(); ++b)
        if (count[b] > 0) {
            m.prodSpread[b] /= count[b];
            m.refSpread[b] /= count[b];
        }

    // Micro-dynamique par zone (mêmes zones que le multibande par défaut).
    if (refMusic.valid && prod.music.valid)
        for (std::size_t b = 0; b < m.attackGap.size(); ++b) m.attackGap[b] = refMusic.microDb[b] - prod.music.microDb[b];

    // « Pompe » : mesurée sur le tempo par la fiche (`pompe` : le bas-médium ou le
    // médium creusé au début du temps, qui remonte à la fin). Sans tempo net ni
    // creux, 0 ; 1,5 dB -> 0 (le plus haut mesuré sur des prods trap sans
    // sidechain : +1,1), 3,5 dB -> 1 (un sidechain de −6 dB donne +2,3, de −16 dB +4,5).
    // Fiche ancienne (pas encore remesurée) : l'indice d'avant, trames de 0,37 s
    // mobiles et sections plates (moins fiable : ne voit pas « One More Time »).
    if (refMusic.valid && refMusic.hasPump) {
        m.pump = ramp(refMusic.pumpDb, 1.5, 3.5);
    } else {
        const double refLowMid = 0.5 * (m.refSpread[1] + m.refSpread[2]);
        m.pump = ramp(refLowMid - ref.loudnessRangeLu, 8.0, 12.0);
    }
    // Sub tenu : une note de grave existe et la dynamique sous la coupure est faible.
    m.subHold = refMusic.valid && refMusic.subHz > 0.0 && count[0] > 0 ? ramp(m.refSpread[0], 20.0, 14.0) : 0.0;

    // La 808 de la prod plafonne (elle domine l'énergie et il reste du PLR à
    // combler) alors que la réf est forte avec un sub dense.
    m.inflate808 = ramp(m.prodSubShare, 0.65, 0.80) * m.gapToClose * ramp(ref.integratedLufs, -10.0, -7.0) * ramp(m.refSubShare, 0.50, 0.70);

    // Bas-médium (226 – 640 Hz) de la réf plus dense ET plus stable que celui de la prod.
    double levelGap = 0.0, spreadGap = 0.0;
    int n = 0;
    for (int k = 0; k < params::kMatchBands; ++k) {
        const double hz = params::matchBandHz(k);
        if (hz < 200.0 || hz > 700.0) continue;
        levelGap += ref.relativeMidDb(k) - prod.relativeMidDb(k);
        spreadGap += prod.midSpreadDb[static_cast<std::size_t>(k)] - ref.midSpreadDb[static_cast<std::size_t>(k)];
        ++n;
    }
    if (n > 0) m.warmth = ramp(levelGap / n, 0.0, 3.0) * ramp(spreadGap / n, 0.0, 3.0);

    // Côtés larges (Side à moins de 6 dB du Mid au-dessus de 5 kHz) et dynamiques
    // (micro-dynamique de l'aigu) dans la réf.
    double sideMid = 0.0;
    n = 0;
    for (int k = 0; k < params::kMatchBands; ++k)
        if (params::matchBandHz(k) >= 5000.0 && ref.trusts(k)) {
            sideMid += ref.sideDb[static_cast<std::size_t>(k)] - ref.midDb[static_cast<std::size_t>(k)];
            ++n;
        }
    if (n > 0 && refMusic.valid) m.sideWidth = ramp(sideMid / n, -12.0, -6.0) * ramp(refMusic.microDb[3], 6.0, 9.0);
    return m;
}

void applyReferenceDynamics(ConsolePlan& p, const analysis::SpectralSignature& prod, const analysis::SpectralSignature& ref,
                            const analysis::MusicDescriptors& refMusic, const CurrentValue& cur) {
    if (prod.activeFrames <= 0 || ref.activeFrames <= 0) return; // rien d'entendu ou rien de mesuré
    const ReferenceMeasures m = measureReferenceGap(prod, ref, refMusic, cur);

    // --- A1. Phase du sub ---------------------------------------------------------
    // Réf écrêtée : la rotation de phase de l'IIR ferait exploser le true peak
    // (+7,8 dB mesurés sur 2hollis). Réf aérée, grave qui rebondit ET prod avec de
    // la marge : la couleur « console » est permise. Entre les deux : inchangé.
    if (m.refPlr < 7.5 || m.refLufs > -7.5) {
        put(p, ParamId::SubPhaseMode, kLinear);
    } else if (m.refPlr > 9.5 && m.prodPlr > 9.5 && refMusic.valid && refMusic.microDb[0] >= 4.5) {
        put(p, ParamId::SubPhaseMode, kAnalog);
        p.said.push_back(fmt("réf aérée (PLR %.1f dB) au grave souple : sub en phase Analog", m.refPlr));
    }

    // --- A2. Topologie du clipper ----------------------------------------------------
    if (m.refPlr >= 8.5) {
        // Réf aérée : genou doux (50 % à 8,5 dB, 80 % à 11,5 dB), la 808 garde son rebond.
        put(p, ParamId::ClipMode, kSoftKnee);
        put(p, ParamId::ClipKnee, 50.0 + 30.0 * ramp(m.refPlr, 8.5, 11.5));
        put(p, ParamId::ClipSubRoom, 0.0);
        p.said.push_back(fmt("réf aérée (PLR %.1f dB) : clip Soft Knee", m.refPlr) + fmt(" %.0f %%, sans Sub Make-Room", 50.0 + 30.0 * ramp(m.refPlr, 8.5, 11.5)));
    } else {
        // Vers le mur : le genou se ferme continûment ; au bout de la rampe, avec
        // plus de 3 dB de PLR à combler, l'écrêtage devient franc.
        double lowMidGap = 0.0;
        int n = 0;
        for (int k = 0; k < params::kMatchBands; ++k) {
            const double hz = params::matchBandHz(k);
            if (hz < 150.0 || hz > 700.0) continue;
            lowMidGap += ref.relativeMidDb(k) - prod.relativeMidDb(k);
            ++n;
        }
        if (n > 0) lowMidGap /= n;
        const bool brickWall = m.wall >= 1.0 && m.plrGap > 3.0;
        const int mode = brickWall ? (lowMidGap >= 1.5 ? kDiode : kHard) : kSoftKnee;
        put(p, ParamId::ClipMode, mode);
        put(p, ParamId::ClipKnee, 50.0 * (1.0 - m.wall));
        // Sub Make-Room : la prod dont le sub dépasse 65 % de l'énergie (100 % à 80 %).
        const double room = 100.0 * m.wall * m.gapToClose * ramp(m.prodSubShare, 0.65, 0.80);
        put(p, ParamId::ClipSubRoom, room);
        // Sub Drive proportionnel à l'écart de sub (prod − réf, relatif au corps).
        const double subDrive = m.wall * m.gapToClose * std::clamp(0.5 * std::fabs(m.subGapDb), 0.0, 6.0);
        put(p, ParamId::ClipSubDrive, subDrive);
        p.said.push_back(fmt("réf très tassée (PLR %.1f dB", m.refPlr) + fmt(", la tienne %.1f) : clip ", m.prodPlr) +
                         (mode == kHard ? "Hard" : mode == kDiode ? "Diode" : "Soft Knee") + fmt(", Sub Make-Room %.0f %%", room) +
                         fmt(", Sub Drive %.1f dB", subDrive));
    }

    // --- B. Multibande : la tenue de la réf, bande par bande --------------------------
    // Seuil au niveau moyen de la bande : la moitié haute de sa dynamique (Sp/2) est
    // divisée par le ratio. Pour ramener Sp à Sr : Sp/2 + Sp/(2r) = Sr
    //     r = Sp / (2·Sr − Sp)     (borné à 4:1, 3:1 pour le sub)
    // Rien si la prod est déjà aussi tenue que la réf.
    const double bpm = prod.music.valid && prod.music.bpm > 0.0 ? prod.music.bpm : (refMusic.bpm > 0.0 ? refMusic.bpm : 0.0);
    const double beatMs = bpm > 0.0 ? 60000.0 / bpm : 0.0;
    const std::array<double, 3> splits{cur(ParamId::MbSplitLow), cur(ParamId::MbSplitMid), cur(ParamId::MbSplitHigh)};
    std::array<double, params::kMbBands> bandPower{};
    double totalPower = 0.0;
    for (int k = 0; k < params::kMatchBands; ++k) {
        if (!prod.trusts(k)) continue;
        const double pw = std::pow(10.0, prod.midDb[static_cast<std::size_t>(k)] / 10.0);
        bandPower[static_cast<std::size_t>(mbBandOf(params::matchBandHz(k), splits))] += pw;
        totalPower += pw;
    }
    std::string ratios;
    for (int b = 0; b < params::kMbBands; ++b) {
        const auto bi = static_cast<std::size_t>(b);
        const double sp = m.prodSpread[bi], sr = m.refSpread[bi];
        double ratio = 1.0;
        if (sp > sr && sr > 0.0) ratio = std::clamp(sp / std::max(2.0 * sr - sp, sp / 4.0), 1.0, b == 0 ? 3.0 : 4.0);
        // Niveau de la bande estimé : sonie de la prod + part de la bande (approximation :
        // la pondération K et le Mid seul décalent de quelques dB).
        const double share = totalPower > 0.0 ? bandPower[bi] / totalPower : 0.0;
        const double level = share > 0.0 ? prod.integratedLufs + 10.0 * std::log10(share) : 0.0;
        const double thr = ratio > 1.0 ? std::clamp(level, -60.0, 0.0) : 0.0;
        put(p, params::mbId(b, MbControl::Ratio), ratio);
        put(p, params::mbId(b, MbControl::Threshold), thr);
        put(p, params::mbId(b, MbControl::Makeup), std::clamp((1.0 - 1.0 / ratio) * sp / 4.0, 0.0, 6.0));
        // Attaque : la réf a plus de crête d'attaque dans cette zone -> le compresseur
        // la laisse passer (jusqu'à 80 ms à +4 dB) ; moins -> il la prend plus tôt.
        const double a0 = defaultOf(params::mbId(b, MbControl::Attack));
        const double gap = m.attackGap[bi];
        const double attack = gap >= 0.0 ? a0 + (80.0 - a0) * ramp(gap, 0.0, 4.0) : a0 * (1.0 - 0.5 * ramp(-gap, 0.0, 4.0));
        put(p, params::mbId(b, MbControl::Attack), attack);
        // Relâchement : croche pour le sub et le bas (notes de 808), double-croche au-dessus.
        if (beatMs > 0.0) put(p, params::mbId(b, MbControl::Release), beatMs / (b < 2 ? 2.0 : 4.0));
        if (ratio > 1.0) {
            // La bande nommée par ses fréquences (rien à traduire).
            const double lo = b == 0 ? 0.0 : splits[static_cast<std::size_t>(b - 1)], hi = b == 3 ? 0.0 : splits[bi];
            char band[64];
            if (b == 0) std::snprintf(band, sizeof band, "< %.0f Hz", hi);
            else if (b == 3) std::snprintf(band, sizeof band, "> %.0f Hz", lo);
            else std::snprintf(band, sizeof band, "%.0f-%.0f Hz", lo, hi);
            char r[32];
            std::snprintf(r, sizeof r, " %.1f:1", ratio);
            ratios += (ratios.empty() ? "" : ", ") + std::string(band) + r;
        }
    }
    if (!ratios.empty()) p.said.push_back("multibande à la tenue de la réf : " + ratios);
    else p.said.push_back("multibande : ta prod est déjà aussi tenue que la réf");

    // --- B3. Limiteur : relâchement sur le tempo, pompe ou sub tenu -------------------
    const double autoRelease = 100.0 - 80.0 * m.pump * (1.0 - m.subHold);
    put(p, ParamId::LimAutoRelease, autoRelease);
    if (beatMs > 0.0) {
        const double sixteenth = beatMs / 4.0, eighth = beatMs / 2.0;
        put(p, ParamId::LimRelease, std::clamp(sixteenth + (eighth - sixteenth) * m.pump, 20.0, 400.0));
        p.said.push_back(fmt("relâchements calés sur le tempo (%.0f BPM)", bpm));
    }
    if (m.pump * (1.0 - m.subHold) > 0.05)
        p.said.push_back(fmt("la réf pompe : auto-release %.0f %%", autoRelease));
    else if (m.subHold > 0.5)
        p.said.push_back("sub de la réf tenu : auto-release 100 %, le grave ne module pas les médiums");

    // --- C1. Inflator ---------------------------------------------------------------
    // Couleur : écrite seulement quand la mesure la demande (un réglage fait à la
    // main n'est pas effacé par le choix d'une référence).
    if (m.inflate808 > 0.05) {
        put(p, ParamId::InflLowEffect, std::max(cur(ParamId::InflLowEffect), 60.0 * m.inflate808));
        put(p, ParamId::InflCurve, std::min(cur(ParamId::InflCurve), -30.0 * m.inflate808)); // vers −50 : plus dense
        p.said.push_back(fmt("ta 808 plafonne la sonie (%.0f %% de l'énergie) : Inflate 808 ", 100.0 * m.prodSubShare) +
                         fmt("%.0f %% plutôt que d'écraser au limiteur", 60.0 * m.inflate808));
    }
    if (m.warmth > 0.05) {
        put(p, ParamId::InflWarmth, std::max(cur(ParamId::InflWarmth), 40.0 * m.warmth));
        p.said.push_back(fmt("bas-médium de la réf plus dense et stable : chaleur %.0f %%", 40.0 * m.warmth));
    }

    // --- C2. Lien stéréo du limiteur sur les attaques --------------------------------
    put(p, ParamId::LimUnlinkTransient, 70.0 * m.sideWidth);
    if (m.sideWidth > 0.05) p.said.push_back(fmt("côtés larges et vifs dans la réf : limiteur délié %.0f %% sur les attaques", 70.0 * m.sideWidth));
}

} // namespace omnis::ai
