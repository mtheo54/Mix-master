// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestFramework.h — Micro-framework de tests sans dépendance.
//
//    OMNIS_TEST("nom") { OMNIS_CHECK(cond); OMNIS_CHECK_NEAR(a, b, tol); }
//
//  omnis_tests [filtre]  : exécute les tests dont le nom contient « filtre ».
// =============================================================================
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace omnis::test {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

inline void reportFailure(const char* file, int line, const std::string& message) {
    ++failureCount();
    std::printf("    ECHEC %s:%d : %s\n", file, line, message.c_str());
}

} // namespace omnis::test

#define OMNIS_CONCAT_IMPL(a, b) a##b
#define OMNIS_CONCAT(a, b) OMNIS_CONCAT_IMPL(a, b)

#define OMNIS_TEST(name)                                                                                     \
    static void OMNIS_CONCAT(omnisTest_, __LINE__)();                                                        \
    static const ::omnis::test::Registrar OMNIS_CONCAT(omnisRegistrar_, __LINE__)(name,                       \
                                                                                  &OMNIS_CONCAT(omnisTest_, __LINE__)); \
    static void OMNIS_CONCAT(omnisTest_, __LINE__)()

#define OMNIS_CHECK(cond)                                                                                    \
    do {                                                                                                     \
        if (!(cond)) ::omnis::test::reportFailure(__FILE__, __LINE__, "condition fausse : " #cond);          \
    } while (0)

#define OMNIS_CHECK_NEAR(actual, expected, tolerance)                                                        \
    do {                                                                                                     \
        const double omnisA_ = static_cast<double>(actual);                                                  \
        const double omnisE_ = static_cast<double>(expected);                                                \
        if (!(std::fabs(omnisA_ - omnisE_) <= static_cast<double>(tolerance))) {                             \
            char omnisBuf_[256];                                                                             \
            std::snprintf(omnisBuf_, sizeof(omnisBuf_), "%s = %.10g, attendu %.10g +/- %.3g", #actual,       \
                          omnisA_, omnisE_, static_cast<double>(tolerance));                                 \
            ::omnis::test::reportFailure(__FILE__, __LINE__, omnisBuf_);                                     \
        }                                                                                                    \
    } while (0)
