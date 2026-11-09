// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestPluginLink.cpp — OMNIS sur les pistes et les bus d'un projet :
//  le son passe tel quel, sans latence ; le master (et chaque bus) voit quels
//  éléments sont dans quel bus, avec leur nom d'origine et ce qu'OMNIS devine.
// =============================================================================
#include "TestFramework.h"

#include "core/UserDataDir.h"
#include "link/LinkBus.h"
#include "link/SharedRegion.h"
#include "params/Parameters.h"
#include "plugin/vst3/OmnisController.h"
#include "plugin/vst3/OmnisIds.h"
#include "plugin/vst3/OmnisProcessor.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstchannelcontextinfo.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace omnis;
using namespace Steinberg;
using namespace Steinberg::Vst;
using params::ParamId;

namespace {

constexpr double kFs = 48000.0;
constexpr double kPi = std::numbers::pi;

/// Une instance du plug-in posée sur une piste (processeur + contrôleur reliés).
struct Instance {
    HostApplication app;
    IPtr<vst3::OmnisProcessor> proc = owned(new vst3::OmnisProcessor());
    IPtr<vst3::OmnisController> ctrl = owned(new vst3::OmnisController());
    HostProcessData data;
    ParameterChanges in{16}, out{64};
    ProcessContext context{};

    Instance(const char* trackName, params::InstanceRole role) {
        proc->initialize(&app);
        ctrl->initialize(&app);
        proc->connect(ctrl);
        ctrl->connect(proc);
        SpeakerArrangement ins[2] = {SpeakerArr::kStereo, SpeakerArr::kStereo}, outs[1] = {SpeakerArr::kStereo};
        proc->setBusArrangements(ins, 2, outs, 1);
        ProcessSetup setup{kRealtime, kSample32, 512, kFs};
        proc->setupProcessing(setup);
        proc->setActive(true);
        proc->setProcessing(true);
        data.prepare(*proc, 512, kSample32);
        context.sampleRate = kFs;
        context.state = ProcessContext::kPlaying;
        data.processContext = &context;
        // L'hôte donne le nom de la piste (IInfoListener), puis la personne choisit le rôle.
        if (trackName) {
            auto list = HostAttributeList::make();
            const auto name = VST3::StringConvert::convert(std::string(trackName));
            list->setString(ChannelContext::kChannelNameKey, name.data());
            ctrl->setChannelContextInfos(list);
        }
        const auto* spec = params::findParameter(ParamId::InstanceRole);
        ctrl->setParamNormalized(static_cast<ParamID>(ParamId::InstanceRole), params::toNormalized(*spec, static_cast<double>(role)));
    }
    ~Instance() {
        proc->setProcessing(false);
        proc->setActive(false);
        ctrl->terminate();
        proc->terminate();
    }
    /// Un bloc de 512 à la position `pos` du projet. Renvoie l'écart max entrée / sortie.
    double block(const std::vector<double>& mono, int pos) {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                data.inputs[0].channelBuffers32[c][i] = static_cast<float>(mono[static_cast<std::size_t>((pos + i) % static_cast<int>(mono.size()))]);
        context.projectTimeSamples = pos;
        data.numSamples = 512;
        in.clearQueue();
        out.clearQueue();
        data.inputParameterChanges = &in;
        data.outputParameterChanges = &out;
        proc->process(data);
        double diff = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i) diff = std::max(diff, std::fabs(static_cast<double>(data.outputs[0].channelBuffers32[c][i] - data.inputs[0].channelBuffers32[c][i])));
        return diff;
    }
};

std::vector<double> kickPart(double seconds) {
    std::vector<double> m(static_cast<std::size_t>(seconds * kFs), 0.0);
    const double beat = 60.0 / 140.0;
    for (double t = 0.0; t < seconds; t += beat)
        for (std::size_t k = 0; k < static_cast<std::size_t>(0.35 * kFs); ++k) {
            const auto i = static_cast<std::size_t>(t * kFs) + k;
            if (i >= m.size()) break;
            const double s = static_cast<double>(k) / kFs;
            m[i] += 0.7 * std::exp(-s / 0.12) * std::sin(2 * kPi * (50.0 * s + 3.0 * (1.0 - std::exp(-s / 0.03))));
        }
    return m;
}

std::vector<double> reversePart(double seconds) {
    std::vector<double> m(static_cast<std::size_t>(seconds * kFs), 0.0);
    std::mt19937 rng(5);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    double prev = 0.0;
    for (std::size_t i = 0; i < m.size(); ++i) {
        const double t = static_cast<double>(i) / kFs, cycle = std::fmod(t, 3.0);
        const double x = u(rng);
        if (cycle >= 1.5) m[i] = 0.4 * std::pow((cycle - 1.5) / 1.5, 3.0) * (x - prev); // bruit aigu qui monte, coupé net
        prev = x;
    }
    return m;
}

std::vector<double> leadPart(double seconds) {
    std::vector<double> m(static_cast<std::size_t>(seconds * kFs), 0.0);
    const double step = 60.0 / 140.0 / 2.0;
    for (std::size_t i = 0; i < m.size(); ++i) {
        const double t = static_cast<double>(i) / kFs;
        m[i] = 0.15 * std::exp(-std::fmod(t, step) / 0.15) * std::sin(2 * kPi * (static_cast<int>(t / step) % 3 ? 660.0 : 990.0) * t);
    }
    return m;
}

