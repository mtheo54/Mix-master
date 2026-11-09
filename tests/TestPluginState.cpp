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
#include "base/source/fstreamer.h"

#include <cmath>

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

OMNIS_TEST("plug-in: une session d'avant le compresseur (ordre à 7 modules, id 8) se relit avec le compresseur après le Match EQ") {
    // État écrit par une version antérieure : l'ordre « Match EQ en premier » (rang 720 sur 7).
    IPtr<MemoryStream> in = owned(new MemoryStream());
    {
        IBStreamer s(in, kLittleEndian);
        s.writeInt32u(vst3::kStateMagic);
        s.writeInt32u(1);
        s.writeInt32u(2);
        s.writeInt32u(8);
        s.writeDouble(720.0 / 5039.0);
        s.writeInt32u(static_cast<std::uint32_t>(params::ParamId::InflDrive));
        s.writeDouble(0.75);
    }
    in->seek(0, IBStream::kIBSeekSet, nullptr);
    IPtr<vst3::OmnisProcessor> processor = owned(new vst3::OmnisProcessor());
    OMNIS_CHECK(processor->setState(in) == kResultOk);
    IPtr<MemoryStream> out = owned(new MemoryStream());
    OMNIS_CHECK(processor->getState(out) == kResultOk);
    out->seek(0, IBStream::kIBSeekSet, nullptr);
    std::map<std::uint32_t, double> read;
    OMNIS_CHECK(vst3::readState(out, [&](std::uint32_t id, double v) { read[id] = v; }));
    OMNIS_CHECK(read.count(8) == 0); // l'ancien id n'est plus écrit
    const auto order = params::chainOrder(static_cast<int>(std::lround(read[static_cast<std::uint32_t>(params::ParamId::ChainOrder)] * (params::kChainOrders - 1))));
    using M = params::Module;
    const std::array<M, 8> expected{M::Match, M::Comp, M::Sub, M::Inflator, M::Imager, M::Multiband, M::Clipper, M::Limiter};
    OMNIS_CHECK(order == expected);
    OMNIS_CHECK(std::fabs(read[static_cast<std::uint32_t>(params::ParamId::InflDrive)] - 0.75) < 1e-12);
    OMNIS_CHECK(read[static_cast<std::uint32_t>(params::ParamId::CompOn)] == 0.0); // le compresseur arrive éteint : même son
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
    std::error_code ec;
    fs::remove_all(dir, ec);
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
    // 61 s : il faut au moins 60 s d'écoute pour retenir une référence.
    const auto ref = tools::signals::pinkNoise(48000.0, 61.0, -14.0, 31, true);
    const auto prod = tools::signals::pinkNoise(48000.0, 61.0, -26.0, 32, true);
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
    for (int pos = 512; pos + 512 <= ref.numFrames(); pos += 512) {
        block(pos, -1);
        // Un vrai hôte joue en temps réel ; on joue ~20x plus vite, ce que
        // l'analyse suit sans perdre d'échantillons.
        if ((pos / 512) % 2 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    block(0, 0); // arrêt de l'écoute

    const fs::path library = dir / "references_utilisateur.txt";
    for (int i = 0; i < 1000 && !fs::exists(library); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    OMNIS_CHECK(fs::exists(library));
    std::stringstream text;
    {
        // Fermé avant la suppression du dossier : Windows refuse d'effacer un fichier ouvert.
        std::ifstream in(library);
        text << in.rdbuf();
    }
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
    fs::remove_all(dir, ec);
}

OMNIS_TEST("plug-in: une écoute de moins de 60 s n'est pas retenue") {
    using namespace Steinberg::Vst;
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "omnis_test_user_data_short";
    std::error_code ec;
    fs::remove_all(dir, ec);
    setUserDataDir(dir.string());
    IPtr<vst3::OmnisProcessor> p = owned(new vst3::OmnisProcessor());
    p->initialize(nullptr);
    SpeakerArrangement ins[2] = {SpeakerArr::kStereo, SpeakerArr::kStereo}, outs[1] = {SpeakerArr::kStereo};
    p->setBusArrangements(ins, 2, outs, 1);
    p->activateBus(kAudio, kInput, 1, true);
    ProcessSetup setup{kRealtime, kSample32, 512, 48000.0};
    p->setupProcessing(setup);
    p->setActive(true);
    HostProcessData data;
    data.prepare(*p, 512, kSample32);
    ParameterChanges changes(8), outChanges(32);
    const auto play = [&](int blocks, int capture) {
        for (int b = 0; b < blocks; ++b) {
            changes.clearQueue();
            outChanges.clearQueue();
            if (b == 0 && capture >= 0) {
                int32 q = 0, point = 0;
                if (IParamValueQueue* queue = changes.addParameterData(static_cast<ParamID>(params::ParamId::RefCapture), q))
                    queue->addPoint(0, capture, point);
            }
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i) data.inputs[1].channelBuffers32[c][i] = 0.3f * std::sin(0.05f * static_cast<float>(i + 512 * b));
            data.numSamples = 512;
            data.inputParameterChanges = &changes;
            data.outputParameterChanges = &outChanges;
            p->process(data);
        }
    };
    play(940, 1); // 10 s
    play(1, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    OMNIS_CHECK(!fs::exists(dir / "references_utilisateur.txt"));
    p->setActive(false);
    p->terminate();
    fs::remove_all(dir, ec);
}

// -----------------------------------------------------------------------------
//  Une session abîmée (fichier de projet tronqué, autre version, octets au
//  hasard) : l'hôte nous la donne telle quelle. Rien ne doit planter.
// -----------------------------------------------------------------------------
#include "plugin/vst3/OmnisController.h"

#include <random>

OMNIS_TEST("plug-in: état de session tronqué ou corrompu (processeur, contrôleur, composant) : refusé ou lu, jamais de plantage") {
    // Un état valide, puis ses variantes.
    IPtr<MemoryStream> good = owned(new MemoryStream());
    OMNIS_CHECK(vst3::writeState(good, [](std::uint32_t) { return 0.37; }));
    int64 size = 0;
    good->seek(0, IBStream::kIBSeekEnd, &size);
    std::string bytes(static_cast<std::size_t>(size), '\0');
    good->seek(0, IBStream::kIBSeekSet, nullptr);
    int32 got = 0;
    good->read(bytes.data(), static_cast<int32>(bytes.size()), &got);
    std::mt19937 rng(31);
    std::uniform_int_distribution<int> byte(0, 255);
    std::vector<std::string> cases;
    for (std::size_t n = 0; n <= bytes.size(); n += 3) cases.push_back(bytes.substr(0, n));
    for (int i = 0; i < 400; ++i) {
        std::string s = bytes;
        for (int e = 0; e < 1 + i % 8; ++e) s[static_cast<std::size_t>(byte(rng)) % s.size()] = static_cast<char>(byte(rng));
        cases.push_back(s);
    }
    for (int i = 0; i < 50; ++i) {
        std::string s(static_cast<std::size_t>(byte(rng)) * 4, '\0');
        for (auto& c : s) c = static_cast<char>(byte(rng));
        cases.push_back(s);
    }
    IPtr<vst3::OmnisProcessor> processor = owned(new vst3::OmnisProcessor());
    IPtr<vst3::OmnisController> controller = owned(new vst3::OmnisController());
    int accepted = 0;
    for (const auto& s : cases) {
        IPtr<MemoryStream> in = owned(new MemoryStream());
        in->write(const_cast<char*>(s.data()), static_cast<int32>(s.size()), nullptr);
        in->seek(0, IBStream::kIBSeekSet, nullptr);
        if (processor->setState(in) == kResultOk) ++accepted;
        in->seek(0, IBStream::kIBSeekSet, nullptr);
        (void)controller->setState(in);
        in->seek(0, IBStream::kIBSeekSet, nullptr);
        (void)controller->setComponentState(in);
        // Ce qui a été lu reste dans les bornes.
        IPtr<MemoryStream> out = owned(new MemoryStream());
        OMNIS_CHECK(processor->getState(out) == kResultOk);
        out->seek(0, IBStream::kIBSeekSet, nullptr);
        int outOfRange = 0;
        OMNIS_CHECK(vst3::readState(out, [&outOfRange](std::uint32_t, double v) {
            if (!(v >= 0.0 && v <= 1.0)) ++outOfRange;
        }));
        OMNIS_CHECK(outOfRange == 0);
    }
    std::printf("      %zu sessions essayées, %d acceptées\n", cases.size(), accepted);
}

using namespace Steinberg::Vst;

OMNIS_TEST("plug-in: hôte aberrant (fréquence 0 / NaN / 10 MHz, bloc de 2^30, bloc plus grand qu'annoncé) : refusé ou absorbé") {
    IPtr<vst3::OmnisProcessor> p = owned(new vst3::OmnisProcessor());
    p->initialize(nullptr);
    SpeakerArrangement ins[2] = {SpeakerArr::kStereo, SpeakerArr::kStereo}, outs[1] = {SpeakerArr::kStereo};
    p->setBusArrangements(ins, 2, outs, 1);
    for (const double bad : {0.0, -48000.0, std::nan(""), 1.0e7}) {
        ProcessSetup setup{kRealtime, kSample32, 512, bad};
        OMNIS_CHECK(p->setupProcessing(setup) != kResultOk);
    }
    ProcessSetup huge{kRealtime, kSample32, 1 << 30, 48000.0};
    OMNIS_CHECK(p->setupProcessing(huge) == kResultOk); // préparé à 16384, pas à 2^30
    p->setActive(true);
    p->setProcessing(true);
    HostProcessData data;
    data.prepare(*p, 40000, kSample32);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 40000; ++i) data.inputs[0].channelBuffers32[c][i] = 0.25f * static_cast<float>(std::sin(0.01 * i));
    data.numSamples = 40000; // plus grand que la taille préparée : découpé par le moteur
    OMNIS_CHECK(p->process(data) == kResultOk);
    bool finite = true;
    for (int i = 0; i < 40000; ++i) finite = finite && std::isfinite(data.outputs[0].channelBuffers32[0][i]);
    OMNIS_CHECK(finite);
    p->setProcessing(false);
    p->setActive(false);
    p->terminate();
}
