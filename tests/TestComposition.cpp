// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestComposition.cpp — Qui est dans quel bus (empreintes, région partagée).
// =============================================================================
#include "TestFramework.h"

#include "core/UserDataDir.h"
#include "link/BandFrames.h"
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

constexpr double kFs = 48000.0;
constexpr double kPi = std::numbers::pi;

using Mono = std::vector<double>;

/// Un projet synthétique : 140 BPM, chaque piste joue sa partie (le kick et la 808 tombent ensemble).
struct Project {
    Mono kick, snare, hats, bass808, pad, lead, reverse;
    explicit Project(double seconds) {
        const std::size_t n = static_cast<std::size_t>(seconds * kFs);
        for (Mono* m : {&kick, &snare, &hats, &bass808, &pad, &lead, &reverse}) m->assign(n, 0.0);
        std::mt19937 rng(11);
        std::uniform_real_distribution<double> u(-1.0, 1.0);
        const double beat = 60.0 / 140.0;
        double prev = 0.0;
        for (double t = 0.0; t < seconds; t += beat / 2.0) {
            const auto at = static_cast<std::size_t>(t * kFs);
            const int step = static_cast<int>(std::lround(t / (beat / 2.0))) % 8;
            for (std::size_t k = 0; k < static_cast<std::size_t>(0.4 * kFs) && at + k < n; ++k) {
                const double s = static_cast<double>(k) / kFs;
                if (step == 0 || step == 5) {
                    kick[at + k] += 0.7 * std::exp(-s / 0.12) * std::sin(2 * kPi * (50.0 * s + 100.0 * 0.03 * (1.0 - std::exp(-s / 0.03))));
                    bass808[at + k] += 0.5 * std::min(1.0, s / 0.005) * std::sin(2 * kPi * 49.0 * s) * std::exp(-s / 0.6);
                }
                if ((step == 2 || step == 6) && s < 0.25) snare[at + k] += 0.4 * std::exp(-s / 0.08) * u(rng) + 0.2 * std::exp(-s / 0.05) * std::sin(2 * kPi * 190.0 * s);
                if (s < 0.06) {
                    const double x = u(rng);
                    hats[at + k] += 0.15 * std::exp(-s / 0.015) * (x - prev);
                    prev = x;
                }
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / kFs;
            for (double f : {220.0, 277.2, 329.6}) pad[i] += 0.06 * std::sin(2 * kPi * f * t) * (0.7 + 0.3 * std::sin(2 * kPi * 0.25 * t));
            // Lead : une note par croche, 880 / 660 Hz.
            const double note = std::fmod(t, beat / 2.0);
            lead[i] = 0.12 * std::exp(-note / 0.15) * std::sin(2 * kPi * (static_cast<int>(t / (beat / 2.0)) % 2 ? 880.0 : 660.0) * t);
            // Reverse cymbal : monte sur 1,5 s, coupée net, toutes les 4 mesures (~6,9 s).
            const double cycle = std::fmod(t, 16 * beat), start = 16 * beat - 1.5;
            if (cycle >= start) reverse[i] = 0.3 * std::pow((cycle - start) / 1.5, 3.0) * u(rng);
        }
    }
};

/// L'empreinte d'un signal, comme le thread audio d'une instance la publie (blocs de 512).
std::vector<link::BandFrame> footprint(const Mono& m) {
    link::BandFrames bf;
    bf.prepare(kFs);
    std::vector<link::BandFrame> out;
    for (std::size_t pos = 0; pos < m.size(); pos += 512) {
        const int n = static_cast<int>(std::min<std::size_t>(512, m.size() - pos));
        bf.process(m.data() + pos, m.data() + pos, n, static_cast<std::int64_t>(pos), [&](std::int64_t k, const std::array<float, link::kBands>& e) {
            out.push_back({k, e});
        });
    }
    return out;
}

Mono mix(std::initializer_list<std::pair<const Mono*, double>> parts) {
    Mono out(parts.begin()->first->size(), 0.0);
    for (const auto& [m, g] : parts)
        for (std::size_t i = 0; i < out.size(); ++i) out[i] += g * (*m)[i];
    return out;
}

Heard track(int id, const char* name, Element element, const Mono& m) {
    Heard h;
    h.id = id;
    h.role = link::Role::Track;
    h.name = name;
    h.identity.element = element;
    h.identity.heard = element;
    h.identity.confidence = 0.8f;
    h.identity.secondsHeard = 10.0f;
    h.frames = footprint(m);
    return h;
}

Heard bus(int id, const char* name, const Mono& m) {
    Heard h;
    h.id = id;
    h.role = link::Role::Bus;
    h.name = name;
    h.frames = footprint(m);
    return h;
}

const BusView* findBus(const Composition& c, const std::string& name) {
    for (const auto& b : c.buses)
        if (b.name == name) return &b;
    return nullptr;
}

bool hasMember(const BusView& b, const std::string& name) {
    for (const auto& m : b.members)
        if (m.name == name) return true;
    return false;
}

} // namespace

