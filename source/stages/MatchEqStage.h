// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/MatchEqStage.h — Étage 2 : Match EQ M/S à phase linéaire.
//
//  Applique la courbe d'intention tonale (Mid et Side, 21 nœuds chacun) avant
//  toute dynamique : la prod est déplacée VERS la référence, délibérément et
//  en partie (voir ai/MatchIntent.h), puis seulement poussée en sonie. Faire
//  ce déplacement par la distorsion du clipper est exactement ce qui gonflait
//  les médiums (calibration module 3, §7).
//
//  Phase linéaire : la forme d'onde de la 808 et des transitoires est
//  conservée (aucune rotation de phase grave, pas de hausse du true peak) ;
//  Mid et Side restent alignés. Convolution partitionnée UPOLS, noyaux
//  recalculés en tâche de fond quand la courbe change et fondus sur une trame.
//
//  Latence : N/2 + B, comme le Sub Control (8704 échantillons à 44.1 kHz).
// =============================================================================
#pragma once

#include "MatchKernelBank.h"

#include "dsp/StereoConvolver.h"
#include "engine/Stage.h"

#include <vector>

namespace omnis {

class MatchEqStage final : public Stage {
public:
    MatchEqStage();
    ~MatchEqStage() override;

    [[nodiscard]] const char* name() const noexcept override { return "Match EQ"; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(double* const* channels, int numSamples) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override { return latency_; }
    void setParameter(params::ParamId id, double plainValue, bool immediate) noexcept override;
    void setRealtime(bool realtime) noexcept override { realtime_ = realtime; }

private:
    void frameBoundary() noexcept;

    double sampleRate_ = 48000.0;
    bool realtime_ = false;
    int latency_ = 0;

    MatchKernelRequest request_; ///< courbe courante (écrite par setParameter)

    dsp::StereoConvolver convolver_;
    MatchKernelBank bank_;
    MatchKernelBank::Slot* activeSlot_ = nullptr;
    MatchKernelBank::Slot* fadingSlot_ = nullptr;
    std::vector<double> fadeRamp_;
    int framePos_ = 0;
};

} // namespace omnis
