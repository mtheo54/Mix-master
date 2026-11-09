// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestComposition.cpp — Les bus du projet se voient (région partagée,
//  page COMPOSITION, thread du lien).
// =============================================================================
#include "TestFramework.h"

#include "link/LinkBus.h"
#include "link/LinkClient.h"
#include "link/SharedRegion.h"
#include "stems/Composition.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace omnis;
using namespace omnis::stems;

namespace {

/// Une région à part pour le test (jamais celle d'une vraie session), effacée à la fin.
struct Namespace {
    std::string name;
    Namespace() : name("omnistest" + std::to_string(static_cast<unsigned>(std::random_device{}() % 100000)) + "x") { set(name.c_str()); }
    ~Namespace() {
        link::SharedRegion::unlink(link::regionName());
        set(nullptr);
    }
    static void set(const char* v) {
#if defined(_WIN32)
        _putenv_s("OMNIS_LINK_NAMESPACE", v ? v : "");
#else
        if (v) setenv("OMNIS_LINK_NAMESPACE", v, 1);
        else unsetenv("OMNIS_LINK_NAMESPACE");
#endif
    }
};

BusContent content(std::initializer_list<HeardItem> items, double seconds = 30.0) {
    BusContent c;
    c.items = items;
    c.secondsHeard = seconds;
    return c;
}

} // namespace

OMNIS_TEST("composition: chaque bus, son groupe (nom, sinon contenu) et ce qu'il contient, en clair") {
    std::vector<link::InstanceView> v(4);
    v[0] = {0, 1, "EFX", content({{Element::ReverseCymbal, 1.0, 2, 0.0}, {Element::Riser, 0.5, 1, 0.0}})};
    v[1] = {1, 2, "DRUMS", content({{Element::Kick, 1.0, 24, 0.0}, {Element::Snare, 1.0, 23, 0.0}, {Element::HiHat, 1.0, 36, 0.0}})};
    v[2] = {2, 3, "Insert 12", content({{Element::Bass808, 1.0, 9, 15.0}})}; // pas de nom parlant : son contenu
    v[3] = {3, 4, "MELO", content({}, 1.0)};                                    // à peine écouté
    const Composition c = buildComposition(v);
    OMNIS_CHECK(c.buses.size() == 4);
    if (c.buses.size() != 4) return;
    // Dans l'ordre des groupes : DRUMS, 808 / BASSE, MÉLO, EFX.
    OMNIS_CHECK(c.buses[0].name == "DRUMS" && c.buses[0].family == Family::Drums);
    OMNIS_CHECK(c.buses[1].name == "Insert 12" && c.buses[1].family == Family::Bass);
    OMNIS_CHECK(c.buses[2].name == "MELO" && c.buses[2].family == Family::Instrument && c.buses[2].listening());
    OMNIS_CHECK(c.buses[3].name == "EFX" && c.buses[3].family == Family::Fx);
    const std::string text = describeComposition(c);
    std::printf("%s\n", text.c_str());
    OMNIS_CHECK(text.find("BUS « DRUMS » (DRUMS) : Kick (24), Snare / clap (23), Hi-hat (36)") != std::string::npos);
    OMNIS_CHECK(text.find("BUS « Insert 12 » (808 / BASSE) : 808 (tenue 15 s)") != std::string::npos);
    OMNIS_CHECK(text.find("BUS « MELO » (MÉLO) : lance la lecture") != std::string::npos);
    OMNIS_CHECK(text.find("BUS « EFX » (EFX) : Reverse cymbal (2), Riser (1)") != std::string::npos);
    OMNIS_CHECK(describeComposition({}).empty());
}

