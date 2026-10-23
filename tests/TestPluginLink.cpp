// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestPluginLink.cpp — Deux instances du plug-in liées : un master et
//  un stem, comme sur le bus master et une piste de FL Studio.
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "link/LinkBus.h"
#include "params/Parameters.h"
#include "stems/Element.h"
#include "plugin/vst3/OmnisIds.h"
#include "plugin/vst3/OmnisProcessor.h"
#include "plugin/vst3/OmnisState.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <chrono>
#include <cmath>
#include <cstdio>
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
    double lastStems = -1.0, lastElement = -1.0;

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
            if (!queue) continue;
            const auto id = static_cast<ParamId>(queue->getParameterId());
            if (id != ParamId::MeterLinkStems && id != ParamId::MeterStemElement) continue;
            int32 offset = 0;
            ParamValue v = 0.0;
            if (queue->getPoint(queue->getPointCount() - 1, offset, v) != kResultTrue) continue;
            // Les afficheurs ne sont publiés qu'au changement : on garde la dernière valeur.
            (id == ParamId::MeterLinkStems ? lastStems : lastElement) = params::toPlain(*params::findParameter(id), v);
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
    Instance stem({{ParamId::LinkRole, 1.0}, {ParamId::LinkStemKind, 1.0}, {ParamId::LinkGroup, 5.0}});
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

OMNIS_TEST("plug-in stem: le nom de piste de l'hôte et l'écoute donnent l'élément reconnu") {
    setLinkNamespace("omnistestel");
    // Rôle Stem, élément sur Auto : il doit se reconnaître seul.
    Instance stem({{ParamId::LinkRole, 1.0}, {ParamId::LinkStemKind, 0.0}, {ParamId::LinkGroup, 6.0}});
    // Le DAW donne le nom de la piste (un nom de pack).
    IPtr<HostMessage> message = owned(new HostMessage());
    message->setMessageID(vst3::kTrackNameMessage);
    const std::string name = "SH_Kick_Hard_04";
    message->getAttributes()->setBinary(vst3::kTrackNameAttribute, name.data(), static_cast<uint32>(name.size()));
    OMNIS_CHECK(stem.p->notify(message) == kResultOk);

    // Un kick toutes les 0,5 s pendant 4 s, au rythme du temps réel (le thread
    // du lien lit l'anneau toutes les 50 ms).
    tools::AudioFile kick;
    kick.sampleRate = 48000.0;
    kick.channels.assign(2, std::vector<double>(static_cast<std::size_t>(4.5 * 48000.0), 0.0));
    for (int hit = 0; hit < 9; ++hit)
        for (int n = 0; n < 20000; ++n) {
            const double t = n / 48000.0;
            const double v = 0.8 * std::sin(2.0 * 3.141592653589793 * (45.0 * t + 115.0 * 0.03 * (1.0 - std::exp(-t / 0.03)))) * std::exp(-t / 0.12);
            const auto i = static_cast<std::size_t>(hit * 24000 + n);
            if (i < kick.channels[0].size()) kick.channels[0][i] = kick.channels[1][i] = v;
        }
    for (int pos = 0; pos + 512 <= static_cast<int>(kick.channels[0].size()); pos += 512) {
        stem.block(kick, pos);
        if (pos % (512 * 8) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    // Une seconde de plus pour la reconnaissance, puis un bloc pour publier l'afficheur.
    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
    for (int k = 0; k < 200 && static_cast<int>(stem.lastElement) != static_cast<int>(stems::Element::Kick); ++k) {
        stem.block(kick, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const double element = stem.lastElement;
    std::printf("      élément reconnu : %s\n", params::formatValue(*params::findParameter(ParamId::MeterStemElement), element).c_str());
    OMNIS_CHECK(static_cast<int>(element) == static_cast<int>(stems::Element::Kick));
}
