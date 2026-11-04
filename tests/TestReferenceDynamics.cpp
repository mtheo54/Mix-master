// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestReferenceDynamics.cpp — La topologie des étages non linéaires et
//  les constantes de temps viennent de la MESURE de la référence, pas d'un genre.
//  Deux références aux mesures opposées, sans aucun genre, donnent deux
//  topologies opposées sur la même prod.
// =============================================================================
#include "TestFramework.h"

#include "ai/ConsoleActions.h"
#include "ai/MixDiagnosis.h"
#include "ai/ReferenceDynamics.h"
#include "ai/ReferenceLibrary.h"
#include "core/I18n.h"

#include <cmath>
#include <cstdio>
#include <optional>

using namespace omnis;
using params::ParamId;

namespace {

struct SigSpec {
    double lufs = -14.0, plr = 11.0, lra = 6.0;
    double subBoostDb = 0.0;                    ///< nœuds sous 120 Hz au-dessus du reste
    std::array<double, 4> spread{12, 10, 10, 10}; ///< P90 − P10 par bande (sub, bas, médium, aigu)
    std::array<double, 4> micro{4, 5, 5, 7};      ///< crête 10 ms − niveau 400 ms par zone
    double sideMinusMidHf = -14.0;              ///< Side − Mid au-dessus de 5 kHz
    double bpm = 140.0, subHz = 49.0;
};

analysis::SpectralSignature makeSignature(const SigSpec& s) {
    analysis::SpectralSignature sig;
    double sub = 0.0;
    for (int k = 0; k < params::kMatchBands; ++k) {
        const double hz = params::matchBandHz(k);
        const auto i = static_cast<std::size_t>(k);
        const int band = hz < 120.0 ? 0 : hz < 800.0 ? 1 : hz < 5000.0 ? 2 : 3;
        sig.midDb[i] = -30.0 + (band == 0 ? s.subBoostDb : 0.0);
        sig.sideDb[i] = sig.midDb[i] + (hz >= 5000.0 ? s.sideMinusMidHf : -14.0);
        sig.midSpreadDb[i] = s.spread[static_cast<std::size_t>(band)];
        if (hz <= 80.0) sub += std::pow(10.0, sig.midDb[i] / 10.0);
    }
    sig.subEnergyDb = 10.0 * std::log10(sub);
    sig.integratedLufs = s.lufs;
    sig.plrDb = s.plr;
    sig.truePeakDb = s.lufs + s.plr;
    sig.loudnessRangeLu = s.lra;
    sig.activeFrames = 200;
    sig.music.valid = true;
    sig.music.microDb = s.micro;
    sig.music.bpm = s.bpm;
    sig.music.subHz = s.subHz;
    sig.music.lowCorrelation = 0.99;
    sig.music.correlation = 0.6;
    sig.music.shortTermLufs = {s.lufs - 3.0, s.lufs, s.lufs + 1.0};
    return sig;
}

double defaultCurrent(ParamId id) {
    const auto* spec = params::findParameter(id);
    return spec ? spec->defaultValue : 0.0;
}

std::optional<double> valueOf(const ai::ConsolePlan& p, ParamId id) {
    for (const auto& [k, v] : p.settings)
        if (k == id) return v;
    return std::nullopt;
}

double settingOr(const ai::ConsolePlan& p, ParamId id) { return valueOf(p, id).value_or(defaultCurrent(id)); }

/// La prod : une 808 qui porte ~78 % de l'énergie, du PLR (11 dB), une 808 qui bouge.
analysis::SpectralSignature userProd() {
    SigSpec s;
    s.lufs = -14.0;
    s.plr = 11.0;
    s.lra = 6.0;
    s.subBoostDb = 9.5;
    s.spread = {30.0, 12.0, 12.0, 11.0};
    s.micro = {5.0, 5.0, 5.0, 7.0};
    s.bpm = 140.0;
    return makeSignature(s);
}

ai::ReferenceEntry entry(const SigSpec& s, const char* title) {
    ai::ReferenceEntry e;
    e.artist = "Mesure";
    e.title = title; // aucun genre : seule la mesure parle
    e.signature = makeSignature(s);
    return e;
}

/// Réf « mur de briques » : très forte, très tassée, sub dense, qui pompe (trames
/// très mobiles dans le bas-médium, sections plates), côtés larges et vifs.
SigSpec wallSpec() {
    SigSpec s;
    s.lufs = -4.0;
    s.plr = 4.5;
    s.lra = 3.0;
    s.subBoostDb = 7.7; // ~70 % de l'énergie sous 120 Hz
    s.spread = {22.0, 16.0, 16.0, 9.0};
    s.micro = {6.4, 6.4, 4.3, 9.5};
    s.sideMinusMidHf = -5.0;
    s.bpm = 145.0;
    return s;
}

/// Réf « aérée » : PLR 12 dB, sonie modérée, sub tenu (faible dynamique sous 120 Hz,
/// note tenue), côtés étroits.
SigSpec airySpec() {
    SigSpec s;
    s.lufs = -13.0;
    s.plr = 12.0;
    s.lra = 8.0;
    s.subBoostDb = 4.0;
    s.spread = {12.0, 14.0, 14.0, 13.0};
    s.micro = {5.0, 6.0, 6.0, 7.0};
    s.sideMinusMidHf = -18.0;
    s.bpm = 90.0;
    return s;
}

void show(const char* name, const ai::ConsolePlan& p) {
    std::printf("      %s\n", name);
    for (const auto& s : p.said) std::printf("        %s\n", s.c_str());
}

} // namespace

