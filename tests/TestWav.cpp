// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestWav.cpp — Entrées/sorties WAV et signaux de test.
// =============================================================================
#include "TestFramework.h"

#include "AnalysisReport.h"
#include "TestSignals.h"
#include "WavFile.h"

#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace omnis::tools;

OMNIS_TEST("wav: aller-retour f64 exact, f32 / pcm24 / pcm16 à la quantification près") {
    const auto src = signals::pinkNoise(44100.0, 0.5, -20.0); // crêtes < 0 dBFS
    const auto dir = std::filesystem::temp_directory_path();
    struct Case {
        SampleFormat format;
        double tolerance;
        const char* name;
    } cases[] = {{SampleFormat::Float64, 0.0, "f64"},
                 {SampleFormat::Float32, 1e-7, "f32"},
                 {SampleFormat::Pcm24, 0.5 / 8388608.0 + 1e-12, "pcm24"},
                 {SampleFormat::Pcm16, 0.5 / 32768.0 + 1e-12, "pcm16"}};
    for (const auto& c : cases) {
        const auto path = (dir / (std::string("omnis_test_") + c.name + ".wav")).string();
        std::string error;
        OMNIS_CHECK(writeWav(path, src, c.format, error));
        AudioFile back;
        OMNIS_CHECK(readWav(path, back, error));
        OMNIS_CHECK(back.numChannels() == 2 && back.numFrames() == src.numFrames() && back.sampleRate == 44100.0);
        double maxError = 0.0;
        for (int ch = 0; ch < 2 && back.numChannels() == 2; ++ch)
            for (int i = 0; i < src.numFrames() && i < back.numFrames(); ++i)
                maxError = std::max(maxError, std::fabs(back.channels[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] -
                                                        src.channels[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)]));
        OMNIS_CHECK(maxError <= c.tolerance);
        std::filesystem::remove(path);
    }
}

OMNIS_TEST("signaux: tous les générateurs produisent un signal fini et non vide") {
    for (const auto& name : signals::availableSignals()) {
        AudioFile f;
        std::string description;
        OMNIS_CHECK(signals::generate(name, 44100.0, f, description));
        OMNIS_CHECK(f.numFrames() > 0 && !description.empty());
        bool finite = true;
        double peak = 0.0;
        for (const auto& ch : f.channels)
            for (double v : ch) {
                finite = finite && std::isfinite(v);
                peak = std::max(peak, std::fabs(v));
            }
        OMNIS_CHECK(finite);
        if (name != "sweep-inverse") OMNIS_CHECK(peak > 0.0 && peak <= 1.0);
    }
}

OMNIS_TEST("signaux: niveaux annoncés vérifiés par l'analyseur") {
    {
        const auto r = analyze(signals::pinkNoise(48000.0, 10.0, -20.0));
        OMNIS_CHECK_NEAR(r.channels[0].rmsDb, -20.0, 0.05);
        OMNIS_CHECK_NEAR(r.correlation, 0.0, 0.05);
    }
    {
        const auto r = analyze(signals::stereoImaging(48000.0, 5.0, -20.0));
        OMNIS_CHECK_NEAR(r.channels[0].rmsDb, -20.0, 0.1);
    }
    {
        const auto r = analyze(signals::intersamplePeak(48000.0, 2.0, -1.0));
        OMNIS_CHECK_NEAR(r.samplePeakDb, -4.01, 0.01);
        OMNIS_CHECK_NEAR(r.truePeakDb, -1.0, 0.05);
    }
    {
        // Bruit gaussien : skewness ~ 0, excès de kurtosis ~ 0.
        const auto r = analyze(signals::whiteNoise(48000.0, 10.0, -20.0));
        OMNIS_CHECK_NEAR(r.channels[0].skewness, 0.0, 0.02);
        OMNIS_CHECK_NEAR(r.channels[0].excessKurtosis, 0.0, 0.05);
    }
    {
        // Sinus pur : excès de kurtosis = -1.5, facteur de crête = 3.01 dB.
        AudioFile f;
        f.sampleRate = 48000.0;
        f.resize(2, 48000);
        for (int i = 0; i < 48000; ++i)
            f.channels[0][static_cast<std::size_t>(i)] = f.channels[1][static_cast<std::size_t>(i)] =
                0.5 * std::sin(2.0 * 3.141592653589793 * 100.0 * i / 48000.0);
        const auto r = analyze(f);
        OMNIS_CHECK_NEAR(r.channels[0].excessKurtosis, -1.5, 0.01);
        OMNIS_CHECK_NEAR(r.channels[0].crestFactorDb, 3.0103, 0.01);
        OMNIS_CHECK_NEAR(r.correlation, 1.0, 1e-9);
    }
}

OMNIS_TEST("sweep: sweep ⊗ inverse = réponse plate (0 dB ±0.5) dans la bande") {
    // Convolution directe sur un sweep court (8 kHz, 0.5 s) puis gain du
    // résultat à plusieurs fréquences : un système neutre doit donner 0 dB.
    constexpr double fs = 8000.0;
    const auto r = signals::logSweep(fs, 0.5, 50.0, 3000.0, -6.0, 20.0);
    const auto& x = r.sweep.channels[0];
    const auto& h = r.inverse.channels[0];
    std::vector<double> y(x.size() + h.size() - 1, 0.0);
    for (std::size_t i = 0; i < x.size(); ++i)
        for (std::size_t k = 0; k < h.size(); ++k) y[i + k] += x[i] * h[k];
    for (double f : {200.0, 500.0, 1000.0, 2000.0}) {
        double re = 0.0, im = 0.0;
        for (std::size_t n = 0; n < y.size(); ++n) {
            re += y[n] * std::cos(2.0 * 3.141592653589793 * f * static_cast<double>(n) / fs);
            im -= y[n] * std::sin(2.0 * 3.141592653589793 * f * static_cast<double>(n) / fs);
        }
        OMNIS_CHECK_NEAR(20.0 * std::log10(std::hypot(re, im)), 0.0, 0.5);
    }
}
