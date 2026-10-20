// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestCore.cpp — Primitives temps réel.
// =============================================================================
#include "TestFramework.h"

#include "core/AudioBuffer.h"
#include "core/DelayLine.h"
#include "core/OnePoleSmoother.h"
#include "core/Platform.h"
#include "core/SpscRingBuffer.h"
#include "core/TripleBuffer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using namespace omnis;

OMNIS_TEST("smoother: 99 % atteint au temps de lissage, sans dépassement") {
    constexpr double fs = 48000.0, timeMs = 20.0;
    OnePoleSmoother s;
    s.prepare(fs, timeMs);
    s.snapTo(0.0);
    s.setTarget(1.0);
    const int n = static_cast<int>(fs * timeMs * 1e-3);
    double previous = 0.0;
    bool monotonic = true;
    for (int i = 0; i < n; ++i) {
        const double v = s.next();
        if (v < previous || v > 1.0) monotonic = false;
        previous = v;
    }
    OMNIS_CHECK(monotonic);
    OMNIS_CHECK_NEAR(previous, 0.99, 1.0e-4);
}

OMNIS_TEST("smoother: colle exactement à la cible et s'arrête") {
    OnePoleSmoother s;
    s.prepare(44100.0, 5.0);
    s.snapTo(0.25);
    s.setTarget(0.75);
    for (int i = 0; i < 44100; ++i) s.next();
    OMNIS_CHECK(!s.isSmoothing());
    OMNIS_CHECK(s.current() == 0.75);
}

OMNIS_TEST("smoother: temps nul = saut immédiat") {
    OnePoleSmoother s;
    s.prepare(48000.0, 0.0);
    s.snapTo(0.0);
    s.setTarget(3.0);
    OMNIS_CHECK(s.next() == 3.0);
}

OMNIS_TEST("spsc: sémantique FIFO, bouclage et file pleine") {
    SpscRingBuffer<int> rb(5); // arrondi à 8
    OMNIS_CHECK(rb.capacity() == 8);
    int data[10];
    for (int i = 0; i < 10; ++i) data[i] = i;
    OMNIS_CHECK(rb.push(data, 6) == 6);
    int out[10] = {};
    OMNIS_CHECK(rb.pop(out, 4) == 4);
    OMNIS_CHECK(out[0] == 0 && out[3] == 3);
    OMNIS_CHECK(rb.push(data, 10) == 6); // 2 restants + 6 = 8 (pleine)
    OMNIS_CHECK(rb.freeSpace() == 0);
    OMNIS_CHECK(rb.pop(out, 10) == 8);
    OMNIS_CHECK(out[0] == 4 && out[1] == 5 && out[2] == 0 && out[7] == 5);
    OMNIS_CHECK(rb.available() == 0);
}

OMNIS_TEST("spsc: concurrence producteur/consommateur, 2 millions d'éléments ordonnés") {
    SpscRingBuffer<std::uint32_t> rb(1024);
    constexpr std::uint32_t kCount = 2'000'000;
    std::atomic<bool> orderOk{true};
    std::thread consumer([&] {
        std::uint32_t expected = 0;
        std::array<std::uint32_t, 97> chunk{};
        while (expected < kCount) {
            const std::size_t n = rb.pop(chunk.data(), chunk.size());
            for (std::size_t i = 0; i < n; ++i)
                if (chunk[i] != expected++) orderOk = false;
            if (n == 0) std::this_thread::yield();
        }
    });
    std::uint32_t next = 0;
    std::array<std::uint32_t, 61> chunk{};
    while (next < kCount) {
        std::size_t n = 0;
        for (; n < chunk.size() && next + n < kCount; ++n) chunk[n] = next + static_cast<std::uint32_t>(n);
        std::size_t pushed = 0;
        while (pushed < n) {
            const std::size_t k = rb.push(chunk.data() + pushed, n - pushed);
            pushed += k;
            if (k == 0) std::this_thread::yield();
        }
        next += static_cast<std::uint32_t>(n);
    }
    consumer.join();
    OMNIS_CHECK(orderOk.load());
}

OMNIS_TEST("triple buffer: le lecteur ne voit jamais d'état déchiré, versions croissantes") {
    struct Snapshot {
        std::uint64_t version = 0;
        std::array<std::uint64_t, 32> payload{};
    };
    TripleBuffer<Snapshot> tb;
    constexpr std::uint64_t kVersions = 200'000;
    std::atomic<bool> done{false};
    bool consistent = true, monotonic = true;
    std::uint64_t lastSeen = 0, updates = 0;
    std::thread reader([&] {
        for (;;) {
            // `done` est lu AVANT update() : s'il valait déjà true, toutes les
            // publications sont visibles et un update() vide signifie la fin.
            const bool finished = done.load(std::memory_order_acquire);
            if (!tb.update()) {
                if (finished) break;
                continue;
            }
            ++updates;
            const Snapshot& s = tb.readBuffer();
            for (auto v : s.payload)
                if (v != s.version) consistent = false;
            if (s.version < lastSeen) monotonic = false;
            lastSeen = s.version;
        }
    });
    for (std::uint64_t v = 1; v <= kVersions; ++v) {
        Snapshot& w = tb.writeBuffer();
        w.version = v;
        w.payload.fill(v);
        tb.publish();
    }
    done.store(true, std::memory_order_release);
    reader.join();
    OMNIS_CHECK(consistent);
    OMNIS_CHECK(monotonic);
    OMNIS_CHECK(lastSeen == kVersions); // le dernier état publié est toujours récupéré
    OMNIS_CHECK(updates > 0);
}

OMNIS_TEST("audio buffer: canaux alignés sur 64 octets") {
    AudioBuffer b(2, 1001);
    for (int ch = 0; ch < 2; ++ch)
        OMNIS_CHECK(reinterpret_cast<std::uintptr_t>(b.channel(ch)) % kSimdAlignment == 0);
}

OMNIS_TEST("delay line: retard entier exact, quel que soit le découpage en blocs") {
    DelayLine d;
    d.prepare(2, 1000);
    d.setDelay(777);
    std::vector<double> l(5000), r(5000);
    for (int i = 0; i < 5000; ++i) {
        l[static_cast<std::size_t>(i)] = i + 1.0;
        r[static_cast<std::size_t>(i)] = -(i + 1.0);
    }
    int pos = 0, block = 1;
    while (pos < 5000) {
        const int n = std::min(block, 5000 - pos);
        double* io[2] = {l.data() + pos, r.data() + pos};
        d.process(io, 2, n);
        pos += n;
        block = block * 3 % 257 + 1;
    }
    bool ok = true;
    for (int i = 0; i < 5000; ++i) {
        const double expected = i < 777 ? 0.0 : i - 777 + 1.0;
        if (l[static_cast<std::size_t>(i)] != expected || r[static_cast<std::size_t>(i)] != -expected) ok = false;
    }
    OMNIS_CHECK(ok);
}

OMNIS_TEST("denormals: FTZ/DAZ actif dans la portée puis restauré") {
#if OMNIS_ARCH_X86
    const unsigned before = _mm_getcsr();
    {
        ScopedNoDenormals guard;
        volatile double tiny = 1.0e-310; // dénormal
        volatile double result = tiny * 0.5;
        OMNIS_CHECK(result == 0.0);
    }
    OMNIS_CHECK(_mm_getcsr() == before);
#else
    OMNIS_CHECK(true);
#endif
}
