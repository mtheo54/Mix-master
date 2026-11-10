// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/guishot/main.cpp — Rend la console OMNIS hors écran (PNG), sans DAW :
//  le vrai processeur tourne sur une boucle de test, le vrai contrôleur et la
//  vraie interface lisent ce qu'il mesure. Sert à vérifier l'interface page
//  par page (outil de développement, Linux).
//
//    omnis_guishot <dossier> [master|travail]
// =============================================================================
#include "TestSignals.h"

#include "link/LinkBus.h"
#include "link/SharedRegion.h"
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
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/vstguiinit.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cmath>
#include <map>
#include <memory>
#include <vector>
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

namespace {

/// Un petit projet pour la page COMPOSITION : quatre bus OMNIS dans la région partagée.
std::vector<std::unique_ptr<omnis::link::Endpoint>> shotProject() {
    using namespace omnis;
    using stems::Element;
    struct Bus {
        const char* name;
        std::vector<stems::HeardItem> items;
    };
    const Bus buses[] = {
        {"DRUMS", {{Element::Kick, 1.0, 168, 0.0}, {Element::Snare, 1.0, 363, 0.0}, {Element::HiHat, 1.0, 145, 0.0}, {Element::OpenHat, 1.0, 44, 0.0}}},
        {"808", {{Element::Bass808, 1.0, 41, 37.0}}},
        {"MELO", {{Element::Lead, 0.92, 451, 0.0}, {Element::Pad, 0.92, 0, 146.0}}},
        {"EFX", {{Element::ReverseCymbal, 0.67, 2, 0.0}, {Element::Riser, 0.5, 1, 0.0}, {Element::Impact, 0.5, 1, 0.0}}},
    };
    std::vector<std::unique_ptr<link::Endpoint>> out;
    for (const auto& b : buses) {
        auto e = std::make_unique<link::Endpoint>();
        if (!e->join(b.name)) continue;
        stems::BusContent c;
        c.items = b.items;
        c.secondsHeard = 147.0;
        c.songSeconds = c.songEnd = 147.0;
        e->publish(b.name, c);
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const std::string mode = argc > 2 ? argv[2] : "master";
    VSTGUI::init(dlopen(nullptr, RTLD_LAZY));
    // Les polices du plug-in (resources/Fonts), comme dans le paquet VST3.
    if (auto* lf = VSTGUI::getPlatformFactory().asLinuxFactory()) lf->setResourcePath(OMNIS_RESOURCES_DIR);
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
        if (mode == "travail") {
            // Mode travail (latence basse) : l'hôte réactive le plug-in après le changement.
            rig.set(ParamId::LowLatency, 1.0);
            rig.run(0.1);
            rig.proc->setProcessing(false);
            rig.proc->setActive(false);
            rig.proc->setActive(true);
            rig.proc->setProcessing(true);
        }
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
        rig.set(omnis::params::ParamId::CompOn, 1.0); // le compresseur allumé : sa réduction se voit
        rig.set(omnis::params::ParamId::CompThreshold, -24.0);
        rig.run(4.0);

        auto* editor = new omnis::vst3::OmnisEditor(rig.ctrl);
        auto* frame = new VSTGUI::CFrame(VSTGUI::CRect(0, 0, omnis::vst3::OmnisEditor::kWidth, omnis::vst3::OmnisEditor::kHeight), editor);
        editor->buildInto(frame);
        for (int step = 0; step < omnis::vst3::OmnisEditor::tutorialSteps(); ++step) {
            editor->startTutorial(step);
            editor->tick();
            const std::string path = dir + "/omnis-" + mode + "-tuto-" + std::to_string(step + 1) + ".png";
            std::printf("%s %s\n", savePng(frame, path) ? "écrit" : "ÉCHEC", path.c_str());
        }
        editor->startTutorial(-1);
        // La fenêtre RÉF : tous les genres, puis une famille avec la fiche d'un genre.
        if (mode == "master") {
            const std::vector<std::array<std::string, 3>> refShots{
                {"ref-1-tous", "", ""}, {"ref-2-rap-fr", "Rap Français & New Wave", "rage fr"}, {"ref-3-electro", "Électro, House & Dance", ""}};
            for (const auto& [name, family, style] : refShots) {
                editor->openReferenceBrowser(family, style);
                editor->tick();
                const std::string path = dir + "/omnis-master-" + name + ".png";
                std::printf("%s %s\n", savePng(frame, path) ? "écrit" : "ÉCHEC", path.c_str());
            }
            editor->closeReferenceBrowser();
        }
        // Une référence choisie au menu : la face du menu la montre.
        // (OMNIS_SHOT_REF : un autre titre ; par défaut le plus long de la base, coupé « … »).
        const char* shotRef = std::getenv("OMNIS_SHOT_REF");
        if (mode == "master")
            rig.ctrl->chooseReference(omnis::vst3::OmnisController::RefKind::Title,
                                      shotRef ? shotRef : "BloodyWraith Pee - DRILL 229 (feat. Konor Marley & DAÏCO CHUP)");
        const std::map<int, std::string> pages{{0, "1-sub"}, {1, "2-match"}, {2, "3-inflator"}, {3, "4-imager"}, {4, "5-multiband"},
                                               {5, "6-clipper"}, {6, "7-limiter"}, {7, "8-comp"}, {8, "9-composition"}, {9, "10-diagnostic"}};
        // La page COMPOSITION : un projet avec ses bus (région à part, effacée à la fin).
        setenv("OMNIS_LINK_NAMESPACE", "omnisshot", 1);
        auto project = shotProject();
        for (const auto& [page, name] : pages) {
            editor->selectPage(page);
            for (int t = 0; t < 40; ++t) {
                rig.run(1.0 / 30.0);
                editor->tick();
            }
            const std::string path = dir + "/omnis-" + mode + "-" + name + ".png";
            std::printf("%s %s\n", savePng(frame, path) ? "écrit" : "ÉCHEC", path.c_str());
        }
        // La chaîne sans l'Imager ni le Comp : leurs cartes partent dans la bibliothèque (+).
        if (mode == "master") {
            rig.set(omnis::params::ParamId::ImgOn, 0.0);
            rig.set(omnis::params::ParamId::CompOn, 0.0);
            editor->selectPage(6);
            for (int t = 0; t < 10; ++t) {
                rig.run(1.0 / 30.0);
                editor->tick();
            }
            const std::string path = dir + "/omnis-master-chaine-bibliotheque.png";
            std::printf("%s %s\n", savePng(frame, path) ? "écrit" : "ÉCHEC", path.c_str());
            rig.set(omnis::params::ParamId::ImgOn, 1.0);
            rig.set(omnis::params::ParamId::CompOn, 1.0);
        }
        // Match EQ, vue DYN : deux bandes allumées (808 et sifflantes).
        {
            using omnis::params::DynControl;
            rig.set(omnis::params::dynId(0, DynControl::On), 1.0);
            rig.set(omnis::params::dynId(0, DynControl::Threshold), 0.0);
            rig.set(omnis::params::dynId(3, DynControl::On), 1.0);
            rig.set(omnis::params::dynId(3, DynControl::Range), -8.0);
            rig.set(omnis::params::dynId(3, DynControl::Threshold), 0.0);
            editor->selectPage(1);
            editor->showMatchView(1, 3);
            for (int t = 0; t < 60; ++t) {
                rig.run(1.0 / 30.0);
                editor->tick();
            }
            const std::string path = dir + "/omnis-" + mode + "-2-match-dyn.png";
            std::printf("%s %s\n", savePng(frame, path) ? "écrit" : "ÉCHEC", path.c_str());
            editor->showMatchView(0, 0);
        }
        project.clear();
        omnis::link::SharedRegion::unlink(omnis::link::regionName());
        unsetenv("OMNIS_LINK_NAMESPACE");
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
