// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestEngine.cpp — Moteur : null-test, PDC, bypass, monitoring.
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "core/Decibels.h"
#include "core/DelayLine.h"
#include "engine/Engine.h"
#include "engine/MasteringChain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

using namespace omnis;
using params::ParamId;

namespace {

/// Étage de test : retard pur de N échantillons (simule un FIR à phase
/// linéaire ou un lookahead) — permet de vérifier la compensation de latence.
class PureDelayStage final : public Stage {
public:
    explicit PureDelayStage(int samples) : samples_(samples) {}
    const char* name() const noexcept override { return "PureDelay"; }
    void prepare(const ProcessSpec& spec) override {
        delay_.prepare(spec.numChannels, samples_);
        delay_.setDelay(samples_);
    }
    void reset() noexcept override { delay_.reset(); }
    void process(double* const* channels, int numSamples) noexcept override { delay_.process(channels, 2, numSamples); }
    int latencySamples() const noexcept override { return samples_; }

private:
    int samples_;
    DelayLine delay_;
};

template <typename Sample>
std::vector<std::vector<Sample>> render(Engine& engine, const tools::AudioFile& in, bool randomBlocks,
                                        int toggleBypassEvery = 0) {
    const int n = in.numFrames();
    std::vector<std::vector<Sample>> io(2, std::vector<Sample>(static_cast<std::size_t>(n)));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
            io[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] =
                static_cast<Sample>(in.channels[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)]);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(1, engine.maxBlockSize() * 2); // y compris > maxBlockSize
    int pos = 0, blocks = 0;
    bool bypass = false;
    while (pos < n) {
        const int len = std::min(n - pos, randomBlocks ? dist(rng) : engine.maxBlockSize());
        if (toggleBypassEvery > 0 && ++blocks % toggleBypassEvery == 0) {
            bypass = !bypass;
            engine.setParameter(ParamId::Bypass, bypass ? 1.0 : 0.0);
        }
        // Traitement en place, comme beaucoup d'hôtes.
        Sample* ptrs[2] = {io[0].data() + pos, io[1].data() + pos};
        engine.process(ptrs, ptrs, len);
        pos += len;
    }
    return io;
}

} // namespace

OMNIS_TEST("engine: chaîne neutre = null-test bit-exact (64 et 32 bits)") {
    const auto in = tools::signals::pinkNoise(48000.0, 2.0, -12.0);
    {
        Engine e;
        e.prepare(48000.0, 256);
        const auto out = render<double>(e, in, true);
        bool exact = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < in.numFrames(); ++i)
                if (out[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] !=
                    in.channels[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)])
                    exact = false;
        OMNIS_CHECK(exact);
    }
    {
        Engine e;
        e.prepare(48000.0, 256);
        const auto out = render<float>(e, in, true);
        bool exact = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < in.numFrames(); ++i)
                if (out[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] !=
                    static_cast<float>(in.channels[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)]))
                    exact = false;
        OMNIS_CHECK(exact);
    }
}

OMNIS_TEST("engine: latence = somme des étages, bypass aligné et sans clic") {
    constexpr int kLatency1 = 137, kLatency2 = 2048;
    Engine e;
    e.appendStage(std::make_unique<PureDelayStage>(kLatency1));
    e.appendStage(std::make_unique<PureDelayStage>(kLatency2));
    e.prepare(44100.0, 512);
    OMNIS_CHECK(e.latencySamples() == kLatency1 + kLatency2);

    // Traité (retard pur) et sec (retardé par la PDC) sont identiques : basculer
    // le bypass sans arrêt ne doit produire AUCUNE différence avec l'entrée
    // retardée. Toute erreur d'alignement apparaîtrait comme un écart.
    const auto in = tools::signals::drumLoop(44100.0, 1);
    const auto out = render<double>(e, in, true, 3);
    const int L = kLatency1 + kLatency2;
    double maxError = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < in.numFrames(); ++i) {
            const double expected = i < L ? 0.0 : in.channels[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i - L)];
            maxError = std::max(maxError, std::fabs(out[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] - expected));
        }
    OMNIS_CHECK(maxError < 1e-15);
}

OMNIS_TEST("engine: gain de sortie +6.02 dB = x2 après lissage") {
    Engine e;
    e.prepare(48000.0, 512);
    e.setParameter(ParamId::OutputGain, gainToDb(2.0));
    const auto in = tools::signals::whiteNoise(48000.0, 1.0, -20.0);
    const auto out = render<double>(e, in, false);
    const std::size_t i = 40000; // bien après le temps de lissage (30 ms)
    OMNIS_CHECK_NEAR(out[0][i], 2.0 * in.channels[0][i], 1e-12);
    OMNIS_CHECK_NEAR(out[1][i], 2.0 * in.channels[1][i], 1e-12);
}