OMNIS_TEST("composition: empreinte par bandes alignée sur le projet, trames entamées jetées") {
    link::BandFrames bf;
    bf.prepare(kFs);
    Mono tone(static_cast<std::size_t>(kFs), 0.0);
    for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = 0.5 * std::sin(2 * kPi * 1000.0 * static_cast<double>(i) / kFs);
    std::vector<link::BandFrame> frames;
    // Départ au milieu d'une trame (position 1000) : la première trame entamée n'est pas publiée.
    for (std::size_t pos = 0; pos < tone.size(); pos += 300) {
        const int n = static_cast<int>(std::min<std::size_t>(300, tone.size() - pos));
        bf.process(tone.data() + pos, tone.data() + pos, n, static_cast<std::int64_t>(1000 + pos),
                   [&](std::int64_t k, const std::array<float, link::kBands>& e) { frames.push_back({k, e}); });
    }
    OMNIS_CHECK(!frames.empty() && frames.front().index == 1); // la trame 0 était entamée
    for (std::size_t i = 1; i < frames.size(); ++i) OMNIS_CHECK(frames[i].index == frames[i - 1].index + 1);
    // 1 kHz : dans la bande 400–2000 Hz.
    const auto& e = frames[frames.size() / 2].energy;
    OMNIS_CHECK(e[3] > 10.0f * e[1] && e[3] > 10.0f * e[5]);
    // Transport arrêté : rien.
    std::size_t before = frames.size();
    bf.process(tone.data(), tone.data(), 4096, link::kNoPosition, [&](std::int64_t k, const std::array<float, link::kBands>& en) { frames.push_back({k, en}); });
    OMNIS_CHECK(frames.size() == before);
}

