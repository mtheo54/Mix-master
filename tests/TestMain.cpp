// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestMain.cpp
// =============================================================================
#include "TestFramework.h"

#include <chrono>
#include <cstring>
#include <exception>

int main(int argc, char** argv) {
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