OMNIS_TEST("engine: monitoring Side sur signal mono = silence, Mid sur anti-phase = silence") {
    for (int mode : {1, 2}) {
        Engine e;
        e.prepare(48000.0, 512);
        e.setParameterImmediate(ParamId::Monitor, mode == 2 ? 2.0 : 1.0);
        auto in = tools::signals::pinkNoise(48000.0, 1.0, -10.0, 7, false); // L = R
        if (mode == 1)
            for (double& v : in.channels[1]) v = -v; // L = -R
        const auto out = render<double>(e, in, true);
        double peak = 0.0;
        for (const auto& ch : out)
            for (double v : ch) peak = std::max(peak, std::fabs(v));
        OMNIS_CHECK(peak < 1e-15);
    }
}

OMNIS_TEST("engine: les mesures portent sur la sortie (avant monitoring)") {
    Engine e;
    e.prepare(48000.0, 480);
    e.setParameterImmediate(ParamId::Monitor, 2.0); // Side seul -> silence en sortie...
    const auto in = tools::signals::ebu3341(48000.0, 1);
    render<double>(e, in, false);
    // ... mais la sonie du master reste -23 LUFS.
    OMNIS_CHECK_NEAR(e.meters().integratedLufs, -23.0, 0.1);
    OMNIS_CHECK_NEAR(e.meters().correlation, 1.0, 1e-6);
}

OMNIS_TEST("engine: sortie indépendante du découpage en blocs (lissage déterministe)") {
    const auto in = tools::signals::drumLoop(48000.0, 1);
    Engine a, b;
    for (Engine* e : {&a, &b}) {
        e->prepare(48000.0, 333);
        e->setParameter(ParamId::InputGain, -6.0);
        e->setParameter(ParamId::Monitor, 1.0);
    }
    const auto x = render<double>(a, in, false);
    const auto y = render<double>(b, in, true);
    bool identical = true;
    for (int ch = 0; ch < 2; ++ch)
        if (x[static_cast<std::size_t>(ch)] != y[static_cast<std::size_t>(ch)]) identical = false;
    OMNIS_CHECK(identical);
}

OMNIS_TEST("engine: NaN et infinis de l'hôte interceptés, la chaîne complète continue de jouer") {
    Engine e;
    buildMasteringChain(e);
    e.prepare(44100.0, 512);
    e.setParameterImmediate(ParamId::InflLowEffect, 50.0);
    e.setParameterImmediate(ParamId::ClipDrive, 6.0);
    e.setParameterImmediate(ParamId::LimGain, 6.0);
    std::vector<double> l(512), r(512);
    double* io[2] = {l.data(), r.data()};
    double lastPeak = 0.0;
    bool allFinite = true;
    for (int block = 0; block < 400; ++block) {
        for (int i = 0; i < 512; ++i) {
            const double t = (block * 512 + i) / 44100.0;
            l[static_cast<std::size_t>(i)] = 0.3 * std::sin(2.0 * 3.141592653589793 * 220.0 * t);
            r[static_cast<std::size_t>(i)] = l[static_cast<std::size_t>(i)];
        }
        if (block == 50) {
            l[10] = std::numeric_limits<double>::quiet_NaN();
            r[20] = std::numeric_limits<double>::infinity();
            l[30] = -std::numeric_limits<double>::infinity();
            r[40] = 1e300;
        }
        e.process(io, io, 512);
        double peak = 0.0;
        for (int i = 0; i < 512; ++i) {
            allFinite = allFinite && std::isfinite(l[static_cast<std::size_t>(i)]) && std::isfinite(r[static_cast<std::size_t>(i)]);
            peak = std::max(peak, std::fabs(l[static_cast<std::size_t>(i)]));
        }
        lastPeak = peak;
    }
    OMNIS_CHECK(allFinite);
    OMNIS_CHECK(lastPeak > 0.1); // le son est revenu (pas de silence permanent)
    OMNIS_CHECK(std::isfinite(e.meters().integratedLufs) || e.meters().integratedLufs < -100.0);
    OMNIS_CHECK(Engine::sanitizeInput(std::numeric_limits<double>::quiet_NaN()) == 0.0);
    OMNIS_CHECK(Engine::sanitizeInput(0.25) == 0.25);
}
