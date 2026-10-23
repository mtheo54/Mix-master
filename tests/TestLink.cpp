// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestLink.cpp — Instances liées : master et stems d'un même groupe.
//
//  Chaque Endpoint ouvre SA vue de la région : même dans un seul processus,
//  tout passe par la mémoire partagée, comme entre deux processus.
// =============================================================================
#include "TestFramework.h"

#include "TestSignals.h"

#include "link/LinkBus.h"

#include <cmath>
#include <cstdlib>
#include <string>

using namespace omnis;
using namespace omnis::link;

namespace {

/// Espace de noms propre au test : jamais la région d'une vraie session.
struct TestNamespace {
    explicit TestNamespace(const char* name) {
#ifdef _WIN32
        _putenv_s("OMNIS_LINK_NAMESPACE", name);
#else
        setenv("OMNIS_LINK_NAMESPACE", name, 1);
#endif
        for (int g = 1; g <= kGroups; ++g) SharedRegion::unlink(regionName(g));
    }
    ~TestNamespace() {
        for (int g = 1; g <= kGroups; ++g) SharedRegion::unlink(regionName(g));
    }
};

} // namespace

OMNIS_TEST("lien: un master voit ses stems (rôle, type, nom), pas ceux d'un autre groupe") {
    TestNamespace ns("omnistesta");
    Endpoint master, kick, vocal, elsewhere;
    OMNIS_CHECK(master.join(1, Role::Master, StemKind::Other, "Master", 48000.0));
    OMNIS_CHECK(kick.join(1, Role::Stem, StemKind::Kick, "Kick principal", 48000.0));
    OMNIS_CHECK(vocal.join(1, Role::Stem, StemKind::Vocal, "Voix", 48000.0));
    OMNIS_CHECK(elsewhere.join(2, Role::Stem, StemKind::Bass808, "808 autre projet", 48000.0));
    OMNIS_CHECK(kick.slotIndex() != vocal.slotIndex());

    const auto stems = master.stems();
    OMNIS_CHECK(stems.size() == 2);
    bool sawKick = false;
    for (const auto& s : stems)
        if (s.kind == StemKind::Kick) {
            sawKick = true;
            OMNIS_CHECK(s.name == "Kick principal");
            OMNIS_CHECK_NEAR(s.sampleRate, 48000.0, 1e-9);
        }
    OMNIS_CHECK(sawKick);
    OMNIS_CHECK(master.liveMasters() == 1);

    // Un stem qui part libère sa place ; le type peut changer en route.
    vocal.leave();
    kick.setKind(StemKind::DrumLoop);
    const auto after = master.stems();
    OMNIS_CHECK(after.size() == 1 && after[0].kind == StemKind::DrumLoop);
}

OMNIS_TEST("lien: le master écoute l'audio d'un stem pendant la lecture (même signature que le fichier)") {
    TestNamespace ns("omnistestb");
    Endpoint master, stem;
    OMNIS_CHECK(master.join(3, Role::Master, StemKind::Other, "Master", 48000.0));
    OMNIS_CHECK(stem.join(3, Role::Stem, StemKind::Bass808, "808", 48000.0));
    const auto noise = tools::signals::pinkNoise(48000.0, 6.0, -18.0, 11, true);
    const auto& l = noise.channels[0];
    const auto& r = noise.channels[1];
    const int n = static_cast<int>(l.size());

    StemListener listener;
    // 1 s à l'arrêt (rien d'écouté), puis lecture : blocs de 512, le master lit toutes les ~50 ms.
    std::int64_t position = 0;
    for (int pos = 0; pos + 512 <= n; pos += 512) {
        const bool playing = pos >= 48000;
        stem.writeAudio(l.data() + pos, r.data() + pos, 512, playing ? position : kNoPosition, playing);
        if (playing) position += 512;
        if ((pos / 512) % 5 == 0) listener.poll(master);
    }
    listener.poll(master);
    OMNIS_CHECK(listener.heard().size() == 1);
    const auto heard = listener.heard().front();
    OMNIS_CHECK(heard.view.kind == StemKind::Bass808 && heard.view.playing);
    OMNIS_CHECK_NEAR(heard.secondsHeard, 5.0, 0.02); // la seconde à l'arrêt n'est pas écoutée
    OMNIS_CHECK(heard.lastProjectPosition == position);
    OMNIS_CHECK(listener.overruns() == 0);
    // Arrêt du transport : ce qui suit n'est plus écouté.
    for (int b = 0; b < 40; ++b) stem.writeAudio(l.data(), r.data(), 512, kNoPosition, false);
    listener.poll(master);
    OMNIS_CHECK_NEAR(listener.heard().front().secondsHeard, 5.0, 0.02);
    OMNIS_CHECK(!listener.heard().front().view.playing);

    // Même balance que l'analyse directe des mêmes 5 s (aux arrondis float près).
    const auto direct = analysis::analyzeSignature(l.data() + 48000, r.data() + 48000, (n - 48000) / 512 * 512, 48000.0);
    for (int k = 2; k < params::kMatchBands - 1; ++k)
        OMNIS_CHECK_NEAR(heard.signature.relativeMidDb(k), direct.relativeMidDb(k), 0.5);
}

