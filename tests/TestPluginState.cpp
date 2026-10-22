// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestPluginState.cpp — État du processeur VST3 (aller-retour complet).
//
//  Le validateur Steinberg ne teste l'état qu'aux valeurs par défaut : il n'a
//  pas vu qu'une capacité fixe de 64 paramètres faisait perdre (et écrire à 0)
//  tous les paramètres suivants. Ici chaque paramètre persistant reçoit une
//  valeur distincte, différente de son défaut, puis l'état est relu.
// =============================================================================
#include "TestFramework.h"

#include "params/Parameters.h"
#include "plugin/vst3/OmnisProcessor.h"
#include "plugin/vst3/OmnisState.h"

#include "public.sdk/source/common/memorystream.h"

#include <map>

using namespace omnis;
using namespace Steinberg;

OMNIS_TEST("plug-in: getState(setState(x)) = x pour les 90 paramètres persistants") {
    std::map<std::uint32_t, double> written;
    int index = 0;
    for (const auto& spec : params::allParameters()) {
        if (!params::isPersistent(spec)) continue;
        written[static_cast<std::uint32_t>(spec.id)] = 0.05 + 0.9 * (index++ % 97) / 97.0;
    }
    OMNIS_CHECK(written.size() >= 90);

    IPtr<MemoryStream> in = owned(new MemoryStream());
    OMNIS_CHECK(vst3::writeState(in, [&](std::uint32_t id) { return written[id]; }));
    in->seek(0, IBStream::kIBSeekSet, nullptr);

    IPtr<vst3::OmnisProcessor> processor = owned(new vst3::OmnisProcessor());
    OMNIS_CHECK(processor->setState(in) == kResultOk);
    IPtr<MemoryStream> out = owned(new MemoryStream());
    OMNIS_CHECK(processor->getState(out) == kResultOk);
    out->seek(0, IBStream::kIBSeekSet, nullptr);

    std::map<std::uint32_t, double> read;
    OMNIS_CHECK(vst3::readState(out, [&](std::uint32_t id, double v) { read[id] = v; }));
    OMNIS_CHECK(read.size() == written.size());
    int mismatches = 0;
    for (const auto& [id, v] : written)
        if (std::fabs(read[id] - v) > 1e-12) ++mismatches;
    OMNIS_CHECK(mismatches == 0);
}

// -----------------------------------------------------------------------------
//  Écoute d'une référence par un hôte : bus auxiliaire, paramètre, base, état.
// -----------------------------------------------------------------------------
#include "TestSignals.h"

#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace {

void setUserDataDir(const std::string& dir) {
#ifdef _WIN32
    _putenv_s("OMNIS_USER_DATA_DIR", dir.c_str());
#else
    setenv("OMNIS_USER_DATA_DIR", dir.c_str(), 1);
#endif
}

} // namespace

OMNIS_TEST("plug-in: la référence jouée sur l'entrée auxiliaire entre dans la base et dans la session") {
    using namespace Steinberg::Vst;
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "omnis_test_user_data";
    fs::remove_all(dir);
    setUserDataDir(dir.string());

    IPtr<vst3::OmnisProcessor> p = owned(new vst3::OmnisProcessor());
    OMNIS_CHECK(p->initialize(nullptr) == kResultOk);
    SpeakerArrangement ins[2] = {SpeakerArr::kStereo, SpeakerArr::kStereo}, outs[1] = {SpeakerArr::kStereo};
    OMNIS_CHECK(p->setBusArrangements(ins, 2, outs, 1) == kResultOk);
    OMNIS_CHECK(p->activateBus(kAudio, kInput, 1, true) == kResultOk);
    ProcessSetup setup{kRealtime, kSample32, 512, 48000.0};
    OMNIS_CHECK(p->setupProcessing(setup) == kResultOk);
    OMNIS_CHECK(p->setActive(true) == kResultOk);
    p->setProcessing(true);

    HostProcessData data;
    OMNIS_CHECK(data.prepare(*p, 512, kSample32));
    OMNIS_CHECK(data.numInputs == 2);
    ParameterChanges changes(8), outChanges(32);
    const auto ref = tools::signals::pinkNoise(48000.0, 6.0, -14.0, 31, true);
    const auto prod = tools::signals::pinkNoise(48000.0, 6.0, -26.0, 32, true);
    const auto block = [&](int pos, int capture) {
        changes.clearQueue();
        outChanges.clearQueue();
        if (capture >= 0) {
            int32 q = 0, point = 0;
            if (IParamValueQueue* queue = changes.addParameterData(static_cast<ParamID>(params::ParamId::RefCapture), q))
                queue->addPoint(0, capture, point);
        }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i) {
                const auto k = static_cast<std::size_t>(std::min(pos + i, ref.numFrames() - 1));
                data.inputs[0].channelBuffers32[c][i] = static_cast<float>(prod.channels[static_cast<std::size_t>(c)][k]);
                data.inputs[1].channelBuffers32[c][i] = static_cast<float>(ref.channels[static_cast<std::size_t>(c)][k]);
            }
        data.numSamples = 512;
        data.inputParameterChanges = &changes;
        data.outputParameterChanges = &outChanges;
        return p->process(data);
    };
    OMNIS_CHECK(block(0, 1) == kResultOk); // « Listen Reference » activé
    for (int pos = 512; pos + 512 <= ref.numFrames(); pos += 512) block(pos, -1);
    block(0, 0); // arrêt de l'écoute

    const fs::path library = dir / "references_utilisateur.txt";
    for (int i = 0; i < 600 && !fs::exists(library); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    OMNIS_CHECK(fs::exists(library));
    std::ifstream in(library);
    std::stringstream text;
    text << in.rdbuf();
    OMNIS_CHECK(text.str().find("Référence écoutée") != std::string::npos);

    // La session garde la référence et la prod (données en plus des paramètres).
    IPtr<MemoryStream> state = owned(new MemoryStream());
    OMNIS_CHECK(p->getState(state) == kResultOk);
    state->seek(0, IBStream::kIBSeekSet, nullptr);
    OMNIS_CHECK(vst3::readState(state, [](std::uint32_t, double) {}));
    std::string extras;
    OMNIS_CHECK(vst3::readExtras(state, extras));
    OMNIS_CHECK(extras.find("Prod") != std::string::npos && extras.find("Référence écoutée") != std::string::npos);

    p->setProcessing(false);
    p->setActive(false);
    p->terminate();
    fs::remove_all(dir);
}
