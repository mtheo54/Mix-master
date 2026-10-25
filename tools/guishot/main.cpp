// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/guishot/main.cpp — Rend la console OMNIS hors écran (PNG), sans DAW :
//  le vrai processeur tourne sur une boucle de test, le vrai contrôleur et la
//  vraie interface lisent ce qu'il mesure. Sert à vérifier l'interface page
//  par page (outil de développement, Linux).
//
//    omnis_guishot <dossier> [master|stem]
// =============================================================================
#include "TestSignals.h"

#include "params/Parameters.h"
#include "plugin/vst3/OmnisController.h"
#include "plugin/vst3/OmnisEditor.h"
#include "plugin/vst3/OmnisProcessor.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/vstguiinit.h"

#include <dlfcn.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;
using omnis::params::ParamId;

// vstguieditor.cpp (vstgui_support) attend le module du plug-in.
void* moduleHandle = nullptr;

namespace {

struct Rig {
    HostApplication host;
    IPtr<omnis::vst3::OmnisProcessor> proc = owned(new omnis::vst3::OmnisProcessor());
    IPtr<omnis::vst3::OmnisController> ctrl = owned(new omnis::vst3::OmnisController());
    HostProcessData data;
    ParameterChanges in{64}, out{64};
    ProcessContext context{};
    omnis::tools::AudioFile signal;
    int pos = 0;

    void set(ParamId id, double plain) {
        const auto* spec = omnis::params::findParameter(id);
        const double n = omnis::params::toNormalized(*spec, plain);
        int32 index = 0;
        if (auto* q = in.addParameterData(static_cast<ParamID>(id), index)) {
            int32 p = 0;
            q->addPoint(0, n, p);
        }
        ctrl->setParamNormalized(static_cast<ParamID>(id), n);
    }
    void run(double seconds) {
        const int blocks = static_cast<int>(seconds * 48000.0 / 512.0);
        for (int b = 0; b < blocks; ++b) {
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
        }
    }
};

bool savePng(VSTGUI::CFrame* frame, const std::string& path) {
    auto ctx = VSTGUI::COffscreenContext::create(VSTGUI::CPoint(omnis::vst3::OmnisEditor::kWidth, omnis::vst3::OmnisEditor::kHeight), 1.0);
    if (!ctx) return false;
    ctx->beginDraw();
    frame->drawRect(ctx, frame->getViewSize());
    ctx->endDraw();
    auto png = VSTGUI::getPlatformFactory().createBitmapMemoryPNGRepresentation(ctx->getBitmap()->getPlatformBitmap());
    if (png.empty()) return false;
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const std::string mode = argc > 2 ? argv[2] : "master";
    VSTGUI::init(dlopen(nullptr, RTLD_LAZY));
    {
        Rig rig;
        rig.proc->initialize(&rig.host);
        rig.ctrl->initialize(&rig.host);
        rig.proc->connect(rig.ctrl);
        rig.ctrl->connect(rig.proc);
        SpeakerArrangement ins[2] = {SpeakerArr::kStereo, SpeakerArr::kStereo}, outs[1] = {SpeakerArr::kStereo};
        rig.proc->setBusArrangements(ins, 2, outs, 1);
        ProcessSetup setup{kRealtime, kSample32, 512, 48000.0};
        rig.proc->setupProcessing(setup);
        rig.proc->setActive(true);
        rig.proc->setProcessing(true);
        rig.data.prepare(*rig.proc, 512, kSample32);
        rig.context.sampleRate = 48000.0;
        rig.context.state = ProcessContext::kPlaying;
        rig.data.processContext = &rig.context;
        rig.signal = omnis::tools::signals::drumLoop(48000.0, 8);
        // Une boucle de drums plus un bruit rose : de quoi remplir les écrans.
        const auto pink = omnis::tools::signals::pinkNoise(48000.0, rig.signal.numFrames() / 48000.0, -26.0, 3);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < rig.signal.numFrames(); ++i)
                rig.signal.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] += pink.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)];
        if (mode == "stem") rig.set(ParamId::LinkRole, 1.0);
        rig.set(ParamId::LimGain, 7.0);
        rig.set(ParamId::LimPunch, 35.0);
        rig.set(ParamId::InflLowEffect, 30.0);
        rig.set(ParamId::InflHighEffect, 45.0);
        rig.set(ParamId::ClipDrive, 3.0);
        rig.set(ParamId::SubGain, 2.0);
        rig.set(ParamId::MatchTilt, 1.0);
        rig.set(static_cast<ParamId>(213), 2.5);
        rig.set(static_cast<ParamId>(226), -1.5);
        rig.set(static_cast<ParamId>(812), 130.0);
        rig.set(omnis::params::mbId(1, omnis::params::MbControl::Threshold), -20.0);
        rig.set(omnis::params::mbId(1, omnis::params::MbControl::Ratio), 3.0);
        rig.run(4.0);

        auto* editor = new omnis::vst3::OmnisEditor(rig.ctrl);
        auto* frame = new VSTGUI::CFrame(VSTGUI::CRect(0, 0, omnis::vst3::OmnisEditor::kWidth, omnis::vst3::OmnisEditor::kHeight), editor);
        editor->buildInto(frame);
        for (int step = 0; step < 10; ++step) {
            editor->startTutorial(step);
            editor->tick();
            const std::string path = dir + "/omnis-" + mode + "-tuto-" + std::to_string(step + 1) + ".png";
            std::printf("%s %s\n", savePng(frame, path) ? "écrit" : "ÉCHEC", path.c_str());
        }
        editor->startTutorial(-1);
        const std::map<int, std::string> pages{{0, "1-sub"}, {1, "2-match"}, {2, "3-inflator"}, {3, "4-imager"}, {4, "5-multiband"},
                                               {5, "6-clipper"}, {6, "7-limiter"}, {7, "8-piste"}, {8, "9-chef"}};
        for (const auto& [page, name] : pages) {
            editor->selectPage(page);
            for (int t = 0; t < 40; ++t) {
                rig.run(1.0 / 30.0);
                editor->tick();
            }
            const std::string path = dir + "/omnis-" + mode + "-" + name + ".png";
            std::printf("%s %s\n", savePng(frame, path) ? "écrit" : "ÉCHEC", path.c_str());
        }
        frame->forget();
        editor->forget();
        rig.proc->setProcessing(false);
        rig.proc->setActive(false);
        rig.ctrl->terminate();
        rig.proc->terminate();
    }
    VSTGUI::exit();
    return 0;
}

// vstguieditor.cpp demande la fabrique du plug-in (pour le module) : aucune ici.
extern "C" Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() { return nullptr; }
