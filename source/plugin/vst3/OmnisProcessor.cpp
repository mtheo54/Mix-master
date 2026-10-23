// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisProcessor.cpp
// =============================================================================
#include "OmnisProcessor.h"

#include "OmnisIds.h"
#include "OmnisState.h"

#include "ai/ReferenceLibrary.h"
#include "core/UserDataDir.h"
#include "engine/MasteringChain.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>
#include <string_view>

namespace omnis::vst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;
using params::ParamId;
using params::ParamKind;

namespace {

constexpr std::array<ParamId, 10> kMeterIds{ParamId::MeterMomentary, ParamId::MeterShortTerm,
                                            ParamId::MeterIntegrated, ParamId::MeterTruePeak,
                                            ParamId::MeterCorrelation, ParamId::MeterLoudnessRange,
                                            ParamId::MeterRefSeconds, ParamId::MeterRefLufs,
                                            ParamId::MeterLinkStems, ParamId::MeterStemElement};

/// Gain qu'un master peut donner à un stem : borné, quoi qu'il écrive.
constexpr double kMinStemGainDb = -24.0, kMaxStemGainDb = 12.0;


std::string nowLabel() {
    const std::time_t t = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
    return buf;
}

double meterPlainValue(ParamId id, const MeterReadout& m) {
    switch (id) {
        case ParamId::MeterMomentary: return m.momentaryLufs;
        case ParamId::MeterShortTerm: return m.shortTermLufs;
        case ParamId::MeterIntegrated: return m.integratedLufs;
        case ParamId::MeterTruePeak: return m.truePeakDb;
        case ParamId::MeterCorrelation: return m.correlation;
        case ParamId::MeterLoudnessRange: return m.loudnessRangeLu;
        default: return 0.0;
    }
}

} // namespace

OmnisProcessor::OmnisProcessor() {
    setControllerClass(FUID::fromTUID(kControllerUID));
    buildMasteringChain(engine_);
    capture_.setCompletionHandler([this](const ReferenceCapture::Result& r) { onCaptureFinished(r); });
    static_assert(kMeterIds.size() <= std::tuple_size_v<decltype(lastMeterValues_)>);
    const auto all = params::allParameters();
    for (std::size_t i = 0; i < all.size() && i < kMaxParams; ++i)
        normalized_[i].store(params::toNormalized(all[i], all[i].defaultValue), std::memory_order_relaxed);
    lastMeterValues_.fill(-1.0);
}

OmnisProcessor::~OmnisProcessor() { capture_.release(); }

void OmnisProcessor::onCaptureFinished(const ReferenceCapture::Result& r) {
    // La durée minimale (60 s) est tenue par ReferenceCapture.
    if (r.reference.activeFrames == 0) return;
    ai::ReferenceEntry ref;
    ref.artist = "Référence écoutée";
    ref.title = nowLabel();
    ref.signature = r.reference;
    // Dans la base de la personne : réutilisable dans toutes ses sessions.
    ai::addToLibraryFile((userDataDir() / "references_utilisateur.txt").string(), ref);
    // Dans la session : la référence ET la prod au moment de l'écoute.
    ai::ReferenceLibrary session;
    session.add(ref);
    ai::ReferenceEntry prod;
    prod.artist = "Prod";
    prod.title = ref.title;
    prod.signature = r.prod;
    prod.measured = r.prod.activeFrames > 0;
    session.add(prod);
    std::lock_guard<std::mutex> lock(captureMutex_);
    capturedText_ = session.serialize();
}

tresult PLUGIN_API OmnisProcessor::initialize(FUnknown* context) {
    const tresult result = AudioEffect::initialize(context);
    if (result != kResultOk) return result;
    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    // Sidechain : la référence que la personne joue dans son projet.
    addAudioInput(STR16("Reference"), SpeakerArr::kStereo, BusTypes::kAux, 0);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    return kResultOk;
}

tresult PLUGIN_API OmnisProcessor::terminate() {
    link_.stop();
    capture_.release();
    return AudioEffect::terminate();
}

