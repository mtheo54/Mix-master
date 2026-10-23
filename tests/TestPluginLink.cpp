// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestPluginLink.cpp — Deux instances du plug-in liées : un master et
//  un stem, comme sur le bus master et une piste de FL Studio.
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "link/LinkBus.h"
#include "params/Parameters.h"
#include "plugin/vst3/OmnisProcessor.h"
#include "plugin/vst3/OmnisState.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <thread>

using namespace omnis;
using namespace Steinberg;
using namespace Steinberg::Vst;
using params::ParamId;

namespace {

void setLinkNamespace(const char* name) {
#ifdef _WIN32
    _putenv_s("OMNIS_LINK_NAMESPACE", name);
#else
    setenv("OMNIS_LINK_NAMESPACE", name, 1);
#endif
    for (int g = 1; g <= link::kGroups; ++g) link::SharedRegion::unlink(link::regionName(g));
}

/// Une instance réglée comme par une session rechargée, puis activée.
struct Instance {
    IPtr<vst3::OmnisProcessor> p = owned(new vst3::OmnisProcessor());
    HostProcessData data;
    ParameterChanges changes{8}, outChanges{32};
    ProcessContext context{};
    double lastStems = -1.0;

    Instance(std::map<ParamId, double> plain) {
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
        data.prepare(*p, 512, kSample32);
        context.sampleRate = 48000.0;
        context.state = ProcessContext::kPlaying;
        data.processContext = &context;
    }
    ~Instance() {
        p->setProcessing(false);
        p->setActive(false);
        p->terminate();
    }
    /// Un bloc de 512 à la position `pos` du signal (et du projet).
    void block(const tools::AudioFile& s, int pos) {
        changes.clearQueue();
        outChanges.clearQueue();
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                data.inputs[0].channelBuffers32[c][i] = static_cast<float>(s.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(pos + i)]);
        context.projectTimeSamples = pos;
        data.numSamples = 512;
        data.inputParameterChanges = &changes;
        data.outputParameterChanges = &outChanges;
        p->process(data);
        for (int32 q = 0; q < outChanges.getParameterCount(); ++q) {
            auto* queue = outChanges.getParameterData(q);
            if (!queue || queue->getParameterId() != static_cast<ParamID>(ParamId::MeterLinkStems)) continue;
            int32 offset = 0;
            ParamValue v = 0.0;
            if (queue->getPoint(queue->getPointCount() - 1, offset, v) == kResultTrue)
                lastStems = params::toPlain(*params::findParameter(ParamId::MeterLinkStems), v);
        }
    }
    [[nodiscard]] double outputRms() const {
        double sum = 0.0;
        for (int i = 0; i < 512; ++i) sum += static_cast<double>(data.outputs[0].channelBuffers32[0][i]) * data.outputs[0].channelBuffers32[0][i];
        return std::sqrt(sum / 512.0);
    }
};

double db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

} // namespace

OMNIS_TEST("plug-in lié: le master voit le stem, le stem n'a aucune latence et applique le gain du master") {
    setLinkNamespace("omnistestp");
    Instance master({{ParamId::LinkRole, 2.0}, {ParamId::LinkGroup, 5.0}});
    Instance stem({{ParamId::LinkRole, 1.0}, {ParamId::LinkStemKind, 2.0}, {ParamId::LinkGroup, 5.0}});
    OMNIS_CHECK(stem.p->getLatencySamples() == 0);
    OMNIS_CHECK(master.p->getLatencySamples() > 0);

    const auto noise = tools::signals::pinkNoise(48000.0, 4.0, -18.0, 21, true);
    int pos = 0;
    // ~20x le temps réel : le thread du lien (toutes les 50 ms) suit.
    for (; pos + 512 <= 2 * 48000; pos += 512) {
        stem.block(noise, pos);
        master.block(noise, pos);
        if ((pos / 512) % 2 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (int i = 0; i < 100 && master.lastStems != 1.0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        master.block(noise, 0);
    }
    OMNIS_CHECK(master.lastStems == 1.0);
    // Sans ordre, le stem laisse passer le son tel quel (sans retard).
    stem.block(noise, pos);
    float maxDiff = 0.0f;
    for (int i = 0; i < 512; ++i)
        maxDiff = std::max(maxDiff, std::fabs(stem.data.outputs[0].channelBuffers32[0][i] - static_cast<float>(noise.channels[0][static_cast<std::size_t>(pos + i)])));
    OMNIS_CHECK(maxDiff < 1e-6f);

    // Ordre du master (ici écrit directement, comme le fera le cerveau du mix) : -6 dB.
    link::Endpoint brain;
    OMNIS_CHECK(brain.join(5, link::Role::Master, link::StemKind::Other, "test", 48000.0));
    const auto stems = brain.stems();
    OMNIS_CHECK(stems.size() == 1 && stems[0].kind == link::StemKind::Kick);
    OMNIS_CHECK(brain.writeOrders(stems[0].slot, stems[0].owner, {-6.0f, 0}));
    for (int b = 0; b < 20; ++b) stem.block(noise, pos); // 200 ms : lissage de 30 ms terminé
    double inSum = 0.0;
    for (int i = 0; i < 512; ++i) inSum += noise.channels[0][static_cast<std::size_t>(pos + i)] * noise.channels[0][static_cast<std::size_t>(pos + i)];
    OMNIS_CHECK_NEAR(db(stem.outputRms() / std::sqrt(inSum / 512.0)), -6.0, 0.05);
}
