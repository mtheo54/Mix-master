// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestI18n.cpp — L'interface en anglais : textes entiers, morceaux dans
//  un texte composé, formats remplis après traduction, diagnostic.
// =============================================================================
#include "TestFramework.h"

#include "ai/MixDiagnosis.h"
#include "core/I18n.h"

using namespace omnis;

OMNIS_TEST("anglais: textes entiers, morceaux, formats ; français inchangé") {
    i18n::overrideLanguageForTests(i18n::Lang::En);
    OMNIS_CHECK(i18n::tr("SUIVANT") == "NEXT");
    OMNIS_CHECK(i18n::tr("Stems liés : 3") == "Linked stems: 3");
    OMNIS_CHECK(i18n::tr("GR -2.0 dB · PAS DERNIER") == "GR -2.0 dB · NOT LAST");
    OMNIS_CHECK(i18n::tr("SUBSTANCE") == "SUBSTANCE"); // pas de morceau au milieu d'un mot
    analysis::SpectralSignature s;
    s.music.valid = true;
    s.music.lowCorrelation = 0.2;
    s.music.samplePeakDb = -6.0;
    s.integratedLufs = -14.0;
    s.plrDb = 12.0;
    const std::string en = ai::formatDiagnosis(ai::diagnoseMix(s, &s));
    std::printf("    %s", en.c_str());
    OMNIS_CHECK(en.find("The low end isn't mono (correlation 0.20 below 120 Hz).") != std::string::npos);
    i18n::overrideLanguageForTests(i18n::Lang::Fr);
    OMNIS_CHECK(i18n::tr("SUIVANT") == "SUIVANT");
    OMNIS_CHECK(ai::formatDiagnosis(ai::diagnoseMix(s, &s)).find("Le grave n'est pas mono") != std::string::npos);
}