OMNIS_TEST("réf mesurée: deux références opposées -> deux topologies opposées (clipper, Make-Room, Inflator 808, auto-release)") {
    const auto prod = userProd();
    const auto wall = entry(wallSpec(), "Mur");
    const auto airy = entry(airySpec(), "Aérée");
    const auto planWall = ai::planReference(prod, {&wall}, "réf mur", defaultCurrent);
    const auto planAiry = ai::planReference(prod, {&airy}, "réf aérée", defaultCurrent);
    show("réf mur :", planWall);
    show("réf aérée :", planAiry);

    // Clipper : franc (Hard ou Diode) avec Sub Make-Room contre un mur ; genou doux sans Make-Room sinon.
    const double modeWall = settingOr(planWall, ParamId::ClipMode), modeAiry = settingOr(planAiry, ParamId::ClipMode);
    OMNIS_CHECK(modeWall == 0.0 || modeWall == 2.0);
    OMNIS_CHECK(modeAiry == 1.0);
    OMNIS_CHECK(settingOr(planWall, ParamId::ClipSubRoom) > 50.0);
    OMNIS_CHECK(settingOr(planAiry, ParamId::ClipSubRoom) == 0.0);
    const double kneeAiry = settingOr(planAiry, ParamId::ClipKnee);
    OMNIS_CHECK(kneeAiry >= 50.0 && kneeAiry <= 80.0);
    OMNIS_CHECK(settingOr(planWall, ParamId::ClipSubDrive) > 0.5);

    // Phase du sub : jamais Analog contre une réf écrêtée.
    OMNIS_CHECK(settingOr(planWall, ParamId::SubPhaseMode) == 0.0);

    // Inflator 808 : la 808 qui plafonne est densifiée face au mur, pas face à la réf aérée.
    OMNIS_CHECK(settingOr(planWall, ParamId::InflLowEffect) > 20.0);
    OMNIS_CHECK(settingOr(planWall, ParamId::InflCurve) < 0.0);
    OMNIS_CHECK(settingOr(planAiry, ParamId::InflLowEffect) == 0.0);

    // Auto-release : la réf qui pompe le baisse (15 – 30 %), le sub tenu le force à 100 %.
    const double arWall = settingOr(planWall, ParamId::LimAutoRelease), arAiry = settingOr(planAiry, ParamId::LimAutoRelease);
    OMNIS_CHECK(arWall >= 15.0 && arWall <= 30.0);
    OMNIS_CHECK(arAiry == 100.0);

    // Relâchements sur le tempo de la prod (140 BPM) : croche 214 ms pour le sub,
    // double-croche 107 ms pour l'aigu ; limiteur vers la croche quand ça pompe.
    OMNIS_CHECK(std::fabs(settingOr(planWall, params::mbId(0, params::MbControl::Release)) - 60000.0 / 140.0 / 2.0) < 0.5);
    OMNIS_CHECK(std::fabs(settingOr(planWall, params::mbId(3, params::MbControl::Release)) - 60000.0 / 140.0 / 4.0) < 0.5);
    OMNIS_CHECK(settingOr(planWall, ParamId::LimRelease) > settingOr(planAiry, ParamId::LimRelease));

    // Multibande : la prod bouge plus que le mur dans le sub et le bas -> ratio > 1 ;
    // la réf aérée bouge plus que la prod dans le bas -> pas de compression.
    OMNIS_CHECK(settingOr(planWall, params::mbId(0, params::MbControl::Ratio)) > 1.2);
    OMNIS_CHECK(settingOr(planAiry, params::mbId(1, params::MbControl::Ratio)) == 1.0);

    // Côtés larges et vifs dans le mur : limiteur délié sur les attaques ; étroits : lié.
    OMNIS_CHECK(settingOr(planWall, ParamId::LimUnlinkTransient) >= 40.0);
    OMNIS_CHECK(settingOr(planAiry, ParamId::LimUnlinkTransient) < 5.0);
}

