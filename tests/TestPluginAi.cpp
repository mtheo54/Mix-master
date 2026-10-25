// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestPluginAi.cpp — La barre IA et AUTO dans le plug-in : le vrai
//  processeur écoute la prod, le vrai contrôleur comprend la demande, règle
//  les modules et mène le volume à la cible par la mesure.
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "params/Parameters.h"
#include "plugin/vst3/OmnisController.h"
#include "plugin/vst3/OmnisProcessor.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace omnis;
using namespace Steinberg;
using namespace Steinberg::Vst;
using params::ParamId;

namespace {

/// Un hôte minimal : processeur et contrôleur reliés, paramètres transmis dans les deux sens.
struct Host {
    HostApplication app;
    IPtr<vst3::OmnisProcessor> proc = owned(new vst3::OmnisProcessor());
    IPtr<vst3::OmnisController> ctrl = owned(new vst3::OmnisController());
    HostProcessData data;
    ParameterChanges in{256}, out{64};
    ProcessContext context{};
    tools::AudioFile signal;
    int pos = 0;
    std::map<ParamID, ParamValue> sent;

    Host() {
        proc->initialize(&app);
        ctrl->initialize(&app);
        proc->connect(ctrl);
        ctrl->connect(proc);
        SpeakerArrangement ins[2] = {SpeakerArr::kStereo, SpeakerArr::kStereo}, outs[1] = {SpeakerArr::kStereo};
        proc->setBusArrangements(ins, 2, outs, 1);
        ProcessSetup setup{kRealtime, kSample32, 512, 48000.0};
        proc->setupProcessing(setup);
        proc->setActive(true);
        proc->setProcessing(true);
        data.prepare(*proc, 512, kSample32);
        context.sampleRate = 48000.0;
        context.state = ProcessContext::kPlaying;
        data.processContext = &context;
        signal = tools::signals::drumLoop(48000.0, 8, 140.0, -10.0);
        const auto pink = tools::signals::pinkNoise(48000.0, signal.numFrames() / 48000.0, -30.0, 3);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < signal.numFrames(); ++i)
                signal.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] += pink.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)];
        ctrl->requestFeed();
    }
    ~Host() {
        proc->setProcessing(false);
        proc->setActive(false);
        ctrl->terminate();
        proc->terminate();
    }
    /// Ce que le contrôleur a changé part vers le processeur (comme l'hôte le fait).
    void sync() {
        for (const auto& spec : params::allParameters()) {
            if (!params::isPersistent(spec)) continue;
            const auto pid = static_cast<ParamID>(spec.id);
            const ParamValue v = ctrl->getParamNormalized(pid);
            auto it = sent.find(pid);
            if (it != sent.end() && it->second == v) continue;
            sent[pid] = v;
            int32 index = 0;
            if (auto* q = in.addParameterData(pid, index)) {
                int32 p = 0;
                q->addPoint(0, v, p);
            }
        }
    }
    void run(double seconds, bool poll) {
        const int blocks = static_cast<int>(seconds * 48000.0 / 512.0);
        for (int b = 0; b < blocks; ++b) {
            sync();
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    data.inputs[0].channelBuffers32[c][i] =
                        static_cast<float>(signal.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>((pos + i) % signal.numFrames())]);
            context.projectTimeSamples = pos;
            data.numSamples = 512;
            data.inputParameterChanges = &in;
            data.outputParameterChanges = &out;
            proc->process(data);
            in.clearQueue();
            for (int32 q = 0; q < out.getParameterCount(); ++q) {
                auto* queue = out.getParameterData(q);
                int32 offset = 0;
                ParamValue v = 0.0;
                if (queue && queue->getPoint(queue->getPointCount() - 1, offset, v) == kResultTrue) ctrl->setParamNormalized(queue->getParameterId(), v);
            }
            out.clearQueue();
            pos += 512;
            // L'interface tourne à ~30 Hz : un appel tous les trois blocs.
            if (poll && b % 3 == 0) {
                const std::string m = ctrl->pollAutomation();
                if (!m.empty()) std::printf("      [OMNIS] %s\n", m.c_str());
            }
            // Laisser le thread d'écoute suivre (~4x le temps réel).
            if (b % 8 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    double plain(ParamId id) {
        const auto* spec = params::findParameter(id);
        return params::toPlain(*spec, ctrl->getParamNormalized(static_cast<ParamID>(id)));
    }
};

} // namespace

OMNIS_TEST("plug-in barre IA: « plus de punch, plus fort, garde ma 808 » règle les modules et mène le volume") {
    Host h;
    // Avant d'avoir écouté : OMNIS demande de lancer la lecture et garde la demande.
    const std::string early = h.ctrl->ask("plus de punch et plus fort, garde ma 808");
    std::printf("      %s\n", early.c_str());
    OMNIS_CHECK(early.find("lecture") != std::string::npos);
    h.run(16.0, true);
    std::printf("      punch %.0f %%, limiteur %.1f dB, sortie %.1f LUFS court terme\n", h.plain(ParamId::LimPunch), h.plain(ParamId::LimGain),
                h.plain(ParamId::MeterShortTerm));
    OMNIS_CHECK(h.plain(ParamId::LimPunch) >= 25.0);
    OMNIS_CHECK(h.plain(ParamId::LimGain) > 1.0); // « plus fort » : le limiteur a été poussé
    // Une seconde demande, entendue cette fois tout de suite.
    const std::string answer = h.ctrl->ask("un peu plus large dans les aigus et plus chaud");
    std::printf("      %s\n", answer.c_str());
    OMNIS_CHECK(answer.find("Fait") != std::string::npos);
    OMNIS_CHECK(h.plain(static_cast<ParamId>(813)) > 100.0);
    OMNIS_CHECK(h.plain(ParamId::InflHighEffect) > 0.0);
}

OMNIS_TEST("plug-in AUTO: écoute 10 s du passage joué, règle la chaîne vers le style et atteint son volume") {
    Host h;
    h.run(1.0, false);
    h.ctrl->startAuto();
    h.run(30.0, true);
    const double st = h.plain(ParamId::MeterShortTerm);
    std::printf("      limiteur %.1f dB, punch %.0f %%, sortie %.1f LUFS\n", h.plain(ParamId::LimGain), h.plain(ParamId::LimPunch), st);
    OMNIS_CHECK(h.ctrl->autoProgress() < 0.0); // terminé
    OMNIS_CHECK(h.plain(ParamId::LimPunch) >= 15.0);
    OMNIS_CHECK(h.plain(ParamId::LimGain) > 3.0);
    OMNIS_CHECK(st > -14.5 && st < -4.5);
}