OMNIS_TEST("lien: ordres du master -> stem (jeton vérifié), reprise d'un emplacement abandonné") {
    TestNamespace ns("omnistestc");
    Endpoint master, stem;
    OMNIS_CHECK(master.join(4, Role::Master, StemKind::Other, "Master", 44100.0));
    OMNIS_CHECK(stem.join(4, Role::Stem, StemKind::Lead, "Mélodie", 44100.0));
    OMNIS_CHECK(stem.readOrders().version == 0);
    const auto s = master.stems().front();
    OMNIS_CHECK(master.writeOrders(s.slot, s.owner, {-3.0f, 0}));
    const auto o = stem.readOrders();
    OMNIS_CHECK_NEAR(o.gainDb, -3.0, 1e-6);
    OMNIS_CHECK(o.version == 1);
    OMNIS_CHECK(!master.writeOrders(s.slot, s.owner ^ 1u, {6.0f, 0})); // ce n'est plus lui
    OMNIS_CHECK_NEAR(stem.readOrders().gainDb, -3.0, 1e-6);

    // Hôte planté : le cœur ne bat plus. L'emplacement est repris par la
    // prochaine instance, et l'ancienne n'y touche plus.
    Endpoint ghost;
    OMNIS_CHECK(ghost.join(4, Role::Stem, StemKind::Kick, "Fantôme", 44100.0));
    const int ghostSlot = ghost.slotIndex();
    master.header()->slots[ghostSlot].heartbeatMs.store(nowMs() - 2 * kStaleMs);
    OMNIS_CHECK(master.stems().size() == 1);
    Endpoint next;
    OMNIS_CHECK(next.join(4, Role::Stem, StemKind::Vocal, "Voix", 44100.0));
    OMNIS_CHECK(next.slotIndex() == ghostSlot);
    ghost.heartbeat();
    ghost.leave(); // ne libère pas l'emplacement de « next »
    const auto stems = master.stems();
    OMNIS_CHECK(stems.size() == 2);
    bool vocal = false;
    for (const auto& v : stems) vocal = vocal || v.kind == StemKind::Vocal;
    OMNIS_CHECK(vocal);
}

#include "link/LinkClient.h"

#include <atomic>
#include <chrono>
#include <thread>

OMNIS_TEST("lien: stem qui écrit et master qui écoute en même temps ; changement de groupe en pleine lecture") {
    TestNamespace ns("omnistestd");
    LinkClient master, stem;
    master.start(Role::Master, StemKind::Other, 6, 48000.0);
    stem.start(Role::Stem, StemKind::Vocal, 6, 48000.0);
    const auto noise = tools::signals::pinkNoise(48000.0, 1.0, -18.0, 5, true);
    std::atomic<bool> stop{false};
    std::atomic<long long> blocks{0};
    // Le « thread audio » du stem : écrit sans arrêt, ~4x le temps réel.
    std::thread audio([&] {
        std::int64_t pos = 0;
        while (!stop.load()) {
            {
                LinkClient::RtAccess access(stem);
                if (Endpoint* e = access.endpoint()) {
                    const auto k = static_cast<std::size_t>(pos % (48000 - 512));
                    e->writeAudio(noise.channels[0].data() + k, noise.channels[1].data() + k, 512, pos, true);
                    (void)e->readOrders();
                }
            }
            pos += 512;
            blocks.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::microseconds(2500));
        }
    });
    const auto waitFor = [](auto condition) {
        for (int i = 0; i < 400 && !condition(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return condition();
    };
    OMNIS_CHECK(waitFor([&] { return master.linkedStems() == 1; }));
    OMNIS_CHECK(waitFor([&] {
        const auto h = master.heard();
        return !h.empty() && h.front().secondsHeard > 0.5;
    }));
    // La piste passe dans un autre groupe pendant que son audio tourne.
    stem.setGroup(7);
    OMNIS_CHECK(waitFor([&] { return master.linkedStems() == 0; }));
    stem.setGroup(6);
    OMNIS_CHECK(waitFor([&] { return master.linkedStems() == 1; }));
    stop.store(true);
    audio.join();
    OMNIS_CHECK(blocks.load() > 100);
    stem.stop();
    OMNIS_CHECK(waitFor([&] { return master.linkedStems() == 0; }));
}

OMNIS_TEST("lien: un stem se reconnaît (nom de piste, puis écoute) et le master le voit") {
    TestNamespace ns("omnisteste");
    LinkClient master, stem;
    master.start(Role::Master, StemKind::Other, 7, 48000.0);
    stem.start(Role::Stem, StemKind::Auto, 7, 48000.0);
    stem.setTrackName("PL_ClosedHat_12");
    const auto waitFor = [](auto condition, int tries = 400) {
        for (int i = 0; i < tries && !condition(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return condition();
    };
    // Sans rien écouter, le nom suffit à un premier avis.
    OMNIS_CHECK(waitFor([&] { return stem.identifiedElement() == StemKind::HiHat; }));
    // Le master voit l'élément et le nom de la piste.
    OMNIS_CHECK(waitFor([&] {
        for (const auto& h : master.heard())
            if (h.view.kind == StemKind::HiHat && h.view.name == "PL_ClosedHat_12") return true;
        return false;
    }));
    // La personne choisit l'élément : il passe avant tout.
    stem.setKind(StemKind::Snare);
    OMNIS_CHECK(waitFor([&] { return stem.identifiedElement() == StemKind::Snare; }));
}
