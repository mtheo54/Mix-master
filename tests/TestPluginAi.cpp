// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestPluginAi.cpp — La barre IA et AUTO dans le plug-in : le vrai
//  processeur écoute la prod, le vrai contrôleur comprend la demande, règle
//  les modules et mène le volume à la cible par la mesure.
// =============================================================================
#include "TestFramework.h"

#include "ai/ReferenceLibrary.h"

#include "TestSignals.h"
#include "WavFile.h"

#include "params/Parameters.h"
#include "plugin/vst3/OmnisController.h"
#include "plugin/vst3/OmnisProcessor.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
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
    std::vector<std::string> said; ///< messages de pollAutomation

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
                if (!m.empty()) {
                    std::printf("      [OMNIS] %s\n", m.c_str());
                    said.push_back(m);
                }
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

OMNIS_TEST("plug-in AUTO: écoute le morceau entier jusqu'à l'arrêt, règle la chaîne, essaie clip / limiteur et atteint le volume") {
    Host h;
    h.run(1.0, false);
    std::printf("      %s\n", h.ctrl->startAuto().c_str());
    // Le « morceau » : 30 s jouées (un peu plus vite que le temps réel).
    for (int i = 0; i < 30; ++i) {
        h.run(1.0, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    std::printf("      %s\n", h.ctrl->autoStatus().c_str());
    OMNIS_CHECK(h.ctrl->autoProgress() >= 0.0); // la lecture tourne : AUTO écoute encore
    // Lecture arrêtée : l'écoute se termine, puis les essais (en arrière-plan).
    h.context.state = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (h.ctrl->autoProgress() >= 0.0 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(240)) {
        h.run(0.1, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    OMNIS_CHECK(h.ctrl->autoProgress() < 0.0); // terminé
    bool refined = false;
    for (const auto& m : h.said) refined = refined || m.find("AUTO affiné") != std::string::npos;
    OMNIS_CHECK(refined);
    // Relancée : le volume est tenu à l'écoute.
    h.context.state = ProcessContext::kPlaying;
    for (int i = 0; i < 40; ++i) {
        h.run(0.5, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    const double st = h.plain(ParamId::MeterShortTerm);
    std::printf("      clip %.1f dB, limiteur %.1f dB, punch %.0f %%, sortie %.1f LUFS\n", h.plain(ParamId::ClipDrive), h.plain(ParamId::LimGain),
                h.plain(ParamId::LimPunch), st);
    OMNIS_CHECK(h.plain(ParamId::LimPunch) >= 15.0);
    OMNIS_CHECK(h.plain(ParamId::LimGain) + h.plain(ParamId::ClipDrive) > 3.0);
    OMNIS_CHECK(st > -14.5 && st < -4.5);
}

OMNIS_TEST("plug-in AUTO: un second clic pendant l'écoute la termine ; avant le son, il annule") {
    Host h;
    h.run(1.0, false);
    h.context.state = 0; // pas de lecture
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // l'écoute a vidé sa file
    h.ctrl->startAuto();
    h.run(0.5, true);
    OMNIS_CHECK(h.ctrl->autoStatus().find("lance la lecture") != std::string::npos);
    OMNIS_CHECK(h.ctrl->startAuto() == "AUTO annulé.");
    OMNIS_CHECK(h.ctrl->autoProgress() < 0.0);
    // Écoute lancée, puis second clic après 14 s : fin sans attendre l'arrêt.
    h.context.state = ProcessContext::kPlaying;
    h.ctrl->startAuto();
    for (int i = 0; i < 14; ++i) {
        h.run(1.0, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    std::printf("      %s\n", h.ctrl->startAuto().c_str());
    const auto t0 = std::chrono::steady_clock::now();
    while (h.ctrl->autoProgress() >= 0.0 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(240)) {
        h.run(0.1, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    OMNIS_CHECK(h.ctrl->autoProgress() < 0.0);
    OMNIS_CHECK(h.plain(ParamId::LimPunch) >= 15.0);
}

OMNIS_TEST("plug-in AUTO: un son glissé est analysé en entier (calme puis fort), réglé et essayé sans lecture ; un fichier illisible le dit") {
    Host h;
    h.run(0.5, false);
    // Le « morceau » : 10 s calmes (-14 dB) puis 20 s de la boucle, au niveau.
    tools::AudioFile song;
    song.sampleRate = 48000.0;
    const int n = 30 * 48000;
    song.resize(2, n);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
            song.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] =
                (i < 10 * 48000 ? 0.2 : 1.0) * h.signal.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(i % h.signal.numFrames())];
    const auto path = std::filesystem::temp_directory_path() / "omnis_test_auto_prod.wav";
    std::string error;
    OMNIS_CHECK(tools::writeWav(path.string(), song, tools::SampleFormat::Float32, error));
    const auto u8 = path.u8string();
    const std::string said = h.ctrl->autoFromFile(std::string(u8.begin(), u8.end()));
    std::printf("      %s\n", said.c_str());
    OMNIS_CHECK(said.find("omnis_test_auto_prod.wav") != std::string::npos);
    // Pas de lecture : tout se fait sur le fichier.
    h.context.state = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (h.ctrl->autoProgress() >= 0.0 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(240)) {
        h.run(0.1, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    bool refined = false;
    for (const auto& m : h.said) refined = refined || m.find("AUTO affiné") != std::string::npos;
    OMNIS_CHECK(refined);
    OMNIS_CHECK(h.plain(ParamId::LimPunch) >= 15.0);
    OMNIS_CHECK(h.plain(ParamId::LimGain) + h.plain(ParamId::ClipDrive) > 3.0);
    std::filesystem::remove(path);
    // Un fichier qui n'existe pas : dit, rien de changé.
    const double lim = h.plain(ParamId::LimGain);
    h.ctrl->autoFromFile((std::filesystem::temp_directory_path() / "omnis_absent.wav").string());
    for (int i = 0; i < 100 && h.ctrl->autoProgress() >= 0.0; ++i) {
        h.run(0.1, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    OMNIS_CHECK(!h.said.empty() && h.said.back().find("illisible") != std::string::npos);
    OMNIS_CHECK(h.plain(ParamId::LimGain) == lim);
}

OMNIS_TEST("plug-in menu RÉFÉRENCE: styles, artistes, titres de la base ; un titre choisi règle la chaîne et reste avec la session") {
    Host h;
    const auto tree = h.ctrl->referenceTree();
    std::size_t titles = 0;
    for (const auto& a : tree.artists) titles += a.titles.size();
    std::printf("      %zu styles, %zu artistes, %zu titres mesurés\n", tree.styles.size(), tree.artists.size(), titles);
    OMNIS_CHECK(!tree.styles.empty() && !tree.artists.empty() && titles > 0);
    // Le plug-in livre des PROFILS de genre (aucun titre) : style « rage fr » ->
    // « Profil OMNIS » -> le profil « Rap Français & New Wave › rage fr ».
    std::string title;
    for (const auto& st : tree.styles)
        if (st.name == "rage fr")
            for (const auto& a : st.artists)
                if (a.name == ai::kProfileArtist)
                    for (const auto& t : a.titles) title = t.name;
    OMNIS_CHECK(title == std::string(ai::kProfileArtist) + " - Rap Français & New Wave › rage fr");
    for (const auto& a : tree.artists) OMNIS_CHECK(a.name == ai::kProfileArtist); // aucun artiste livré
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
    OMNIS_CHECK(card.name == title && card.titles >= 3 && card.detail.find("titres") != std::string::npos); // un profil : ses titres
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

OMNIS_TEST("plug-in mode promo: « /fbi » bascule les monogrammes, gardé dans le dossier de l'utilisateur") {
    // Un dossier d'utilisateur à part : le vrai n'est pas touché.
    const auto dir = std::filesystem::temp_directory_path() / "omnis_test_promo";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    const std::string dirText = dir.string();
#if defined(_WIN32)
    _putenv_s("OMNIS_USER_DATA_DIR", dirText.c_str());
#else
    setenv("OMNIS_USER_DATA_DIR", dirText.c_str(), 1);
#endif
    {
        Host h;
        OMNIS_CHECK(!h.ctrl->promoMode());
        const std::string on = h.ctrl->ask("  /fbi ");
        std::printf("      %s\n", on.c_str());
        OMNIS_CHECK(h.ctrl->promoMode() && on.find("monogrammes") != std::string::npos);
        OMNIS_CHECK(std::filesystem::exists(dir / "mode_promo.txt"));
        // Une autre instance (session suivante) le retrouve.
        IPtr<vst3::OmnisController> other = owned(new vst3::OmnisController());
        other->initialize(&h.app);
        OMNIS_CHECK(other->promoMode());
        other->terminate();
        OMNIS_CHECK(!h.ctrl->ask("/fbi").empty() && !h.ctrl->promoMode());
        OMNIS_CHECK(!std::filesystem::exists(dir / "mode_promo.txt"));
    }
#if defined(_WIN32)
    _putenv_s("OMNIS_USER_DATA_DIR", "");
#else
    unsetenv("OMNIS_USER_DATA_DIR");
#endif
    std::filesystem::remove_all(dir, ec);
}

OMNIS_TEST("plug-in: « remets tout à zéro » ramène chaque réglage à son départ") {
    Host h;
    const auto id = static_cast<Steinberg::Vst::ParamID>(params::ParamId::ClipDrive);
    const auto* spec = params::findParameter(static_cast<std::uint32_t>(params::ParamId::ClipDrive));
    OMNIS_CHECK(spec != nullptr);
    h.ctrl->setParamNormalized(id, params::toNormalized(*spec, 9.0));
    OMNIS_CHECK(h.ctrl->getParamNormalized(id) > params::toNormalized(*spec, spec->defaultValue) + 1e-6);
    const std::string answer = h.ctrl->ask("remet tout a zero stp");
    std::printf("      %s\n", answer.c_str());
    OMNIS_CHECK(answer.find("départ") != std::string::npos);
    OMNIS_CHECK(std::fabs(h.ctrl->getParamNormalized(id) - params::toNormalized(*spec, spec->defaultValue)) < 1e-9);
}

OMNIS_TEST("plug-in: « tuto », « aide », « comment ça marche » rouvrent le guide ; une demande de son ne le rouvre pas") {
    Host h;
    OMNIS_CHECK(!h.ctrl->takeTutorialRequest());
    for (const char* ask : {"tuto", "Aide", "comment ça marche ?", "relance le tuto stp", "help"}) {
        const std::string answer = h.ctrl->ask(ask);
        OMNIS_CHECK(answer.find("guide") != std::string::npos);
        OMNIS_CHECK(h.ctrl->takeTutorialRequest());
        OMNIS_CHECK(!h.ctrl->takeTutorialRequest()); // pris une seule fois
    }
    (void)h.ctrl->ask("plus de punch, aide moi a garder ma 808");
    OMNIS_CHECK(!h.ctrl->takeTutorialRequest());
}

OMNIS_TEST("plug-in barre IA: « ça siffle » allume une bande de l'EQ dynamique (7 kHz, centre) ; redit, elle se creuse ; « annule » la rend") {
    Host h;
    const auto plainOf = [&](params::ParamId pid) {
        const auto* spec = params::findParameter(pid);
        return params::toPlain(*spec, h.ctrl->getParamNormalized(static_cast<Steinberg::Vst::ParamID>(pid)));
    };
    using params::DynControl;
    OMNIS_CHECK(plainOf(params::dynId(3, DynControl::On)) < 0.5);
    (void)h.ctrl->ask("les voix sifflent, ça siffle trop"); // gardée jusqu'à ce que la prod soit entendue
    h.run(16.0, true);
    for (int i = 0; i < 80 && plainOf(params::dynId(3, DynControl::On)) < 0.5; ++i) {
        h.run(0.25, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    OMNIS_CHECK(plainOf(params::dynId(3, DynControl::On)) >= 0.5);
    OMNIS_CHECK_NEAR(plainOf(params::dynId(3, DynControl::Freq)), 7000.0, 1.0);
    OMNIS_CHECK_NEAR(plainOf(params::dynId(3, DynControl::Range)), -6.0, 0.01);
    OMNIS_CHECK_NEAR(plainOf(params::dynId(3, DynControl::Channel)), 1.0, 1e-9); // Mid
    const std::string again = h.ctrl->ask("ça siffle encore");
    std::printf("      %s\n", again.c_str());
    OMNIS_CHECK(again.find("EQ dynamique") != std::string::npos);
    OMNIS_CHECK_NEAR(plainOf(params::dynId(3, DynControl::Range)), -8.0, 0.01);
    // Les autres bandes n'ont pas bougé.
    OMNIS_CHECK(plainOf(params::dynId(0, DynControl::On)) < 0.5);
    OMNIS_CHECK(plainOf(params::dynId(1, DynControl::On)) < 0.5);
    (void)h.ctrl->ask("annule");
    OMNIS_CHECK_NEAR(plainOf(params::dynId(3, DynControl::Range)), -6.0, 0.01);
    // Une demande sans plainte de ce genre ne touche pas l'EQ dynamique.
    Host g;
    (void)g.ctrl->ask("plus de punch");
    OMNIS_CHECK(params::toPlain(*params::findParameter(params::dynId(2, DynControl::On)),
                                g.ctrl->getParamNormalized(static_cast<Steinberg::Vst::ParamID>(params::dynId(2, DynControl::On)))) < 0.5);
}

OMNIS_TEST("plug-in barre IA: « saturation à lampe », « bande », « transistor » choisissent le caractère de l'Inflator ; « ça tape » reste du punch") {
    Host h;
    h.run(4.0, true);
    const auto character = [&h] { return static_cast<int>(std::lround(h.plain(ParamId::InflCharacter))); };
    OMNIS_CHECK(character() == 0); // Inflate par défaut
    (void)h.ctrl->ask("mets une saturation à lampe"); // gardée jusqu'à ce que la prod soit entendue
    for (int i = 0; i < 80 && character() != 2; ++i) {
        h.run(0.25, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    std::string a;
    for (const auto& m : h.said) a += m;
    OMNIS_CHECK(character() == 2 && a.find("Lampe") != std::string::npos);
    OMNIS_CHECK(h.plain(ParamId::InflHighEffect) >= 20.0);
    a = h.ctrl->ask("plutôt un effet bande");
    std::printf("      %s\n", a.c_str());
    OMNIS_CHECK(character() == 1);
    a = h.ctrl->ask("du transistor");
    OMNIS_CHECK(character() == 3);
    a = h.ctrl->ask("ça tape pas assez");
    OMNIS_CHECK(character() == 3); // « tape » (ça frappe) ne touche pas au caractère
    OMNIS_CHECK(a.find("Punch") != std::string::npos);
}

OMNIS_TEST("plug-in: « annule » défait la dernière action (ici la remise à zéro), pas à pas") {
    Host h;
    const auto id = static_cast<Steinberg::Vst::ParamID>(params::ParamId::ClipDrive);
    const auto* spec = params::findParameter(static_cast<std::uint32_t>(params::ParamId::ClipDrive));
    OMNIS_CHECK(h.ctrl->ask("annule").find("Rien à annuler") != std::string::npos);
    h.ctrl->setParamNormalized(id, params::toNormalized(*spec, 9.0));
    h.ctrl->ask("remets tout a zero");
    OMNIS_CHECK(std::fabs(params::toPlain(*spec, h.ctrl->getParamNormalized(id)) - spec->defaultValue) < 1e-6);
    const std::string back = h.ctrl->ask("annule");
    std::printf("      %s\n", back.c_str());
    OMNIS_CHECK(back.find("Annulé") != std::string::npos);
    OMNIS_CHECK(std::fabs(params::toPlain(*spec, h.ctrl->getParamNormalized(id)) - 9.0) < 1e-6);
}

OMNIS_TEST("plug-in: prêt à exporter ? les outils d'écoute oubliés sont signalés") {
    Host h;
    OMNIS_CHECK(h.ctrl->exportProblems().empty());
    const auto set = [&](params::ParamId pid, double plain) {
        const auto* spec = params::findParameter(static_cast<std::uint32_t>(pid));
        h.ctrl->setParamNormalized(static_cast<Steinberg::Vst::ParamID>(pid), params::toNormalized(*spec, plain));
    };
    set(params::ParamId::LowLatency, 1.0);
    set(params::ParamId::Monitor, 2.0);
    const auto problems = h.ctrl->exportProblems();
    OMNIS_CHECK(problems.size() == 2);
    OMNIS_CHECK(problems.front().find("SIDE") != std::string::npos); // le plus grave d'abord
    const std::string answer = h.ctrl->ask("je peux exporter ?");
    std::printf("      %s\n", answer.c_str());
    OMNIS_CHECK(answer.find("PAS ENCORE PRÊT") != std::string::npos);
    set(params::ParamId::LowLatency, 0.0);
    set(params::ParamId::Monitor, 0.0);
    OMNIS_CHECK(h.ctrl->ask("pret a exporter").find("PRÊT À EXPORTER") == 0);
}

OMNIS_TEST("plug-in: filtre du menu RÉF par famille (genres_classement) : seuls ses artistes restent") {
    Host h;
    const auto families = h.ctrl->referenceFamilies();
    OMNIS_CHECK(!families.empty());
    const auto count = [](const vst3::OmnisController::ReferenceTree& t) {
        std::size_t n = 0;
        for (const auto& a : t.artists) n += a.titles.size();
        return n;
    };
    const auto all = h.ctrl->referenceTree();
    std::size_t biggest = 0;
    for (const auto& f : families) {
        const auto t = h.ctrl->referenceTree(f);
        std::printf("      %-38s %3zu profil(s), %2zu style(s)\n", f.c_str(), count(t), t.styles.size());
        OMNIS_CHECK(count(t) <= count(all));
        biggest = std::max(biggest, count(t));
    }
    OMNIS_CHECK(biggest > 0 && biggest < count(all)); // le filtre retire bien quelque chose
}

OMNIS_TEST("plug-in: filtre du menu RÉF par sous-genre (styles_artistes) : famille › style") {
    Host h;
    const auto filters = h.ctrl->referenceFilters();
    OMNIS_CHECK(!filters.empty());
    std::size_t withStyles = 0;
    for (const auto& f : filters) {
        const auto family = h.ctrl->referenceTree(f.name);
        if (!f.styles.empty()) ++withStyles;
        std::printf("      %-34s %4d titre(s), %2zu sous-genre(s)\n", f.name.c_str(), f.titles, f.styles.size());
        for (const auto& [style, count] : f.styles) {
            OMNIS_CHECK(count > 0);
            const auto t = h.ctrl->referenceTree(f.name, style);
            OMNIS_CHECK(!t.artists.empty());
            OMNIS_CHECK(t.artists.size() <= family.artists.size()); // le sous-genre restreint la famille
        }
    }
    std::printf("      %zu famille(s), dont %zu avec sous-genres\n", filters.size(), withStyles);
    OMNIS_CHECK(withStyles > 0);
}

OMNIS_TEST("plug-in: « Ajouter un fichier de référence » — reconnu par son nom, mesuré en arrière-plan, visible dans le menu RÉF") {
    const auto dir = std::filesystem::temp_directory_path() / "omnis_test_import";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    const std::string dirText = dir.string();
#if defined(_WIN32)
    _putenv_s("OMNIS_USER_DATA_DIR", dirText.c_str());
#else
    setenv("OMNIS_USER_DATA_DIR", dirText.c_str(), 1);
#endif
    {
        // Un son nommé « Titre - Artiste » (SLAYR est connu : remis dans l'ordre).
        tools::AudioFile f;
        f.sampleRate = 48000.0;
        f.resize(2, 8 * 48000);
        for (int i = 0; i < f.numFrames(); ++i)
            f.channels[0][static_cast<std::size_t>(i)] = f.channels[1][static_cast<std::size_t>(i)] = 0.3 * std::sin(2.0 * 3.141592653589793 * 49.0 * i / 48000.0);
        std::string error;
        const auto file = dir / "Sloppy Joe - SLAYR.wav";
        OMNIS_CHECK(tools::writeWav(file.string(), f, tools::SampleFormat::Pcm24, error));
        Host h;
        h.ctrl->setOnlineRecognition(false); // aucun réseau pendant les tests
        OMNIS_CHECK(!h.ctrl->onlineRecognition());
        const std::string started = h.ctrl->importReferenceFile(file.string());
        std::printf("      %s\n", started.c_str());
        std::string done;
        for (int i = 0; i < 200 && done.empty(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            done = h.ctrl->pollAutomation();
        }
        std::printf("      %s\n", done.c_str());
        OMNIS_CHECK(done.find("Ajouté à ta base : SLAYR - Sloppy Joe") == 0);
        OMNIS_CHECK(h.ctrl->takeLibraryChanged() && !h.ctrl->takeLibraryChanged());
        OMNIS_CHECK(std::filesystem::exists(dir / "references_utilisateur.txt"));
        bool listed = false;
        for (const auto& a : h.ctrl->referenceTree().artists)
            if (a.name == "SLAYR")
                for (const auto& t : a.titles) listed = listed || t.title == "Sloppy Joe";
        OMNIS_CHECK(listed);
        // Le choix « MusicBrainz coupé » est gardé (une autre instance le relit).
        IPtr<vst3::OmnisController> other = owned(new vst3::OmnisController());
        other->initialize(&h.app);
        OMNIS_CHECK(!other->onlineRecognition());
        other->terminate();
    }
#if defined(_WIN32)
    _putenv_s("OMNIS_USER_DATA_DIR", "");
#else
    unsetenv("OMNIS_USER_DATA_DIR");
#endif
    std::filesystem::remove_all(dir, ec);
}
