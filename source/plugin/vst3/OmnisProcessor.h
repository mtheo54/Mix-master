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
//   - Porte entre les threads (AudioGate) : certains hôtes (FL Studio, bouton
//     du slot du mixer) activent / désactivent l'instance pendant que leur
//     thread audio est encore dans process(). Avant de réallouer quoi que ce
//     soit (setupProcessing, setActive), on ferme la porte
//     et on attend que process() en soit sorti ; porte fermée ou instance
//     inactive, process() laisse passer le son tel quel sans toucher à rien.
//   - Rôle (« role ») : Master (le mastering) ; Bus ou Piste : le son passe
//     tel quel, sans latence ; l'instance publie le nom de sa piste, ce
//     qu'elle reconnaît et l'empreinte de ce qu'elle entend (link/LinkClient),
//     lus par la page COMPOSITION des autres instances.
// =============================================================================
#pragma once

#include "engine/Engine.h"
#include "engine/ReferenceCapture.h"
#include "link/LinkClient.h"
#include "params/Parameters.h"
#include "UiFeed.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace omnis {
class LimiterStage;
class ClipperStage;
class CompressorStage;
class MultibandStage;
class MatchEqStage;
} // namespace omnis

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
    /// Prépare le moteur (mode de latence, réglages courants) ; hors thread audio.
    void prepareEngine(Steinberg::Vst::ProcessSetup& setup);
    bool wantsLowLatency(const Steinberg::Vst::ProcessSetup& setup) const;
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
    /// Hors thread audio : change de rôle tout de suite (porte fermée le temps du changement).
    void switchRole(int role);
    void startLink(int role);
    [[nodiscard]] params::InstanceRole role() const noexcept { return static_cast<params::InstanceRole>(activeRole_.load(std::memory_order_relaxed)); }
    template <typename Sample>
    void processListening(Steinberg::Vst::ProcessData& data, Sample** inputs);

    link::LinkClient link_;
    std::atomic<int> activeRole_{0};

    Engine engine_;
    ReferenceCapture capture_;
    std::atomic<bool> active_{false};

    /// Porte du thread audio (voir l'en-tête). Ordre séquentiel des deux côtés :
    /// process() annonce son entrée puis lit la porte ; le reste ferme la porte
    /// puis attend la sortie de process() : l'un des deux voit toujours l'autre.
    std::atomic<bool> gateClosed_{false}, inProcess_{false};
    std::mutex gateMutex_; ///< un seul thread non audio à la fois derrière la porte
    class GateHold {
    public:
        explicit GateHold(OmnisProcessor& p);
        ~GateHold();
        GateHold(const GateHold&) = delete;
        GateHold& operator=(const GateHold&) = delete;

    private:
        OmnisProcessor& p_;
        std::unique_lock<std::mutex> lock_;
    };
    bool prepared_ = false; ///< setupProcessing a eu lieu (allocations faites)
    static void passThrough(Steinberg::Vst::ProcessData& data);
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

    // Interface (UiFeed) : mesures des modules et échantillons de sortie.
    std::shared_ptr<UiFeed> feed_ = std::make_shared<UiFeed>();
    std::uint64_t feedToken_ = 0;
    std::array<float, 2> inPeak_{}, outPeak_{};
    omnis::LimiterStage* limiter_ = nullptr;
    omnis::ClipperStage* clipper_ = nullptr;
    omnis::CompressorStage* comp_ = nullptr;
    omnis::MultibandStage* multiband_ = nullptr;
    omnis::MatchEqStage* matchEq_ = nullptr;
    template <typename Sample>
    void feedBlock(Sample** inputs, Sample** outputs, Steinberg::int32 numSamples, bool beforeProcessing) noexcept;
    void publishFeed() noexcept;

    bool transportWasPlaying_ = false;
    int meterIntervalSamples_ = 1600;
    int meterCountdown_ = 0;
    std::array<double, 11> lastMeterValues_{};
};

} // namespace omnis::vst3
