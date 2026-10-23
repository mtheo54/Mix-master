// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/ImagerStage.h — Étage 4 : Imager (largeur stéréo multibande).
//
//  Quatre bandes (bas, bas-médium, médium-aigu, aigu) séparées par trois
//  fréquences. Dans chaque bande, la largeur dose le Side :
//      0 % = mono, 100 % = inchangé, 200 % = Side +6 dB.
//  Le Mid n'est jamais touché : élargir n'enlève rien au centre (voix, 808).
//  Transitions douces (cosinus sur ±1/3 d'octave en log-fréquence) : pas de
//  marche entre deux bandes.
//
//  Phase linéaire, comme le Match EQ : le Side garde sa forme d'onde et reste
//  aligné sur le Mid (une séparation IIR ferait tourner la phase du Side
//  seul, donc l'image). Sous ~120 Hz, le sub est déjà mono (Sub Control).
//  Largeurs à 100 % (défaut) : noyau = impulsion exacte, sortie = entrée
//  retardée (à l'arrondi du passage Mid/Side près).
//
//  Latence : N/2 + B, N = 4096 à 48 kHz (bandes au-dessus de 100 Hz : une
//  résolution de 12 Hz suffit).
// =============================================================================
#pragma once

#include "dsp/FirKernelBank.h"
#include "dsp/LinearPhaseDesigner.h"
#include "dsp/StereoConvolver.h"
#include "engine/Stage.h"
#include "params/Parameters.h"

#include <array>
#include <vector>

namespace omnis {

inline constexpr int kImagerBands = 4;

struct ImagerKernelRequest {
    std::array<double, kImagerBands - 1> splitHz{200.0, 2000.0, 8000.0};
    std::array<double, kImagerBands> width{1.0, 1.0, 1.0, 1.0}; ///< gain du Side (1 = inchangé)

    [[nodiscard]] bool nearlyEquals(const ImagerKernelRequest& o) const noexcept;
    /// Gain du Side à la fréquence f.
    [[nodiscard]] double sideMagnitude(double frequency) const noexcept;
};

/// Canal A = Mid (inchangé), canal B = Side.
class ImagerKernelDesigner {
public:
    void prepare(double sampleRate, int kernelLength, int blockSize) { designer_.prepare(sampleRate, kernelLength, blockSize); }
    void design(const ImagerKernelRequest& r, dsp::StereoConvolver::Kernel& out) noexcept {
        designer_.design([](double) { return 1.0; }, [&](double f) { return r.sideMagnitude(f); }, out);
    }

private:
    dsp::LinearPhaseDesigner designer_;
};

class ImagerStage final : public Stage {
public:
    ImagerStage();
    ~ImagerStage() override;

    [[nodiscard]] const char* name() const noexcept override { return "Imager"; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(double* const* channels, int numSamples) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override { return latency_; }
    void setParameter(params::ParamId id, double plainValue, bool immediate) noexcept override;
    void setRealtime(bool realtime) noexcept override { realtime_ = realtime; }

    static int kernelLengthFor(double sampleRate) noexcept;

private:
    using Bank = dsp::FirKernelBank<ImagerKernelRequest, ImagerKernelDesigner>;

    void frameBoundary() noexcept;
    [[nodiscard]] ImagerKernelRequest orderedRequest() const noexcept;

    double sampleRate_ = 48000.0;
    bool realtime_ = false;
    int latency_ = 0;
    ImagerKernelRequest request_; ///< réglages tels que saisis

    dsp::StereoConvolver convolver_;
    Bank bank_;
    Bank::Slot* activeSlot_ = nullptr;
    Bank::Slot* fadingSlot_ = nullptr;
    std::vector<double> fadeRamp_;
    int framePos_ = 0;
};

} // namespace omnis