OMNIS_TEST("composition: chaque piste retrouvée dans son bus (DRUMS, 808, MÉLO, EFX), le kick et la 808 qui tombent ensemble ne se confondent pas") {
    const Project p(14.0);
    std::vector<Heard> all;
    all.push_back(track(1, "Kick Jayy 03", Element::Kick, p.kick));
    all.push_back(track(2, "SNR Crack", Element::Snare, p.snare));
    all.push_back(track(3, "PL_ClosedHat_12", Element::HiHat, p.hats));
    all.push_back(track(4, "808 Mafia", Element::Bass808, p.bass808));
    all.push_back(track(5, "Pad Dreamy", Element::Pad, p.pad));
    all.push_back(track(6, "Serum Lead", Element::Lead, p.lead));
    all.push_back(track(7, "Rev Crash 03", Element::ReverseCymbal, p.reverse));
    // Les bus reçoivent leurs pistes (avec leurs volumes de mixer).
    all.push_back(bus(20, "DRUMS", mix({{&p.kick, 0.9}, {&p.snare, 1.1}, {&p.hats, 0.7}})));
    all.push_back(bus(21, "808", mix({{&p.bass808, 1.0}})));
    all.push_back(bus(22, "MELO", mix({{&p.pad, 0.8}, {&p.lead, 1.2}})));
    all.push_back(bus(23, "EFX", mix({{&p.reverse, 0.6}})));
    const Composition c = buildComposition(all);
    std::printf("%s\n", describeComposition(c).c_str());
    const auto* drums = findBus(c, "DRUMS");
    const auto* b808 = findBus(c, "808");
    const auto* melo = findBus(c, "MELO");
    const auto* efx = findBus(c, "EFX");
    OMNIS_CHECK(drums && b808 && melo && efx);
    if (!drums || !b808 || !melo || !efx) return;
    OMNIS_CHECK(drums->members.size() == 3 && hasMember(*drums, "Kick Jayy 03") && hasMember(*drums, "SNR Crack") && hasMember(*drums, "PL_ClosedHat_12"));
    OMNIS_CHECK(b808->members.size() == 1 && hasMember(*b808, "808 Mafia"));
    OMNIS_CHECK(melo->members.size() == 2 && hasMember(*melo, "Pad Dreamy") && hasMember(*melo, "Serum Lead"));
    OMNIS_CHECK(efx->members.size() == 1 && hasMember(*efx, "Rev Crash 03"));
    OMNIS_CHECK(drums->family == Family::Drums && b808->family == Family::Bass && melo->family == Family::Instrument && efx->family == Family::Fx);
    OMNIS_CHECK(c.direct.empty());
    for (const auto* b : {drums, b808, melo, efx}) OMNIS_CHECK(b->explained > 0.9);
    // Le texte donne le nom d'origine et ce qu'OMNIS devine.
    const std::string text = describeComposition(c);
    OMNIS_CHECK(text.find("« Rev Crash 03 » → Reverse cymbal (EFX") != std::string::npos);
    OMNIS_CHECK(text.find("« Serum Lead » → Lead / synthé (MÉLO") != std::string::npos);
}

OMNIS_TEST("composition: une piste hors bus va « vers le master » ; un bus sans pistes OMNIS dit ce qu'il entend") {
    const Project p(12.0);
    std::vector<Heard> all;
    all.push_back(track(1, "Kick", Element::Kick, p.kick));
    all.push_back(track(2, "Lead", Element::Lead, p.lead));
    Heard drums = bus(10, "Drum Bus", mix({{&p.kick, 1.0}, {&p.snare, 1.0}, {&p.hats, 1.0}}));
    drums.identity.kick = drums.identity.snare = drums.identity.hat = true;
    all.push_back(drums);
    Heard fx = bus(11, "FX", mix({{&p.reverse, 1.0}}));
    fx.identity.element = Element::ReverseCymbal;
    all.push_back(fx);
    const Composition c = buildComposition(all);
    const auto* d = findBus(c, "Drum Bus");
    const auto* f = findBus(c, "FX");
    OMNIS_CHECK(d && f);
    if (!d || !f) return;
    OMNIS_CHECK(d->members.size() == 1 && hasMember(*d, "Kick"));
    OMNIS_CHECK(f->members.empty() && f->family == Family::Fx && f->enoughToMatch);
    OMNIS_CHECK(c.direct.size() == 1 && c.direct[0].name == "Lead");
    const std::string text = describeComposition(c);
    std::printf("%s\n", text.c_str());
    OMNIS_CHECK(text.find("entendu dans le bus : Kick, Snare / clap, Hi-hat") != std::string::npos);
    OMNIS_CHECK(text.find("entendu dans le bus : Reverse cymbal") != std::string::npos);
    OMNIS_CHECK(text.find("HORS BUS") != std::string::npos);
}