OMNIS_TEST("composition: la région partagée (bus, noms, contenu) se lit d'une autre instance") {
    Namespace ns;
    {
        link::Observer before;
        OMNIS_CHECK(!before.open() || before.instances().empty());
    }
    link::Endpoint drums, efx;
    OMNIS_CHECK(drums.join("DRUMS") && efx.join("EFX"));
    drums.publish("DRUMS", content({{Element::Kick, 0.9, 24, 0.0}, {Element::OpenHat, 0.3, 4, 0.0}}));
    // Plus d'éléments que la place : les 16 premiers seulement.
    BusContent many;
    for (int i = 0; i < 20; ++i) many.items.push_back({Element::Riser, 0.5, i, 0.0});
    efx.publish("EFX", many);
    link::Observer obs;
    OMNIS_CHECK(obs.open());
    const auto views = obs.instances();
    OMNIS_CHECK(views.size() == 2);
    for (const auto& v : views) {
        if (v.name == "DRUMS") {
            OMNIS_CHECK(v.content.items.size() == 2 && v.content.items[0].element == Element::Kick && v.content.items[0].count == 24);
            OMNIS_CHECK(std::fabs(v.content.items[1].confidence - 0.3) < 1e-6 && v.content.secondsHeard == 30.0);
        } else {
            OMNIS_CHECK(v.name == "EFX" && v.content.items.size() == static_cast<std::size_t>(link::kMaxItems));
        }
    }
    // Nom trop long : coupé, jamais au milieu d'un caractère UTF-8.
    drums.publish("Ébauche très très longue de bus qui dépasse la limite du nom é", {});
    for (const auto& v : obs.instances())
        if (v.slot == drums.slotIndex()) OMNIS_CHECK(v.name.size() < static_cast<std::size_t>(link::kNameLength) && (static_cast<unsigned char>(v.name.back()) & 0xC0u) != 0xC0u);
    drums.leave();
    OMNIS_CHECK(obs.instances().size() == 1);
    efx.leave();
    OMNIS_CHECK(obs.instances().empty());
    obs.close();
}

OMNIS_TEST("lien: thread audio, thread du lien et lecture d'une autre instance en même temps (sans verrou, sans course)") {
    Namespace ns;
    constexpr double kFs = 48000.0;
    // Un bus de kicks : 140 BPM, 6 s en boucle.
    std::vector<float> kick(static_cast<std::size_t>(6 * kFs), 0.0f);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (double t = 0.0; t < 6.0; t += 60.0 / 140.0)
        for (std::size_t k = 0; k < static_cast<std::size_t>(0.3 * kFs); ++k) {
            const double s = static_cast<double>(k) / kFs;
            const auto i = static_cast<std::size_t>(t * kFs) + k;
            if (i < kick.size())
                kick[i] += static_cast<float>(0.7 * std::exp(-s / 0.08) * std::sin(2 * std::numbers::pi * (50.0 * s + 3.0 * (1.0 - std::exp(-s / 0.03)))) +
                                              0.3 * std::exp(-s / 0.004) * u(rng));
        }
    link::LinkClient bus;
    bus.setTrackName("DRUMS");
    bus.start(kFs);
    std::atomic<bool> quit{false};
    std::thread audio([&] {
        for (std::size_t pos = 0; !quit.load(); pos += 512) {
            const float* p = kick.data() + (pos % (kick.size() - 512));
            bus.process(p, p, 512, true);
            if (pos % 8192 == 0) std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    });
    link::Observer obs;
    Composition c;
    const auto heardKick = [&](const char* name) {
        return c.buses.size() == 1 && c.buses[0].name == name && c.buses[0].content.has(Element::Kick);
    };
    for (int i = 0; i < 100 && !heardKick("DRUMS"); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!obs.isOpen()) obs.open();
        if (obs.isOpen()) c = buildComposition(obs.instances());
    }
    OMNIS_CHECK(heardKick("DRUMS"));
    bus.setTrackName("DRUMS 2"); // renommé dans le DAW pendant la lecture
    for (int i = 0; i < 40 && !heardKick("DRUMS 2"); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        c = buildComposition(obs.instances());
    }
    OMNIS_CHECK(heardKick("DRUMS 2"));
    OMNIS_CHECK(bus.content().has(Element::Kick) && bus.slot() >= 0);
    quit.store(true);
    audio.join();
    // Arrêt : l'emplacement est rendu ; relancé : repris.
    bus.stop();
    OMNIS_CHECK(obs.instances().empty());
    bus.start(kFs);
    for (int i = 0; i < 30 && obs.instances().empty(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    OMNIS_CHECK(obs.instances().size() == 1);
    bus.stop();
    obs.close();
}
