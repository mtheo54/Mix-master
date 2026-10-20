// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisProcessor.h — Composant audio VST3 (thread temps réel).
//
//  Rôle : adaptateur fin entre l'hôte VST3 et omnis::Engine.
//   - Automation échantillon-précise : le bloc hôte est découpé aux instants
//     exacts des points d'automation reçus, chaque segment étant traité avec
//     les valeurs en vigueur (puis lissées par l'Engine).
//   - Traitement natif 32 et 64 bits (kSample32 / kSample64).
//   - Latence annoncée à l'hôte (PDC) : somme des latences des étages.
//   - Mesures renvoyées au contrôleur via des paramètres de sortie en lecture
//     seule (seul canal processeur -> contrôleur garanti temps réel par la
//     norme ; l'interface NanoVG du module 6 utilisera en plus l'API
//     IDataExchange pour le scope et le spectre).
//   - Sonie intégrée remise à zéro à chaque démarrage du transport de l'hôte.
// =============================================================================
#pragma once

#include "engine/Engine.h"
#include "params/Parameters.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>
#include <vector>

namespace omnis::vst3 {

class OmnisProcessor : public Steinberg::Vst::AudioEffect {
public:
    OmnisProcessor();
    ~OmnisProcessor() override;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new OmnisProcessor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;

    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                                                     Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs,
                                                     Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override;

    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

private:
    struct ParamEvent {
        Steinberg::int32 sampleOffset;
        Steinberg::int32 sequence; ///< ordre d'arrivée (départage à offset égal)
        Steinberg::Vst::ParamID id;
        Steinberg::Vst::ParamValue normalized;
    };

    void collectParameterEvents(Steinberg::Vst::IParameterChanges* changes, Steinberg::int32 numSamples);
    void applyParameter(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue normalized, bool immediate);
    void applyPendingState();
    void handleTransport(const Steinberg::Vst::ProcessContext* context);
    void publishMeters(Steinberg::Vst::IParameterChanges* outputChanges, Steinberg::int32 numSamples);

    template <typename Sample>
    void processSegments(Sample** inputs, Sample** outputs, Steinberg::int32 numSamples);

    Engine engine_;

    // Valeurs normalisées courantes (lues par getState depuis le thread UI,
    // écrites par process() depuis le thread audio) -> atomiques.
    static constexpr std::size_t kMaxParams = 64;
    std::array<std::atomic<double>, kMaxParams> normalized_{};
    std::atomic<bool> pendingState_{false};

    // Événements d'automation du bloc courant, triés par offset. Capacité fixe :
    // au-delà, les points surnuméraires d'un même paramètre sont fusionnés.
    static constexpr int kMaxEvents = 2048;
    std::array<ParamEvent, kMaxEvents> events_{};
    int numEvents_ = 0;

    bool transportWasPlaying_ = false;
    int meterIntervalSamples_ = 1600;
    int meterCountdown_ = 0;
    std::array<double, 8> lastMeterValues_{};
};

} // namespace omnis::vst3
