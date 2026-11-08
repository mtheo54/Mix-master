// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestProdListener.cpp — L'écoute d'AUTO (engine/ProdListener) : le
//  morceau entier jusqu'à l'arrêt de la lecture, un second clic qui termine,
//  le silence de fin de morceau, et le passage le plus fort gardé pour les
//  essais (15 s, dans l'ordre, au bon niveau).
// =============================================================================
#include "TestFramework.h"

#include "engine/ProdListener.h"

#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>
#include <vector>

using namespace omnis;

namespace {

constexpr double kRate = 48000.0;

/// Pousse `seconds` d'un sinus 220 Hz à `dbfs` (crête), par blocs de 512, ~8x le temps réel.
void play(ProdListener& listener, double seconds, double dbfs, double& phase) {
    const double amp = std::pow(10.0, dbfs / 20.0);
    std::vector<float> l(512), r(512);
    const int blocks = static_cast<int>(seconds * kRate / 512.0);
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < 512; ++i) {
            l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] = static_cast<float>(amp * std::sin(phase));
            phase += 2.0 * std::numbers::pi * 220.0 / kRate;
        }
        listener.push(l.data(), r.data(), 512);
        if (b % 12 == 11) std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

bool waitFor(const ProdListener& listener, ProdListener::AutoState state, double seconds) {
    const auto t0 = std::chrono::steady_clock::now();
    while (listener.autoState() != state) {
        if (std::chrono::steady_clock::now() - t0 > std::chrono::duration<double>(seconds)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

double rmsDb(const std::vector<float>& x, std::size_t from, std::size_t count) {
    double sum = 0.0;
    for (std::size_t i = from; i < from + count; ++i) sum += static_cast<double>(x[i]) * x[i];
    return 10.0 * std::log10(std::max(1e-30, sum / static_cast<double>(count)));
}

} // namespace

OMNIS_TEST("AUTO écoute: le morceau entier jusqu'à l'arrêt ; le passage le plus fort (15 s) est gardé dans l'ordre") {
    ProdListener listener;
    listener.prepare(kRate);
    listener.startAuto();
    OMNIS_CHECK(listener.autoState() == ProdListener::AutoState::Waiting);
    double phase = 0.0;
    play(listener, 10.0, -30.0, phase); // couplet calme
    play(listener, 16.0, -6.0, phase);  // refrain fort
    play(listener, 10.0, -30.0, phase); // outro calme
    OMNIS_CHECK(waitFor(listener, ProdListener::AutoState::Listening, 2.0) || listener.autoState() == ProdListener::AutoState::Done);
    std::printf("      écouté %.1f s\n", listener.autoSeconds());
    OMNIS_CHECK(listener.autoSeconds() > 30.0); // tout le morceau, pas 10 s
    // Plus rien n'arrive (lecture arrêtée) : l'écoute se termine d'elle-même.
    OMNIS_CHECK(waitFor(listener, ProdListener::AutoState::Done, 4.0));
    analysis::SpectralSignature sig;
    std::vector<float> excerpt;
    OMNIS_CHECK(listener.takeAutoResult(sig, &excerpt));
    OMNIS_CHECK(listener.autoState() == ProdListener::AutoState::Idle);
    const std::size_t frames = excerpt.size() / 2;
    std::printf("      passage gardé : %.1f s, %.1f dB RMS\n", frames / kRate, rmsDb(excerpt, 0, excerpt.size()));
    OMNIS_CHECK(frames == static_cast<std::size_t>(ProdListener::kExcerptSeconds * kRate));
    // Le refrain (sinus à -6 dB crête : -9 dB RMS), chaque seconde.
    const auto second = static_cast<std::size_t>(2 * kRate);
    for (std::size_t s = 0; s + second <= excerpt.size(); s += second) OMNIS_CHECK(std::fabs(rmsDb(excerpt, s, second) + 9.03) < 0.5);
    // Dans l'ordre : pas de saut dans le sinus (l'écart d'un échantillon au suivant reste petit).
    double jump = 0.0;
    for (std::size_t i = 2; i < excerpt.size(); i += 2) jump = std::max(jump, static_cast<double>(std::fabs(excerpt[i] - excerpt[i - 2])));
    OMNIS_CHECK(jump < 0.03);
    listener.release();
}

OMNIS_TEST("AUTO écoute: un second clic termine dès 4 s ; avant, l'écoute continue ; la lecture arrêtée tôt n'arrête rien") {
    ProdListener listener;
    listener.prepare(kRate);
    listener.startAuto();
    double phase = 0.0;
    play(listener, 2.0, -12.0, phase);
    OMNIS_CHECK(waitFor(listener, ProdListener::AutoState::Listening, 2.0));
    listener.finishAuto();
    // 2 s seulement, et moins de kAutoMinSeconds : ni le clic ni l'arrêt ne terminent.
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    OMNIS_CHECK(listener.autoState() == ProdListener::AutoState::Listening);
    play(listener, 3.0, -12.0, phase);
    OMNIS_CHECK(waitFor(listener, ProdListener::AutoState::Done, 3.0));
    analysis::SpectralSignature sig;
    std::vector<float> excerpt;
    OMNIS_CHECK(listener.takeAutoResult(sig, &excerpt));
    std::printf("      fini à %.1f s, passage %.1f s\n", listener.autoSeconds(), excerpt.size() / 2 / kRate);
    OMNIS_CHECK(excerpt.size() / 2 + 1024 >= static_cast<std::size_t>(4.0 * kRate)); // tout ce qui a été entendu
    // Un nouvel AUTO repart de zéro (le clic précédent est oublié).
    listener.startAuto();
    play(listener, 5.0, -12.0, phase);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    OMNIS_CHECK(listener.autoState() == ProdListener::AutoState::Listening);
    listener.cancelAuto();
    listener.release();
}

OMNIS_TEST("AUTO écoute: la lecture qui tourne sur du silence (fin du morceau) termine l'écoute") {
    ProdListener listener;
    listener.prepare(kRate);
    listener.startAuto();
    double phase = 0.0;
    play(listener, 13.0, -12.0, phase);
    // Du silence, au temps réel, jusqu'à ce qu'AUTO conclue (kEndSilenceSeconds).
    std::vector<float> zero(512, 0.0f);
    const auto t0 = std::chrono::steady_clock::now();
    while (listener.autoState() != ProdListener::AutoState::Done && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10)) {
        listener.push(zero.data(), zero.data(), 512);
        std::this_thread::sleep_for(std::chrono::microseconds(10667));
    }
    const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("      fin après %.1f s de silence\n", waited);
    OMNIS_CHECK(listener.autoState() == ProdListener::AutoState::Done);
    OMNIS_CHECK(waited > ProdListener::kEndSilenceSeconds - 1.0 && waited < ProdListener::kEndSilenceSeconds + 2.5);
    listener.release();
}