OMNIS_TEST("composition: bus légèrement saturé, décalé, avec une piste sans OMNIS : les bonnes pistes quand même") {
    const Project p(16.0);
    std::vector<Heard> all;
    all.push_back(track(1, "Kick", Element::Kick, p.kick));
    all.push_back(track(2, "Hats", Element::HiHat, p.hats));
    all.push_back(track(3, "808", Element::Bass808, p.bass808));
    all.push_back(track(4, "Lead", Element::Lead, p.lead));
    all.push_back(track(5, "Pad", Element::Pad, p.pad));
    // DRUMS = kick + hats + une snare SANS OMNIS, un peu saturé (tanh : un plug-in avant OMNIS sur le bus),
    // et 300 échantillons de retard (latence mal compensée). OMNIS BUS se met en premier sur le bus : ce qui
    // compresse le bus après lui ne change rien à ce qu'il entend.
    Mono d = mix({{&p.kick, 1.0}, {&p.hats, 1.0}, {&p.snare, 1.0}});
    for (double& v : d) v = std::tanh(v);
    Mono delayed(d.size(), 0.0);
    std::copy(d.begin(), d.end() - 300, delayed.begin() + 300);
    all.push_back(bus(10, "DRUMS", delayed));
    all.push_back(bus(11, "MELO", mix({{&p.lead, 1.0}, {&p.pad, 0.5}})));
    const Composition c = buildComposition(all);
    std::printf("%s\n", describeComposition(c).c_str());
    const auto* drums = findBus(c, "DRUMS");
    const auto* melo = findBus(c, "MELO");
    OMNIS_CHECK(drums && melo);
    if (!drums || !melo) return;
    OMNIS_CHECK(hasMember(*drums, "Kick") && hasMember(*drums, "Hats"));
    OMNIS_CHECK(!hasMember(*drums, "808") && !hasMember(*drums, "Lead") && !hasMember(*drums, "Pad"));
    OMNIS_CHECK(melo->members.size() == 2 && hasMember(*melo, "Lead") && hasMember(*melo, "Pad"));
    OMNIS_CHECK(c.direct.size() == 1 && c.direct[0].name == "808");
}

OMNIS_TEST("composition: gros projet (40 pistes, 6 bus) relu vite (thread de l'interface)") {
    const Project p(14.0);
    const Mono* parts[] = {&p.kick, &p.snare, &p.hats, &p.bass808, &p.pad, &p.lead, &p.reverse};
    std::vector<Heard> all;
    std::vector<Mono> buses(6, Mono(p.kick.size(), 0.0));
    for (int t = 0; t < 40; ++t) {
        // Chaque piste : une partie, décalée de quelques croches (son propre rythme), son propre mouvement
        // de volume, envoyée à un bus. (Le même son aux mêmes instants dans deux bus ne se distingue pas.)
        const Mono& src = *parts[t % 7];
        const std::size_t shift = static_cast<std::size_t>((t / 7) * 3 * (60.0 / 140.0 / 2.0) * kFs) % src.size();
        Mono m(src.size(), 0.0);
        for (std::size_t k = 0; k < m.size(); ++k) m[k] = src[(k + shift) % src.size()];
        // Et ses mesures de silence (une nappe continue ne se distingue sinon que par son volume).
        const double bar = 4 * 60.0 / 140.0;
        for (std::size_t k = 0; k < m.size(); ++k)
            if ((static_cast<int>(static_cast<double>(k) / kFs / bar) + t) % 3 == 0) m[k] = 0.0;
        for (std::size_t k = 0; k < m.size(); ++k) m[k] *= 0.55 + 0.45 * std::sin(2 * kPi * (0.31 + 0.173 * t) * static_cast<double>(k) / kFs + t);
        for (std::size_t k = 0; k < m.size(); ++k) buses[static_cast<std::size_t>(t % 6)][k] += m[k];
        all.push_back(track(t, ("Piste " + std::to_string(t)).c_str(), Element::Other, m));
    }
    for (int b = 0; b < 6; ++b) all.push_back(bus(100 + b, ("Bus " + std::to_string(b)).c_str(), buses[static_cast<std::size_t>(b)]));
    const auto start = std::chrono::steady_clock::now();
    const Composition c = buildComposition(all);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    int right = 0, wrong = 0;
    for (const auto& b : c.buses)
        for (const auto& m : b.members) {
            const bool ok = std::stoi(m.name.substr(6)) % 6 == b.id - 100;
            (ok ? right : wrong)++;
            if (!ok) std::printf("      %s rangée dans %s (part %.2f)\n", m.name.c_str(), b.name.c_str(), m.share);
        }
    std::printf("      %.0f ms ; %d pistes bien rangées, %d mal, %zu hors bus\n", ms, right, wrong, c.direct.size());
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__) && defined(NDEBUG)
    OMNIS_CHECK(ms < 400.0); // (sous ASan / TSan, tout est 5 à 20 fois plus lent)