void setNamespace(const std::string& ns) {
#if defined(_WIN32)
    _putenv_s("OMNIS_LINK_NAMESPACE", ns.c_str());
#else
    if (ns.empty()) unsetenv("OMNIS_LINK_NAMESPACE");
    else setenv("OMNIS_LINK_NAMESPACE", ns.c_str(), 1);
#endif
}

} // namespace

OMNIS_TEST("plug-in pistes et bus: le son passe tel quel sans latence, le master voit qui est dans quel bus") {
    const std::string ns = "omnisplug" + std::to_string(static_cast<unsigned>(std::random_device{}() % 100000)) + "x";
    setNamespace(ns);
    {
        const double seconds = 12.0;
        const auto kick = kickPart(seconds), rev = reversePart(seconds), lead = leadPart(seconds);
        std::vector<double> drums(kick.size()), efx(kick.size());
        for (std::size_t i = 0; i < kick.size(); ++i) {
            drums[i] = 0.8 * kick[i];
            efx[i] = rev[i];
        }
        auto kickTrack = std::make_unique<Instance>("SH_Kick_Hard_04", params::InstanceRole::Track);
        auto revTrack = std::make_unique<Instance>("Rev Crash 03", params::InstanceRole::Track);
        auto leadTrack = std::make_unique<Instance>("Serum Lead", params::InstanceRole::Track);
        auto drumBus = std::make_unique<Instance>("DRUMS", params::InstanceRole::Bus);
        auto efxBus = std::make_unique<Instance>("EFX", params::InstanceRole::Bus);
        auto master = std::make_unique<Instance>("Master", params::InstanceRole::Master);
        // Bus et Piste : aucune latence ; le master garde la sienne (phase linéaire).
        OMNIS_CHECK(kickTrack->proc->getLatencySamples() == 0 && drumBus->proc->getLatencySamples() == 0);
        OMNIS_CHECK(master->proc->getLatencySamples() > 0);
        // La lecture, comme l'hôte : toutes les instances au même instant du projet, ~6x le temps réel.
        double worst = 0.0;
        const int blocks = static_cast<int>(seconds * kFs / 512.0);
        for (int b = 0; b < blocks; ++b) {
            const int pos = b * 512;
            worst = std::max({worst, kickTrack->block(kick, pos), revTrack->block(rev, pos), leadTrack->block(lead, pos), drumBus->block(drums, pos),
                              efxBus->block(efx, pos)});
            master->block(lead, pos);
            if (b % 16 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        OMNIS_CHECK(worst == 0.0); // bit à bit
        // Le thread du lien publie toutes les 500 ms : on lui laisse le temps.
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        const auto& c = master->ctrl->composition();
        const std::string text = master->ctrl->compositionText();
        std::printf("%s\n", text.c_str());
        const stems::BusView* d = nullptr;
        const stems::BusView* e = nullptr;
        for (const auto& bus : c.buses) {
            if (bus.name == "DRUMS") d = &bus;
            if (bus.name == "EFX") e = &bus;
        }
        OMNIS_CHECK(d && e);
        if (d && e) {
            OMNIS_CHECK(d->members.size() == 1 && d->members[0].name == "SH_Kick_Hard_04" && d->members[0].element == stems::Element::Kick);
            OMNIS_CHECK(e->members.size() == 1 && e->members[0].name == "Rev Crash 03" && e->members[0].element == stems::Element::ReverseCymbal);
            OMNIS_CHECK(d->family == stems::Family::Drums && e->family == stems::Family::Fx);
            OMNIS_CHECK(d->kick); // l'écoute du bus entend le kick
        }
        OMNIS_CHECK(c.direct.size() == 1 && c.direct[0].name == "Serum Lead" && c.direct[0].family() == stems::Family::Instrument);
        OMNIS_CHECK(text.find("« SH_Kick_Hard_04 » → Kick (DRUMS") != std::string::npos);
        // La barre IA : « composition », et une instance de bus le dit aussi.
        OMNIS_CHECK(master->ctrl->ask("qu'est-ce qu'il y a dans mes bus ?").find("BUS « DRUMS »") != std::string::npos);
        OMNIS_CHECK(drumBus->ctrl->ask("monte le sub").find("RÔLE Bus") != std::string::npos);
        // Repasser une piste en Master : elle quitte la région (plus vue), sa latence revient.
        const auto* spec = params::findParameter(ParamId::InstanceRole);
        leadTrack->ctrl->setParamNormalized(static_cast<ParamID>(ParamId::InstanceRole), params::toNormalized(*spec, 0.0));
        OMNIS_CHECK(leadTrack->proc->getLatencySamples() > 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        const auto& after = master->ctrl->composition();
        OMNIS_CHECK(after.direct.empty());
        // Une instance retirée du projet : elle disparaît.
        efxBus.reset();
        revTrack.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        OMNIS_CHECK(master->ctrl->composition().buses.size() == 1);
    }
    link::SharedRegion::unlink(link::regionName());
    setNamespace("");
}