tresult PLUGIN_API OmnisProcessor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                      SpeakerArrangement* outputs, int32 numOuts) {
    // Mastering stéréo : entrée stéréo -> sortie stéréo, plus la référence
    // (stéréo ou mono) sur l'entrée auxiliaire.
    if (numOuts != 1 || outputs[0] != SpeakerArr::kStereo || numIns < 1 || numIns > 2 || inputs[0] != SpeakerArr::kStereo)
        return kResultFalse;
    if (numIns == 2 && inputs[1] != SpeakerArr::kStereo && inputs[1] != SpeakerArr::kMono) return kResultFalse;
    return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

tresult PLUGIN_API OmnisProcessor::canProcessSampleSize(int32 symbolicSampleSize) {
    return (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API OmnisProcessor::setupProcessing(ProcessSetup& setup) {
    const tresult result = AudioEffect::setupProcessing(setup);
    if (result != kResultOk) return result;

    // Toutes les allocations ont lieu ici (thread non temps réel, composant
    // inactif). La latence est connue dès la sortie de cette fonction.
    // Bounce hors ligne (kOffline) : calcul déterministe et synchrone des noyaux.
    engine_.setRealtimeMode(setup.processMode != kOffline);
    engine_.prepare(setup.sampleRate, std::max<int32>(1, setup.maxSamplesPerBlock));
    const auto all = params::allParameters();
    for (std::size_t i = 0; i < all.size() && i < kMaxParams; ++i) {
        if (!params::isPersistent(all[i])) continue;
        engine_.setParameterImmediate(all[i].id,
                                      params::toPlain(all[i], normalized_[i].load(std::memory_order_relaxed)));
    }
    pendingState_.store(false, std::memory_order_relaxed);
    sampleRate_ = setup.sampleRate;
    meterIntervalSamples_ = std::max(1, static_cast<int>(setup.sampleRate / 30.0)); // ~30 Hz
    meterCountdown_ = 0;
    return kResultOk;
}

tresult PLUGIN_API OmnisProcessor::setActive(TBool state) {
    if (state) {
        engine_.reset();
        transportWasPlaying_ = false;
        lastMeterValues_.fill(-1.0);
        // Hors thread audio : files et thread d'analyse de l'écoute.
        capture_.prepare(sampleRate_);
        const int index = params::parameterIndex(ParamId::RefCapture);
        capture_.setCapturing(index >= 0 && normalized_[static_cast<std::size_t>(index)].load(std::memory_order_relaxed) >= 0.5);
        // Le rôle est pris ici : il fixe la latence annoncée à l'hôte.
        activeRole_ = static_cast<params::LinkRole>(static_cast<int>(plainOf(ParamId::LinkRole)));
        stemGain_.prepare(sampleRate_, 30.0);
        stemGain_.snapTo(1.0);
        if (activeRole_ != params::LinkRole::Alone)
            link_.start(static_cast<link::Role>(activeRole_), static_cast<link::StemKind>(static_cast<int>(plainOf(ParamId::LinkStemKind))),
                        static_cast<int>(plainOf(ParamId::LinkGroup)), sampleRate_);
        else
            link_.stop();
    } else {
        link_.stop();
        capture_.release();
    }
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API OmnisProcessor::notify(IMessage* message) {
    if (message && message->getMessageID() && std::string_view(message->getMessageID()) == kTrackNameMessage) {
        const void* data = nullptr;
        uint32 size = 0;
        if (message->getAttributes() && message->getAttributes()->getBinary(kTrackNameAttribute, data, size) == kResultTrue && data)
            link_.setTrackName(std::string(static_cast<const char*>(data), size));
        return kResultOk;
    }
    return AudioEffect::notify(message);
}

uint32 PLUGIN_API OmnisProcessor::getLatencySamples() {
    // Un stem ne retarde pas sa piste : l'hôte peut le demander avant
    // l'activation, on lit donc le réglage plutôt que le rôle actif.
    if (static_cast<params::LinkRole>(static_cast<int>(plainOf(ParamId::LinkRole))) == params::LinkRole::Stem) return 0;
    return static_cast<uint32>(engine_.latencySamples());
}

double OmnisProcessor::plainOf(ParamId id) const noexcept {
    const auto* spec = params::findParameter(id);
    const int index = params::parameterIndex(id);
    if (!spec || index < 0 || index >= static_cast<int>(kMaxParams)) return 0.0;
    return params::toPlain(*spec, normalized_[static_cast<std::size_t>(index)].load(std::memory_order_relaxed));
}

// -----------------------------------------------------------------------------
//  Paramètres
// -----------------------------------------------------------------------------
void OmnisProcessor::applyParameter(ParamID id, ParamValue normalized, bool immediate) {
    const auto* spec = params::findParameter(id);
    if (!spec || spec->kind == ParamKind::Meter) return;
    const int index = params::parameterIndex(spec->id);
    if (index >= 0 && index < static_cast<int>(kMaxParams))
        normalized_[static_cast<std::size_t>(index)].store(normalized, std::memory_order_relaxed);
    const double plain = params::toPlain(*spec, normalized);
    if (spec->id == ParamId::RefCapture) capture_.setCapturing(plain >= 0.5);
    if (spec->id == ParamId::LinkStemKind) link_.setKind(static_cast<link::StemKind>(static_cast<int>(plain)));
    if (spec->id == ParamId::LinkGroup) link_.setGroup(static_cast<int>(plain));
    if (immediate)
        engine_.setParameterImmediate(spec->id, plain);
    else
        engine_.setParameter(spec->id, plain);
}

void OmnisProcessor::applyPendingState() {
    if (!pendingState_.exchange(false, std::memory_order_acquire)) return;
    // Changement de preset pendant la lecture : on glisse vers les nouvelles
    // valeurs avec le lissage normal (aucun clic).
    const auto all = params::allParameters();
    for (std::size_t i = 0; i < all.size() && i < kMaxParams; ++i) {
        if (!params::isPersistent(all[i])) continue;
        engine_.setParameter(all[i].id, params::toPlain(all[i], normalized_[i].load(std::memory_order_relaxed)));
    }
}

void OmnisProcessor::collectParameterEvents(IParameterChanges* changes, int32 numSamples) {
    numEvents_ = 0;
    if (!changes) return;
    const int32 numQueues = changes->getParameterCount();

    int32 totalPoints = 0;
    for (int32 q = 0; q < numQueues; ++q)
        if (IParamValueQueue* queue = changes->getParameterData(q)) totalPoints += queue->getPointCount();
    // En cas de rafale d'automation dépassant la capacité, on ne garde que le
    // dernier point de chaque paramètre (au plus un par file : tient toujours).
    const bool keepAll = totalPoints <= kMaxEvents;

    const int32 lastOffset = std::max<int32>(0, numSamples - 1);
    int32 sequence = 0;
    for (int32 q = 0; q < numQueues; ++q) {
        IParamValueQueue* queue = changes->getParameterData(q);
        if (!queue) continue;
        const ParamID id = queue->getParameterId();
        const int32 numPoints = queue->getPointCount();
        for (int32 p = keepAll ? 0 : numPoints - 1; p < numPoints; ++p) {
            int32 offset = 0;
            ParamValue value = 0.0;
            if (queue->getPoint(p, offset, value) != kResultTrue) continue;
            if (numEvents_ >= kMaxEvents) break;
            events_[static_cast<std::size_t>(numEvents_++)] = {std::clamp(offset, 0, lastOffset), sequence++, id, value};
        }
    }
    // std::sort n'alloue pas (introsort en place).
    std::sort(events_.begin(), events_.begin() + numEvents_, [](const ParamEvent& a, const ParamEvent& b) {
        return a.sampleOffset != b.sampleOffset ? a.sampleOffset < b.sampleOffset : a.sequence < b.sequence;
    });
}

void OmnisProcessor::handleTransport(const ProcessContext* context) {
    if (!context) return;
    const bool playing = (context->state & ProcessContext::kPlaying) != 0;
    // Chaque nouvelle lecture démarre une nouvelle mesure intégrée : c'est le
    // comportement attendu en mastering (on mesure un passage complet).
    if (playing && !transportWasPlaying_) engine_.resetLoudnessHistory();
    transportWasPlaying_ = playing;
}

// -----------------------------------------------------------------------------
//  Audio
// -----------------------------------------------------------------------------
template <typename Sample>
void OmnisProcessor::processSegments(Sample** inputs, Sample** outputs, int32 numSamples) {
    int ev = 0;
    int32 position = 0;
    while (position < numSamples) {
        while (ev < numEvents_ && events_[static_cast<std::size_t>(ev)].sampleOffset <= position) {
            const auto& e = events_[static_cast<std::size_t>(ev++)];
            applyParameter(e.id, e.normalized, false);
        }
        const int32 end = ev < numEvents_ ? events_[static_cast<std::size_t>(ev)].sampleOffset : numSamples;
        const Sample* in[kNumChannels] = {inputs[0] + position, inputs[1] + position};
        Sample* out[kNumChannels] = {outputs[0] + position, outputs[1] + position};
        engine_.process(in, out, end - position);
        position = end;
    }
    // Points restants (aucun en pratique : offsets bornés à numSamples - 1).
    while (ev < numEvents_) {
        const auto& e = events_[static_cast<std::size_t>(ev++)];
        applyParameter(e.id, e.normalized, false);
    }
}

void OmnisProcessor::publishMeters(IParameterChanges* outputChanges, int32 numSamples) {
    if (!outputChanges) return;
    meterCountdown_ -= numSamples;
    if (meterCountdown_ > 0) return;
    meterCountdown_ += meterIntervalSamples_;
    if (meterCountdown_ <= 0) meterCountdown_ = meterIntervalSamples_;

    const MeterReadout readout = engine_.meters();
    for (std::size_t i = 0; i < kMeterIds.size(); ++i) {
        const auto* spec = params::findParameter(kMeterIds[i]);
        if (!spec) continue;
        double plain = kMeterIds[i] == ParamId::MeterRefSeconds ? capture_.referenceSeconds()
                       : kMeterIds[i] == ParamId::MeterRefLufs  ? capture_.referenceLufs()
                       : kMeterIds[i] == ParamId::MeterLinkStems ? static_cast<double>(link_.linkedStems())
                       : kMeterIds[i] == ParamId::MeterStemElement ? static_cast<double>(static_cast<int>(link_.identifiedElement()))
                                                                : meterPlainValue(kMeterIds[i], readout);
        if (!std::isfinite(plain)) plain = spec->minValue; // -inf LUFS -> bas d'échelle
        const double normalized = params::toNormalized(*spec, plain);
        if (std::fabs(normalized - lastMeterValues_[i]) < 1.0e-5) continue;
        int32 queueIndex = 0;
        if (IParamValueQueue* queue = outputChanges->addParameterData(static_cast<ParamID>(spec->id), queueIndex)) {
            int32 pointIndex = 0;
            queue->addPoint(0, normalized, pointIndex);
            lastMeterValues_[i] = normalized;
        }
    }
}

template <typename Sample>
void OmnisProcessor::processStem(Sample** inputs, Sample** outputs, int32 numSamples, const ProcessContext* context) {
    // Automation : appliquée en début de bloc (rien à lisser ici à l'échantillon près).
    for (int i = 0; i < numEvents_; ++i)
        applyParameter(events_[static_cast<std::size_t>(i)].id, events_[static_cast<std::size_t>(i)].normalized, false);
    numEvents_ = 0;

    const bool playing = context && (context->state & ProcessContext::kPlaying) != 0;
    const std::int64_t position = playing ? static_cast<std::int64_t>(context->projectTimeSamples) : link::kNoPosition;
    link::LinkClient::RtAccess access(link_);
    link::Endpoint* endpoint = access.endpoint();
    double gainDb = endpoint ? std::clamp(static_cast<double>(endpoint->readOrders().gainDb), kMinStemGainDb, kMaxStemGainDb) : 0.0;
    if (plainOf(ParamId::Bypass) >= 0.5) gainDb = 0.0;
    stemGain_.setTarget(std::pow(10.0, gainDb / 20.0));
    for (int32 i = 0; i < numSamples; ++i) {
        const double g = stemGain_.next();
        outputs[0][i] = static_cast<Sample>(g * inputs[0][i]);
        outputs[1][i] = static_cast<Sample>(g * inputs[1][i]);
    }
    // Ce qui part vers le mix, et donc ce que le master doit entendre.
    if (endpoint) endpoint->writeAudio(outputs[0], outputs[1], numSamples, position, playing);
}

tresult PLUGIN_API OmnisProcessor::process(ProcessData& data) {
    applyPendingState();
    handleTransport(data.processContext);
    collectParameterEvents(data.inputParameterChanges, data.numSamples);

    const bool hasAudio = data.numSamples > 0 && data.numInputs >= 1 && data.numOutputs >= 1 &&
                          data.inputs[0].numChannels >= kNumChannels && data.outputs[0].numChannels >= kNumChannels;
    if (!hasAudio) {
        // Appel de « flush » des paramètres (numSamples == 0) : on applique tout.
        for (int i = 0; i < numEvents_; ++i)
            applyParameter(events_[static_cast<std::size_t>(i)].id, events_[static_cast<std::size_t>(i)].normalized,
                           false);
        numEvents_ = 0;
        return kResultOk;
    }

    // Écoute de la référence : copie vers le thread d'analyse AVANT le
    // traitement (l'hôte peut traiter en place). La prod est mesurée en même temps.
    if (capture_.capturing()) {
        const bool aux = data.numInputs > 1 && data.inputs[1].numChannels > 0;
        const int auxChannels = aux ? data.inputs[1].numChannels : 0;
        if (data.symbolicSampleSize == kSample64) {
            const double* refL = aux ? data.inputs[1].channelBuffers64[0] : nullptr;
            const double* refR = auxChannels > 1 ? data.inputs[1].channelBuffers64[1] : refL;
            capture_.push(refL, refR, data.inputs[0].channelBuffers64[0], data.inputs[0].channelBuffers64[1], data.numSamples);
        } else {
            const float* refL = aux ? data.inputs[1].channelBuffers32[0] : nullptr;
            const float* refR = auxChannels > 1 ? data.inputs[1].channelBuffers32[1] : refL;
            capture_.push(refL, refR, data.inputs[0].channelBuffers32[0], data.inputs[0].channelBuffers32[1], data.numSamples);
        }
    }

    if (activeRole_ == params::LinkRole::Stem) {
        if (data.symbolicSampleSize == kSample64)
            processStem(data.inputs[0].channelBuffers64, data.outputs[0].channelBuffers64, data.numSamples, data.processContext);
        else
            processStem(data.inputs[0].channelBuffers32, data.outputs[0].channelBuffers32, data.numSamples, data.processContext);
        data.outputs[0].silenceFlags = data.inputs[0].silenceFlags;
        publishMeters(data.outputParameterChanges, data.numSamples);
        return kResultOk;
    }

    if (data.symbolicSampleSize == kSample64)
        processSegments(data.inputs[0].channelBuffers64, data.outputs[0].channelBuffers64, data.numSamples);
    else
        processSegments(data.inputs[0].channelBuffers32, data.outputs[0].channelBuffers32, data.numSamples);

    // Les étages ont des états (latence, enveloppes) : la sortie n'est jamais
    // déclarée silencieuse même si l'entrée l'est.
    data.outputs[0].silenceFlags = 0;

    publishMeters(data.outputParameterChanges, data.numSamples);
    return kResultOk;
}

// -----------------------------------------------------------------------------
//  État
// -----------------------------------------------------------------------------
tresult PLUGIN_API OmnisProcessor::setState(IBStream* state) {
    const auto all = params::allParameters();
    // Tout paramètre absent du flux revient à sa valeur par défaut.
    std::array<double, kMaxParams> values{};
    for (std::size_t i = 0; i < all.size() && i < kMaxParams; ++i)
        values[i] = params::toNormalized(all[i], all[i].defaultValue);

    const bool ok = readState(state, [&](std::uint32_t id, double value) {
        const int index = params::parameterIndex(static_cast<ParamId>(id));
        if (index >= 0 && index < static_cast<int>(kMaxParams)) values[static_cast<std::size_t>(index)] = value;
    });
    if (!ok) return kResultFalse;
    std::string extras;
    if (readExtras(state, extras)) {
        std::lock_guard<std::mutex> lock(captureMutex_);
        capturedText_ = std::move(extras);
    }

    for (std::size_t i = 0; i < all.size() && i < kMaxParams; ++i)
        normalized_[i].store(values[i], std::memory_order_relaxed);
    pendingState_.store(true, std::memory_order_release);
    return kResultOk;
}

tresult PLUGIN_API OmnisProcessor::getState(IBStream* state) {
    const bool ok = writeState(state, [&](std::uint32_t id) {
        const int index = params::parameterIndex(static_cast<ParamId>(id));
        return index >= 0 && index < static_cast<int>(kMaxParams)
                   ? normalized_[static_cast<std::size_t>(index)].load(std::memory_order_relaxed)
                   : 0.0;
    });
    if (!ok) return kResultFalse;
    std::lock_guard<std::mutex> lock(captureMutex_);
    return writeExtras(state, capturedText_) ? kResultOk : kResultFalse;
}

} // namespace omnis::vst3