#endif
    OMNIS_CHECK(wrong == 0);
    OMNIS_CHECK(right >= 34);
}

OMNIS_TEST("composition: pas assez de lecture commune -> on n'invente pas de pistes") {
    const Project p(1.0); // < 40 trames
    std::vector<Heard> all{track(1, "Kick", Element::Kick, p.kick), bus(2, "DRUMS", p.kick)};
    const Composition c = buildComposition(all);
    OMNIS_CHECK(c.buses.size() == 1 && c.buses[0].members.empty() && !c.buses[0].enoughToMatch);
    OMNIS_CHECK(c.direct.size() == 1);
}

OMNIS_TEST("composition: la région partagée (pistes, bus, noms, trames) se lit d'une autre instance") {
    const std::string ns = "omnistest" + std::to_string(static_cast<unsigned>(std::random_device{}() % 100000)) + "x";
#if defined(_WIN32)
    _putenv_s("OMNIS_LINK_NAMESPACE", ns.c_str());
#else
    setenv("OMNIS_LINK_NAMESPACE", ns.c_str(), 1);
#endif
    {
        link::Observer before;
        OMNIS_CHECK(!before.open() || before.instances().empty());
    }
    link::Endpoint kick, drums;
    OMNIS_CHECK(kick.join(link::Role::Track, "Kick Jayy 03", kFs));
    OMNIS_CHECK(drums.join(link::Role::Bus, "DRUMS", kFs));
    link::Identity id;
    id.element = Element::Kick;
    id.heard = Element::Kick;
    id.confidence = 0.7f;
    id.secondsHeard = 12.0f;
    kick.publish("Kick Jayy 03", id);
    link::Identity busId;
    busId.kick = busId.hat = true;
    drums.publish("DRUMS", busId);
    for (std::int64_t k = 0; k < 1500; ++k) { // plus que l'anneau : les plus anciennes sont remplacées
        std::array<float, link::kBands> e{};
        e[1] = static_cast<float>(k);
        kick.writeFrame(k, e);
    }
    link::Observer obs;
    OMNIS_CHECK(obs.open());
    const auto views = obs.instances();
    OMNIS_CHECK(views.size() == 2);
    for (const auto& v : views) {
        if (v.role == link::Role::Track) {
            OMNIS_CHECK(v.name == "Kick Jayy 03" && v.identity.element == Element::Kick && std::fabs(v.identity.confidence - 0.7f) < 1e-6f);
            const auto frames = obs.frames(v.slot, v.owner);
            OMNIS_CHECK(frames.size() == static_cast<std::size_t>(link::kFrameRing));
            OMNIS_CHECK(frames.back().index == 1499 && frames.front().index == 1500 - link::kFrameRing);
            OMNIS_CHECK(frames.back().energy[1] == 1499.0f);
        } else {
            OMNIS_CHECK(v.role == link::Role::Bus && v.name == "DRUMS" && v.identity.kick && v.identity.hat && !v.identity.snare);
        }
    }
    // Nom trop long, UTF-8 jamais coupé au milieu d'un caractère.
    kick.publish("Ébauche très très longue de piste qui dépasse la limite du nom é", id);
    for (const auto& v : obs.instances())
        if (v.role == link::Role::Track) OMNIS_CHECK(v.name.size() < static_cast<std::size_t>(link::kNameLength) && (static_cast<unsigned char>(v.name.back()) & 0xC0u) != 0xC0u);
    kick.leave();
    OMNIS_CHECK(obs.instances().size() == 1);
    drums.leave();
    obs.close();
    link::SharedRegion::unlink(link::regionName());
#if defined(_WIN32)
    _putenv_s("OMNIS_LINK_NAMESPACE", "");
#else
    unsetenv("OMNIS_LINK_NAMESPACE");
#endif
}

