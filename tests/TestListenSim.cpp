// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestListenSim.cpp — Écoutes simulées : courbes, spatial, plateformes,
//  et jamais dans un export.
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"
#include "engine/Engine.h"
#include "engine/ListenSim.h"
#include "engine/MasteringChain.h"
#include "params/Parameters.h"

#include <cmath>
#include <numbers>
#include <vector>

using namespace omnis;

namespace {
constexpr double kFs = 48000.0;

/// Niveau RMS (dB) d'un sinus `f` passé dans l'écoute `preset`.
double toneDb(int preset, double f, double amplitude = 0.25) {
    ListenSim sim;
    sim.setPreset(preset);
    sim.prepare(kFs, 512);
    const int n = static_cast<int>(kFs);
    std::vector<double> l(static_cast<std::size_t>(n)), r(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] = amplitude * std::sin(2.0 * std::numbers::pi * f * i / kFs);
    for (int pos = 0; pos < n; pos += 512) {
        double* ch[2] = {l.data() + pos, r.data() + pos};
        sim.process(ch, std::min(512, n - pos), -30.0);
    }
    double e = 0.0;
    for (int i = n / 2; i < n; ++i) e += l[static_cast<std::size_t>(i)] * l[static_cast<std::size_t>(i)];
    return 10.0 * std::log10(e / (n / 2) + 1e-30) - 20.0 * std::log10(amplitude / std::sqrt(2.0));
}
} // namespace

OMNIS_TEST("écoute simulée: les étiquettes suivent les préréglages") {
    OMNIS_CHECK(ListenSim::labels().size() == static_cast<std::size_t>(ListenSim::Count));
}

OMNIS_TEST("écoute simulée: téléphone sans grave, AirPods ouverts avec peu de sub, studio presque neutre") {
    const double phone50 = toneDb(ListenSim::Phone, 50.0), phone2k = toneDb(ListenSim::Phone, 2500.0);
    const double air40 = toneDb(ListenSim::AirPods, 40.0), pro40 = toneDb(ListenSim::AirPodsPro, 40.0);
    const double hd1k = toneDb(ListenSim::Hd600, 1000.0), beats60 = toneDb(ListenSim::BeatsStudioPro, 60.0);
    std::printf("    téléphone 50 Hz %.1f dB / 2,5 kHz %+.1f ; AirPods 40 Hz %.1f, Pro %+.1f ; HD 600 1 kHz %+.1f ; Beats 60 Hz %+.1f\n",
                phone50, phone2k, air40, pro40, hd1k, beats60);
    OMNIS_CHECK(phone50 < -30.0 && phone2k > 2.0);
    OMNIS_CHECK(air40 < -5.0 && pro40 > 1.0);
    OMNIS_CHECK(std::fabs(hd1k) < 0.6);
    OMNIS_CHECK(beats60 > 3.5);
}

OMNIS_TEST("écoute simulée: « Off » ne touche à rien, le spatial reste fini et du même niveau") {
    const double off = toneDb(ListenSim::Off, 1000.0);
    OMNIS_CHECK(std::fabs(off) < 1e-9);
    const double sp = toneDb(ListenSim::Spatial, 1000.0);
    std::printf("    spatial binaural à 1 kHz : %+.1f dB\n", sp);
    OMNIS_CHECK(std::isfinite(sp) && std::fabs(sp) < 4.0);
}

OMNIS_TEST("écoute simulée: une plateforme baisse un master fort vers sa cible, jamais dans un export") {
    const auto in = tools::signals::drumLoop(kFs, 6);
    for (const bool offline : {false, true}) {
        Engine e;
        buildMasteringChain(e);
        e.setRealtimeMode(!offline);
        e.prepare(kFs, 512);
        e.setParameterImmediate(params::ParamId::LimGain, 10.0); // master fort (~ -7 LUFS)
        e.setParameterImmediate(params::ParamId::ListenSim, static_cast<double>(ListenSim::Spotify));
        const int n = in.numFrames();
        std::vector<double> l(in.channels[0]), r(in.channels[1]);
        double peakEnd = 0.0;
        for (int pos = 0; pos < n; pos += 512) {
            double* ch[2] = {l.data() + pos, r.data() + pos};
            e.process(ch, ch, std::min(512, n - pos));
        }
        for (int i = n - static_cast<int>(kFs); i < n; ++i) peakEnd = std::max(peakEnd, std::fabs(l[static_cast<std::size_t>(i)]));
        std::printf("    %s : crête de fin %.1f dBFS\n", offline ? "export" : "écoute", 20.0 * std::log10(peakEnd));
        if (offline) OMNIS_CHECK(20.0 * std::log10(peakEnd) > -2.0); // export : le vrai master, intact
        else OMNIS_CHECK(20.0 * std::log10(peakEnd) < -4.0);         // écoute : baissé vers -14 LUFS
    }
}
