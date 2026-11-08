// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestPluginSlotToggle.cpp — Le bouton rond d'un slot du mixer de FL
//  Studio : l'hôte désactive / réactive l'instance pendant que son thread
//  audio continue d'appeler process(). Rien ne doit planter, ni sortir autre
//  chose que du son fini, même avec deux instances en même temps, des messages
//  de l'interface et un nouveau setupProcessing au milieu.
//  (À passer aussi sous ASan / TSan : -DOMNIS_SANITIZE=address|thread.)
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "params/Parameters.h"
#include "plugin/vst3/OmnisIds.h"
#include "plugin/vst3/OmnisProcessor.h"
#include "plugin/vst3/OmnisState.h"

#include "base/source/fobject.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <random>
#include <thread>

using namespace omnis;
using namespace Steinberg;
using namespace Steinberg::Vst;
using params::ParamId;

namespace {

IPtr<vst3::OmnisProcessor> makeProcessor(const std::map<ParamId, double>& plain) {
    IPtr<vst3::OmnisProcessor> p = owned(new vst3::OmnisProcessor());
    IPtr<MemoryStream> state = owned(new MemoryStream());
    vst3::writeState(state, [&](std::uint32_t id) {
        const auto* spec = params::findParameter(id);
        const auto it = plain.find(static_cast<ParamId>(id));
        return params::toNormalized(*spec, it != plain.end() ? it->second : spec->defaultValue);
    });
    state->seek(0, IBStream::kIBSeekSet, nullptr);
    p->initialize(nullptr);
    p->setState(state);
    SpeakerArrangement ins[2] = {SpeakerArr::kStereo, SpeakerArr::kStereo}, outs[1] = {SpeakerArr::kStereo};
    p->setBusArrangements(ins, 2, outs, 1);
    ProcessSetup setup{kRealtime, kSample32, 512, 48000.0};
    p->setupProcessing(setup);
    p->setActive(true);
    p->setProcessing(true);
    return p;
}

/// Le thread audio de l'hôte : appelle process() sans relâche, comme FL qui
/// ne s'arrête pas pendant qu'on clique sur le slot.
struct AudioThread {
    vst3::OmnisProcessor& p;
    std::atomic<bool> quit{false};
    std::atomic<long> blocks{0}, nonFinite{0};
    std::thread thread;

    explicit AudioThread(vst3::OmnisProcessor& proc) : p(proc) {
        thread = std::thread([this] { run(); });
    }
    ~AudioThread() {
        quit.store(true);
        thread.join();
    }
    void run() {
        const auto noise = tools::signals::pinkNoise(48000.0, 1.0, -12.0, 9, true);
        HostProcessData data;
        data.prepare(p, 512, kSample32);
        ParameterChanges changes{8}, outChanges{32};
        ProcessContext context{};
        context.sampleRate = 48000.0;
        context.state = ProcessContext::kPlaying;
        data.processContext = &context;
        int pos = 0;
        while (!quit.load()) {
            changes.clearQueue();
            outChanges.clearQueue();
            // De temps en temps une automation (le gain d'entrée) : elle ne doit pas se perdre ni planter.
            if (blocks.load() % 7 == 0) {
                int32 index = 0;
                if (auto* q = changes.addParameterData(static_cast<ParamID>(ParamId::InputGain), index)) {
                    int32 point = 0;
                    q->addPoint(0, 0.5, point);
                }
            }
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    data.inputs[0].channelBuffers32[c][i] =
                        static_cast<float>(noise.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>((pos + i) % 48000)]);
            context.projectTimeSamples = pos;
            data.numSamples = 512;
            data.inputParameterChanges = &changes;
            data.outputParameterChanges = &outChanges;
            p.process(data);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    if (!std::isfinite(data.outputs[0].channelBuffers32[c][i])) {
                        nonFinite.fetch_add(1);
                        break;
                    }
            pos += 512;
            blocks.fetch_add(1);
            // ~10x le temps réel : le thread du lien suit, les clics tombent en plein traitement.
            if (pos % 4096 == 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
};

/// L'interface demande son flux (thread de l'interface, pendant l'audio).
void sendFeedRequest(vst3::OmnisProcessor& p) {
    IPtr<HostMessage> m = owned(new HostMessage());
    m->setMessageID(vst3::kFeedRequest);
    p.notify(m);
}

/// Les clics de la personne sur le slot (et ce que FL fait autour).
void hammer(vst3::OmnisProcessor& p, AudioThread& audio, int toggles, unsigned seed, bool reprepare) {
    std::mt19937 rng(seed);
    for (int t = 0; t < toggles; ++t) {
        p.setProcessing(false);
        p.setActive(false);
        std::this_thread::sleep_for(std::chrono::microseconds(rng() % 3000));
        if (t % 5 == 0) {
            // Réglage du traitement refait à la réactivation (taille de bloc qui change).
            ProcessSetup setup{kRealtime, kSample32, t % 10 == 0 ? 256 : 512, 48000.0};
            p.setupProcessing(setup);
        }
        p.setActive(true);
        p.setProcessing(true);
        if (reprepare && t % 4 == 0) sendFeedRequest(p);
        const long before = audio.blocks.load();
        // Laisser passer au moins un bloc entre deux clics.
        for (int i = 0; i < 200 && audio.blocks.load() == before; ++i) std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

} // namespace

OMNIS_TEST("slot FL: activer / désactiver pendant process() (Seul) ne plante pas") {
    auto p = makeProcessor({});
    {
        AudioThread audio(*p);
        hammer(*p, audio, 60, 1, false);
        OMNIS_CHECK(audio.nonFinite.load() == 0);
        OMNIS_CHECK(audio.blocks.load() > 60);
    }
    p->setProcessing(false);
    p->setActive(false);
    p->terminate();
}

OMNIS_TEST("slot FL: deux instances activées / désactivées en même temps pendant process() ne plantent pas") {
    auto first = makeProcessor({});
    auto second = makeProcessor({{ParamId::LowLatency, 1.0}});
    {
        AudioThread firstAudio(*first), secondAudio(*second);
        std::thread clicksOnSecond([&] { hammer(*second, secondAudio, 60, 2, false); });
        hammer(*first, firstAudio, 60, 3, false);
        clicksOnSecond.join();
        OMNIS_CHECK(firstAudio.nonFinite.load() == 0);
        OMNIS_CHECK(secondAudio.nonFinite.load() == 0);
    }
    for (auto* p : {first.get(), second.get()}) {
        p->setProcessing(false);
        p->setActive(false);
        p->terminate();
    }
}

OMNIS_TEST("slot FL: clics sur le slot mêlés à des messages de l'interface en route ne plantent pas") {
    auto p = makeProcessor({});
    {
        AudioThread audio(*p);
        // Les messages de l'interface arrivent de son thread pendant que l'hôte clique.
        std::atomic<bool> stop{false};
        std::thread ui([&] {
            while (!stop.load()) {
                sendFeedRequest(*p);
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
        });
        hammer(*p, audio, 50, 4, true);
        stop.store(true);
        ui.join();
        OMNIS_CHECK(audio.nonFinite.load() == 0);
    }
    p->setProcessing(false);
    p->setActive(false);
    p->terminate();
}