OMNIS_TEST("lien: thread audio, thread du lien et lecture d'une autre instance en même temps (sans verrou, sans course)") {
    const std::string ns = "omnistsan" + std::to_string(static_cast<unsigned>(std::random_device{}() % 100000)) + "x";
#if defined(_WIN32)
    _putenv_s("OMNIS_LINK_NAMESPACE", ns.c_str());
#else
    setenv("OMNIS_LINK_NAMESPACE", ns.c_str(), 1);
#endif
    {
        const Project p(6.0);
        link::LinkClient track, bus;
        track.setTrackName("Kick Jayy 03");
        bus.setTrackName("DRUMS");
        track.start(link::Role::Track, kFs);
        bus.start(link::Role::Bus, kFs);
        std::atomic<bool> quit{false};
        std::thread audio([&] {
            std::vector<float> buf(512);
            for (std::int64_t pos = 0; !quit.load(); pos += 512) {
                for (int i = 0; i < 512; ++i) buf[static_cast<std::size_t>(i)] = static_cast<float>(p.kick[static_cast<std::size_t>((pos + i) % static_cast<std::int64_t>(p.kick.size()))]);
                track.process(buf.data(), buf.data(), 512, pos);
                bus.process(buf.data(), buf.data(), 512, pos);
                if (pos % 8192 == 0) std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        });
        link::Observer obs;
        Composition c;
        const auto found = [&](const char* name) { return !c.buses.empty() && c.buses[0].members.size() == 1 && c.buses[0].members[0].name == name; };
        for (int i = 0; i < 60 && !found("Kick Jayy 03"); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!obs.isOpen()) obs.open();
            if (obs.isOpen()) c = buildComposition(readInstances(obs));
        }
        OMNIS_CHECK(found("Kick Jayy 03"));
        track.setTrackName("Kick Jayy 04"); // renommée dans le DAW pendant la lecture
        for (int i = 0; i < 30 && !found("Kick Jayy 04"); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            c = buildComposition(readInstances(obs));
        }
        quit.store(true);
        audio.join();
        if (!found("Kick Jayy 04")) std::printf("%s\n", describeComposition(c).c_str());
        OMNIS_CHECK(c.buses.size() == 1 && c.buses[0].members.size() == 1);
        if (!c.buses.empty() && !c.buses[0].members.empty()) OMNIS_CHECK(c.buses[0].members[0].name == "Kick Jayy 04");
        OMNIS_CHECK(track.slot() >= 0 && bus.slot() >= 0 && track.slot() != bus.slot());
        // Arrêt, puis départ dans l'autre rôle : l'emplacement est rendu, puis repris.
        track.stop();
        OMNIS_CHECK(obs.instances().size() == 1);
        track.start(link::Role::Bus, kFs);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        OMNIS_CHECK(obs.instances().size() == 2);
        track.stop();
        bus.stop();
        obs.close();
    }
    link::SharedRegion::unlink(link::regionName());
#if defined(_WIN32)
    _putenv_s("OMNIS_LINK_NAMESPACE", "");
#else
    unsetenv("OMNIS_LINK_NAMESPACE");
#endif
}
