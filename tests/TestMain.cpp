// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestMain.cpp
// =============================================================================
#include "TestFramework.h"

#include "core/I18n.h"

#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

int main(int argc, char** argv) {
    // Les tests vérifient les textes français : langue fixée, quel que soit le système.
    omnis::i18n::overrideLanguageForTests(omnis::i18n::Lang::Fr);
    // Jamais le vrai dossier de l'utilisateur (journal d'apprentissage, styles appris, base perso) :
    // un dossier temporaire neuf à chaque exécution.
    {
        const auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto dir = std::filesystem::temp_directory_path() / ("omnis_tests_" + stamp);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::string text = dir.string();
#if defined(_WIN32)
        _putenv_s("OMNIS_TEST_USER_DIR", text.c_str());
#else
        setenv("OMNIS_TEST_USER_DIR", text.c_str(), 1);
#endif
        omnis::test::restoreUserDataDir();
    }
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedTests = 0;
    for (const auto& test : omnis::test::registry()) {
        if (filter && test.name.find(filter) == std::string::npos) continue;
        const int before = omnis::test::failureCount();
        const auto t0 = std::chrono::steady_clock::now();
        std::printf("[ RUN  ] %s\n", test.name.c_str());
        std::fflush(stdout);
        // Une exception échoue le test au lieu d'arrêter tout le programme
        // (sous Windows, un arrêt ne laisse qu'un code 0xc0000409, sans message).
        try {
            test.body();
        } catch (const std::exception& e) {
            omnis::test::reportFailure(__FILE__, __LINE__, std::string("exception : ") + e.what());
        } catch (...) {
            omnis::test::reportFailure(__FILE__, __LINE__, "exception inconnue");
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const bool ok = omnis::test::failureCount() == before;
        std::printf("[ %s ] %s (%.1f ms)\n", ok ? " OK " : "FAIL", test.name.c_str(), ms);
        std::fflush(stdout);
        ++run;
        if (!ok) ++failedTests;
    }
    std::printf("\n%d test(s), %d echec(s)\n", run, failedTests);
    return failedTests == 0 && run > 0 ? 0 : 1;
}
