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

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <chrono>
#include <cmath>
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
    // Machine rapide : le son passe bien plus vite que le temps réel, l'écoute (thread
    // à part, photo toutes les 2 s) peut être en retard. On attend qu'elle ait suivi.
    for (int i = 0; i < 80 && h.plain(ParamId::LimPunch) < 25.0; ++i) {
        h.run(0.25, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    for (int i = 0; i < 40; ++i) {
        h.run(0.25, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
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

OMNIS_TEST("plug-in menu RÉFÉRENCE: styles, artistes, titres de la base ; un titre choisi règle la chaîne et reste avec la session") {
    Host h;
    const auto tree = h.ctrl->referenceTree();
    std::size_t titles = 0;
    for (const auto& a : tree.artists) titles += a.titles.size();
    std::printf("      %zu styles, %zu artistes, %zu titres mesurés\n", tree.styles.size(), tree.artists.size(), titles);
    OMNIS_CHECK(!tree.styles.empty() && !tree.artists.empty() && titles > 0);
    // Style -> artiste -> ses titres : « rage » contient 2hollis, qui contient poster boy.
    std::string title;
    for (const auto& st : tree.styles)
        if (st.name == "rage")
            for (const auto& a : st.artists)
                if (a.name == "2hollis")
                    for (const auto& t : a.titles)
                        if (t.title == "poster boy") title = t.name;
    OMNIS_CHECK(title == "2hollis - poster boy");
    // Pas encore écouté : le choix attend la lecture.
    const std::string early = h.ctrl->chooseReference(vst3::OmnisController::RefKind::Title, title);
    std::printf("      %s\n", early.c_str());
    OMNIS_CHECK(early.find("lecture") != std::string::npos);
    OMNIS_CHECK(h.ctrl->referenceName() == title);
    h.run(12.0, true);
    const auto curve = [&h] {
        double sum = 0.0;
        for (int k = 0; k < params::kMatchBands; ++k) sum += std::fabs(h.plain(params::matchMidId(k)));
        return sum;
    };
    for (int i = 0; i < 80 && curve() < 0.1; ++i) {
        h.run(0.25, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    std::printf("      courbe du Match EQ : %.1f dB cumulés\n", curve());
    OMNIS_CHECK(curve() > 0.1); // la courbe vers le titre est écrite
    // Choisi une fois que la prod est entendue : appliqué tout de suite.
    const auto card = h.ctrl->referenceCard();
    std::printf("      carte : %s · %s\n", card.name.c_str(), card.detail.c_str());
    OMNIS_CHECK(card.name == title && card.titles == 1 && card.detail.find("BPM") != std::string::npos);
    const std::string now = h.ctrl->chooseReference(vst3::OmnisController::RefKind::Style, tree.styles.front().name);
    std::printf("      %s\n", now.c_str());
    OMNIS_CHECK(now.find("Fait : vers ") == 0);
    // Gardé avec la session (état du contrôleur).
    MemoryStream stream;
    OMNIS_CHECK(h.ctrl->getState(&stream) == kResultOk);
    IPtr<vst3::OmnisController> other = owned(new vst3::OmnisController());
    other->initialize(&h.app);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    OMNIS_CHECK(other->setState(&stream) == kResultOk);
    OMNIS_CHECK(other->referenceKind() == vst3::OmnisController::RefKind::Style);
    OMNIS_CHECK(other->referenceName() == tree.styles.front().name);
    other->terminate();
    // Aucune : AUTO revient au style des mots.
    OMNIS_CHECK(h.ctrl->chooseReference(vst3::OmnisController::RefKind::None, {}).find("Pas de référence") == 0);
    OMNIS_CHECK(h.ctrl->referenceKind() == vst3::OmnisController::RefKind::None);
}
