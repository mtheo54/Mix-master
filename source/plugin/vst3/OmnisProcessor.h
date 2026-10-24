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
//   - Entrée auxiliaire « Reference » (sidechain) : quand « Listen Reference »
//     est actif, la référence et la prod sont écoutées (engine/ReferenceCapture,
//     thread d'analyse). À l'arrêt, la référence entre dans la base de la
//     personne (core/UserDataDir.h) et ses signatures sont gardées dans l'état
//     de la session. Aucun audio n'est enregistré.
//   - Instances liées (link/LinkClient.h), réglage « Link Role » :
//       Seul   : comme avant ;
//       Master : la chaîne complète, et l'écoute des stems de son groupe ;
//       Stem   : sur une piste. AUCUNE latence (pas d'étage à phase linéaire
//                par piste) : le son passe par la chaîne de l'élément
//                (stems/StemChain.h, « Stem Mode » Off / Auto / Manuel), est
//                multiplié par le gain que le master lui donne, et part vers le
//                master avec sa position dans le projet. La reconnaissance
//                écoute le son sec, avant la chaîne.
//     Le rôle est pris à l'activation : changer de rôle change la latence,
//     le contrôleur demande à l'hôte de réactiver l'instance.
// =============================================================================
#pragma once

#include "engine/Engine.h"
#include "engine/ReferenceCapture.h"
#include "core/OnePoleSmoother.h"
#include "link/LinkClient.h"
#include "params/Parameters.h"
#include "stems/StemChain.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
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
    /// Messages du contrôleur : nom de la piste dans l'hôte (kTrackNameMessage).
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

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

    void onCaptureFinished(const ReferenceCapture::Result& result); ///< thread d'analyse

    [[nodiscard]] double plainOf(params::ParamId id) const noexcept;
    template <typename Sample>
    void processStem(Sample** inputs, Sample** outputs, Steinberg::int32 numSamples, const Steinberg::Vst::ProcessContext* context);

    Engine engine_;
    ReferenceCapture capture_;
    link::LinkClient link_;
    /// Rôle en vigueur : pris à l'activation, ou changé en route (switchRole,
    /// thread de l'interface). Pendant le changement, le thread audio ne touche
    /// pas au lien (roleSwitching_ / audioInLink_, ordre séquentiel).
    std::atomic<int> activeRole_{static_cast<int>(params::LinkRole::Alone)};
    std::atomic<bool> roleSwitching_{false}, audioInLink_{false}, active_{false};
    void switchRole(int role);
    [[nodiscard]] params::LinkRole role() const noexcept { return static_cast<params::LinkRole>(activeRole_.load()); }
    OnePoleSmoother stemGain_;                             ///< gain ordonné par le master
    stems::StemChain stemChain_;
    std::array<std::vector<double>, 2> stemBuffer_;        ///< bloc du stem en double
    double sampleRate_ = 48000.0;
    std::mutex captureMutex_;
    std::string capturedText_; ///< signatures de la dernière écoute (état de la session)

    // Valeurs normalisées courantes (lues par getState depuis le thread UI,
    // écrites par process() depuis le thread audio) -> atomiques.
    // Un emplacement par paramètre de la table. (Une capacité fixe de 64 avait
    // été dépassée par le Match EQ : les paramètres suivants n'étaient ni
    // sauvegardés ni rechargés.)
    static constexpr std::size_t kMaxParams = params::kParameterCount;
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
    std::array<double, 11> lastMeterValues_{};
};

} // namespace omnis::vst3