OMNIS_TEST("réf mesurée: les règles sont continues — le genou se ferme et le Make-Room monte quand le PLR de la réf baisse") {
    const auto prod = userProd();
    double lastKnee = 1e9, lastRoom = -1.0;
    for (double plr = 10.0; plr >= 5.0; plr -= 0.5) {
        SigSpec s = wallSpec();
        s.plr = plr;
        const auto e = entry(s, "Balayage");
        const auto plan = ai::planReference(prod, {&e}, "balayage", defaultCurrent);
        const double knee = settingOr(plan, ParamId::ClipKnee), room = settingOr(plan, ParamId::ClipSubRoom);
        OMNIS_CHECK(knee <= lastKnee + 1e-9);
        OMNIS_CHECK(room >= lastRoom - 1e-9);
        // Pas de saut : deux pas de 0,5 dB ne changent pas le Make-Room de plus de 30 points.
        if (lastRoom >= 0.0) OMNIS_CHECK(room - lastRoom <= 30.0);
        lastKnee = knee;
        lastRoom = room;
    }
    OMNIS_CHECK(lastRoom > 50.0);
}

OMNIS_TEST("réf mesurée: une prod non entendue ne change aucune topologie") {
    analysis::SpectralSignature silent; // activeFrames = 0
    const auto wall = entry(wallSpec(), "Mur");
    ai::ConsolePlan p;
    ai::applyReferenceDynamics(p, silent, ai::aggregateSignature({&wall}), wall.signature.music, defaultCurrent);
    OMNIS_CHECK(p.settings.empty());
}

OMNIS_TEST("diagnostic: « déjà écrasé » jugé face à la référence (une réf à -4 LUFS / PLR 4,5 n'alerte pas une prod à -6 / 6)") {
    SigSpec loud;
    loud.lufs = -6.0;
    loud.plr = 6.0;
    const auto prod = makeSignature(loud);
    const auto wall = makeSignature(wallSpec());
    const auto airy = makeSignature(airySpec());
    const auto crushed = [](const std::vector<ai::MixFinding>& f) {
        for (const auto& x : f)
            if (x.problem.find("écrasé") != std::string::npos) return true;
        return false;
    };
    OMNIS_CHECK(!crushed(ai::diagnoseMix(prod, &wall)));
    OMNIS_CHECK(crushed(ai::diagnoseMix(prod, &airy)));
    // Plus forte et plus tassée que le mur lui-même : l'alerte revient.
    SigSpec beyond = loud;
    beyond.lufs = -3.0;
    beyond.plr = 3.5;
    OMNIS_CHECK(crushed(ai::diagnoseMix(makeSignature(beyond), &wall)));
}

OMNIS_TEST("réf mesurée: le compte rendu passe en anglais") {
    const auto prod = userProd();
    const auto wall = entry(wallSpec(), "Mur");
    const auto airy = entry(airySpec(), "Aérée");
    i18n::overrideLanguageForTests(i18n::Lang::En);
    std::string text;
    for (const auto* e : {&wall, &airy})
        for (const auto& s : ai::planReference(prod, {e}, "ref", defaultCurrent).said) text += i18n::tr(s) + "\n";
    i18n::overrideLanguageForTests(i18n::Lang::Fr);
    std::printf("%s", text.c_str());
    for (const char* french : {"réf très tassée", "multibande à la tenue", "relâchements", "la réf pompe", "plafonne", "côtés larges", "sub de la réf tenu", "réf aérée"})
        OMNIS_CHECK(text.find(french) == std::string::npos);
}

OMNIS_TEST("réf mesurée: la pompe mesurée sur le tempo prime sur l'indice des trames") {
    const auto prod = userProd();
    // Trames immobiles (l'indice dirait « ne pompe pas »), mais la fiche mesure +4 dB.
    SigSpec calm = airySpec();
    calm.spread = {30.0, 9.0, 9.0, 9.0}; // sub non tenu : seule la pompe décide
    auto pumped = entry(calm, "Pompe mesurée");
    pumped.signature.music.pumpDb = 4.0;
    pumped.signature.music.hasPump = true;
    // Trames très mobiles et sections plates (l'indice dirait « pompe »), mais la fiche mesure 0.
    SigSpec busy = wallSpec();
    auto flat = entry(busy, "Pas de pompe");
    flat.signature.music.pumpDb = 0.0;
    flat.signature.music.hasPump = true;
    const double arPumped = settingOr(ai::planReference(prod, {&pumped}, "pompe", defaultCurrent), ParamId::LimAutoRelease);
    const double arFlat = settingOr(ai::planReference(prod, {&flat}, "plat", defaultCurrent), ParamId::LimAutoRelease);
    std::printf("      pompe mesurée +4 dB : auto-release %.0f %% · mesurée 0 dB : %.0f %%\n", arPumped, arFlat);
    OMNIS_CHECK(arPumped <= 25.0);
    OMNIS_CHECK(arFlat == 100.0);
}
