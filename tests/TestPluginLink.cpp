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

#include "base/source/fobject.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
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
    double lastStems = -1.0, lastElement = -1.0, lastProposals = -1.0;

    Instance(std::map<ParamId, double> plain, FUnknown* host = nullptr) {
        IPtr<MemoryStream> state = owned(new MemoryStream());
        vst3::writeState(state, [&](std::uint32_t id) {
            const auto* spec = params::findParameter(id);
            const auto it = plain.find(static_cast<ParamId>(id));
            return params::toNormalized(*spec, it != plain.end() ? it->second : spec->defaultValue);
        });
        state->seek(0, IBStream::kIBSeekSet, nullptr);
        p->initialize(host);
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
            if (id != ParamId::MeterLinkStems && id != ParamId::MeterStemElement && id != ParamId::MeterProposals) continue;
            int32 offset = 0;
            ParamValue v = 0.0;
            if (queue->getPoint(queue->getPointCount() - 1, offset, v) != kResultTrue) continue;
            // Les afficheurs ne sont publiés qu'au changement : on garde la dernière valeur.
            (id == ParamId::MeterLinkStems ? lastStems : id == ParamId::MeterStemElement ? lastElement : lastProposals) =
                params::toPlain(*params::findParameter(id), v);
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

OMNIS_TEST("plug-in stem: la chaîne de l'élément agit sans latence (Auto), Manuel suit les doses") {
    setLinkNamespace("omnistestch");
    // Piano choisi par la personne, chaîne en Auto : passe-haut à 60 Hz.
    Instance stem({{ParamId::LinkRole, 1.0}, {ParamId::LinkStemKind, static_cast<double>(stems::Element::Keys)}, {ParamId::LinkGroup, 3.0}, {ParamId::StemMode, 1.0}});
    OMNIS_CHECK(stem.p->getLatencySamples() == 0);
    tools::AudioFile sig;
    sig.sampleRate = 48000.0;
    sig.channels.assign(2, std::vector<double>(3 * 48000, 0.0));
    for (std::size_t i = 0; i < sig.channels[0].size(); ++i) {
        const double t = static_cast<double>(i) / 48000.0;
        // Grondement à 10 Hz + médium : seul le grondement doit partir.
        const double v = 0.3 * std::sin(2.0 * 3.141592653589793 * 10.0 * t) + 0.2 * std::sin(2.0 * 3.141592653589793 * 1000.0 * t);
        sig.channels[0][i] = sig.channels[1][i] = v;
    }
    // Le thread du lien publie l'élément choisi en quelques dizaines de ms.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::vector<float> out;
    for (int pos = 0; pos + 512 <= 3 * 48000; pos += 512) {
        stem.block(sig, pos);
        for (int i = 0; i < 512; ++i) out.push_back(stem.data.outputs[0].channelBuffers32[0][i]);
        if (pos == 512 * 10) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // Sur la dernière seconde : le 10 Hz est retiré, le 1 kHz reste.
    const auto level = [&](const std::vector<double>& x, double hz) {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 2 * 48000; i < 3 * 48000 - 512; ++i) {
            re += x[i] * std::cos(2.0 * 3.141592653589793 * hz * static_cast<double>(i) / 48000.0);
            im += x[i] * std::sin(2.0 * 3.141592653589793 * hz * static_cast<double>(i) / 48000.0);
        }
        return db(std::hypot(re, im));
    };
    std::vector<double> o(out.begin(), out.end());
    o.resize(sig.channels[0].size(), 0.0);
    const double rumble = level(o, 10.0) - level(sig.channels[0], 10.0);
    const double mids = level(o, 1000.0) - level(sig.channels[0], 1000.0);
    std::printf("      Auto (piano) : 10 Hz %.1f dB, 1 kHz %+.2f dB\n", rumble, mids);
    OMNIS_CHECK(rumble < -25.0);
    OMNIS_CHECK(std::fabs(mids) < 0.5);

    // Manuel, largeur 0 % : la piste sort en mono.
    setLinkNamespace("omnistestch2");
    Instance manual({{ParamId::LinkRole, 1.0}, {ParamId::LinkStemKind, 1.0}, {ParamId::LinkGroup, 3.0}, {ParamId::StemMode, 2.0}, {ParamId::StemWidth, 0.0}});
    const auto noise = tools::signals::pinkNoise(48000.0, 1.0, -18.0, 3, true);
    for (int pos = 0; pos + 512 <= 48000; pos += 512) manual.block(noise, pos);
    float side = 0.0f;
    for (int i = 0; i < 512; ++i) side = std::max(side, std::fabs(manual.data.outputs[0].channelBuffers32[0][i] - manual.data.outputs[0].channelBuffers32[1][i]));
    OMNIS_CHECK(side < 1e-6f);
}

namespace {

/// Le contrôleur, vu du processeur : reçoit ses messages.
class Peer : public FObject, public IConnectionPoint {
public:
    std::string text;
    tresult PLUGIN_API connect(IConnectionPoint*) override { return kResultOk; }
    tresult PLUGIN_API disconnect(IConnectionPoint*) override { return kResultOk; }
    tresult PLUGIN_API notify(IMessage* message) override {
        const void* data = nullptr;
        uint32 size = 0;
        if (message && std::string_view(message->getMessageID()) == vst3::kProposalsMessage &&
            message->getAttributes()->getBinary(vst3::kProposalsAttribute, data, size) == kResultTrue)
            text.assign(static_cast<const char*>(data), size);
        return kResultOk;
    }
    OBJ_METHODS(Peer, FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IConnectionPoint)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};

tools::AudioFile stereo(double seconds, const std::function<double(double)>& f) {
    tools::AudioFile a;
    a.sampleRate = 48000.0;
    a.channels.assign(2, std::vector<double>(static_cast<std::size_t>(seconds * 48000.0), 0.0));
    for (std::size_t i = 0; i < a.channels[0].size(); ++i) a.channels[0][i] = a.channels[1][i] = f(static_cast<double>(i) / 48000.0);
    return a;
}

} // namespace

OMNIS_TEST("plug-in chef d'orchestre: le master propose, la personne accepte, le piano est retouché") {
    setLinkNamespace("omnistestcond");
    IPtr<HostApplication> host = owned(new HostApplication());
    Instance master({{ParamId::LinkRole, 2.0}, {ParamId::LinkGroup, 4.0}}, host);
    Instance b808({{ParamId::LinkRole, 1.0}, {ParamId::LinkStemKind, static_cast<double>(stems::Element::Bass808)}, {ParamId::LinkGroup, 4.0}});
    Instance piano({{ParamId::LinkRole, 1.0}, {ParamId::LinkStemKind, static_cast<double>(stems::Element::Keys)}, {ParamId::LinkGroup, 4.0}});
    IPtr<Peer> peer = owned(new Peer());
    master.p->connect(peer);

    constexpr double pi = 3.141592653589793;
    const auto sig808 = stereo(10.0, [&](double t) {
        const double u = std::fmod(t, 1.0);
        return u < 0.9 ? 0.7 * std::tanh(1.5 * std::sin(2 * pi * 45.0 * u)) * std::exp(-u / 1.2) : 0.0;
    });
    // Piano dont la main gauche (98 / 110 Hz) couvre le haut du corps de la 808.
    const auto sigPiano = stereo(10.0, [&](double t) {
        const double f = static_cast<int>(t) % 2 ? 110.0 : 98.0;
        return 0.25 * (0.5 * std::sin(2 * pi * f * t) + 0.17 * std::sin(4 * pi * f * t) + 0.3 * std::sin(2 * pi * 392.0 * t) +
                       0.3 * std::sin(2 * pi * 493.9 * t) + 0.3 * std::sin(2 * pi * 587.3 * t));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // les stems rejoignent le groupe
    for (int pos = 0; pos + 512 <= static_cast<int>(sig808.channels[0].size()); pos += 512) {
        b808.block(sig808, pos);
        piano.block(sigPiano, pos);
        master.block(sig808, pos);
        std::this_thread::sleep_for(std::chrono::microseconds(2000)); // ~5x le temps réel : le master suit
    }
    for (int i = 0; i < 400 && master.lastProposals < 1.0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        master.block(sig808, 0);
    }
    OMNIS_CHECK(master.lastProposals >= 1.0);

    // L'interface demande les propositions, puis accepte la première.
    IPtr<HostMessage> request = owned(new HostMessage());
    request->setMessageID(vst3::kProposalsRequest);
    OMNIS_CHECK(master.p->notify(request) == kResultOk);
    std::printf("      %s", peer->text.c_str());
    OMNIS_CHECK(peer->text.rfind("P\t", 0) == 0);
    const auto tab = peer->text.find('\t', 2);
    const std::uint32_t key = static_cast<std::uint32_t>(std::stoul(peer->text.substr(2, tab - 2)));
    OMNIS_CHECK(peer->text.find("Piano") != std::string::npos);

    const auto lowLevel = [&](int from) {
        double re = 0.0, im = 0.0, reIn = 0.0, imIn = 0.0;
        for (int b = 0; b < 20; ++b) {
            piano.block(sigPiano, from + 512 * b);
            for (int i = 0; i < 512; ++i) {
                const double t = (from + 512 * b + i) / 48000.0;
                const double y = piano.data.outputs[0].channelBuffers32[0][i];
                const double x = sigPiano.channels[0][static_cast<std::size_t>(from + 512 * b + i)];
                re += y * std::cos(2 * pi * 98.0 * t);
                im += y * std::sin(2 * pi * 98.0 * t);
                reIn += x * std::cos(2 * pi * 98.0 * t);
                imIn += x * std::sin(2 * pi * 98.0 * t);
            }
        }
        return db(std::hypot(re, im) / std::hypot(reIn, imIn));
    };
    OMNIS_CHECK(std::fabs(lowLevel(0)) < 0.1); // avant : rien ne bouge sans accord

    IPtr<HostMessage> accept = owned(new HostMessage());
    accept->setMessageID(vst3::kAnswerMessage);
    accept->getAttributes()->setInt(vst3::kAnswerKeyAttribute, key);
    accept->getAttributes()->setInt(vst3::kAnswerAttribute, 0);
    OMNIS_CHECK(master.p->notify(accept) == kResultOk);
    double after = 0.0;
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        after = lowLevel(0);
        if (after < -3.0) break;
    }
    std::printf("      piano à 98 Hz après accord : %.1f dB\n", after);
    OMNIS_CHECK(after < -3.0);
    for (int i = 0; i < 100 && master.lastProposals != 0.0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        master.block(sig808, 0);
    }
    OMNIS_CHECK(master.lastProposals == 0.0);
    master.p->disconnect(peer);
}
