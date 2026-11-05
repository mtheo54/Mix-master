// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestPluginRealtime.cpp — Le thread audio n'alloue JAMAIS de mémoire :
//  une allocation dans process() peut bloquer (verrou de l'allocateur) et faire
//  craquer le son dans le DAW. On compte toutes les allocations faites PENDANT
//  process(), sur des milliers de blocs : automation de tous les modules,
//  bypass, Delta, Gain Match, Monitor, écoute simulée, changement d'ordre du
//  rack, mode travail, 32 et 64 bits, tailles de blocs variables.
// =============================================================================
#include "TestFramework.h"

#if !defined(_WIN32) // remplacement de operator new : POSIX (Linux, macOS) seulement

#include "params/Parameters.h"
#include "plugin/vst3/OmnisProcessor.h"

#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <random>
#include <execinfo.h>
#include <unistd.h>

// ---- Compteur d'allocations (ce programme de test seulement) -----------------
namespace {
thread_local bool gWatching = false;
std::atomic<long> gAllocations{0};
/// Les premières piles d'appels fautives (pour trouver d'où vient l'allocation).
void reportStack() {
    if (gAllocations.load() > 4) return;
    void* frames[24];
    const int n = backtrace(frames, 24);
    const char line[] = "      --- allocation dans process() :\n";
    (void)!write(2, line, sizeof line - 1);
    backtrace_symbols_fd(frames, n, 2);
}
void* counted(std::size_t n) {
    if (gWatching) {
        gWatching = false; // backtrace peut allouer
        gAllocations.fetch_add(1, std::memory_order_relaxed);
        reportStack();
        gWatching = true;
    }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* countedAligned(std::size_t n, std::align_val_t a) {
    if (gWatching) gAllocations.fetch_add(1, std::memory_order_relaxed);
    const std::size_t al = static_cast<std::size_t>(a);
    void* p = nullptr;
    if (posix_memalign(&p, al < sizeof(void*) ? sizeof(void*) : al, n ? n : 1) == 0) return p;
    throw std::bad_alloc();
}
} // namespace
void* operator new(std::size_t n) { return counted(n); }
void* operator new[](std::size_t n) { return counted(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return counted(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return counted(n); } catch (...) { return nullptr; } }
void* operator new(std::size_t n, std::align_val_t a) { return countedAligned(n, a); }
void* operator new[](std::size_t n, std::align_val_t a) { return countedAligned(n, a); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }

using namespace omnis;
using namespace Steinberg;
using namespace Steinberg::Vst;
using params::ParamId;

OMNIS_TEST("temps réel: process() n'alloue jamais (automation de tout, bypass, Delta, écoutes, ordre du rack, 32 / 64 bits, blocs variables)") {
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    std::printf("      (sanitiseur : compteur d'allocations sans objet, test passé)\n");
    return;
#else
    for (const auto format : {kSample32, kSample64}) {
        IPtr<vst3::OmnisProcessor> p = owned(new vst3::OmnisProcessor());
        p->initialize(nullptr);
        SpeakerArrangement ins[2] = {SpeakerArr::kStereo, SpeakerArr::kStereo}, outs[1] = {SpeakerArr::kStereo};
        p->setBusArrangements(ins, 2, outs, 1);
        ProcessSetup setup{kRealtime, format, 1024, 48000.0};
        p->setupProcessing(setup);
        p->setActive(true);
        p->setProcessing(true);
        HostProcessData data;
        data.prepare(*p, 1024, format);
        ProcessContext context{};
        context.sampleRate = 48000.0;
        context.state = ProcessContext::kPlaying;
        data.processContext = &context;
        ParameterChanges changes(64), outChanges(64);
        data.inputParameterChanges = &changes;
        data.outputParameterChanges = &outChanges;
        std::mt19937 rng(format == kSample32 ? 1 : 2);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        std::vector<ParamId> automated;
        for (const auto& spec : params::allParameters())
            if (spec.kind != params::ParamKind::Meter && spec.id != ParamId::LowLatency) automated.push_back(spec.id); // le mode travail redemande la latence
        long samples = 0;
        const auto block = [&](int n, bool watch) {
            changes.clearQueue();
            outChanges.clearQueue();
            for (int k = 0; k < 6; ++k) {
                int32 index = 0;
                if (auto* q = changes.addParameterData(static_cast<ParamID>(automated[static_cast<std::size_t>(rng() % automated.size())]), index)) {
                    q->addPoint(0, u(rng), index);
                    q->addPoint(n / 2, u(rng), index);
                }
            }
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i) {
                    const double x = 0.5 * std::sin(0.01 * static_cast<double>(samples + i) * (c + 1));
                    if (format == kSample32) data.inputs[0].channelBuffers32[c][i] = static_cast<float>(x);
                    else data.inputs[0].channelBuffers64[c][i] = x;
                }
            data.numSamples = n;
            context.projectTimeSamples = samples;
            gWatching = watch;
            p->process(data);
            gWatching = false;
            samples += n;
        };
        // Échauffement : les threads de fond préparent leurs noyaux.
        for (int i = 0; i < 200; ++i) block(512, false);
        gAllocations = 0;
        for (int i = 0; i < 1500; ++i) block(1 + static_cast<int>(rng() % 1024), true);
        std::printf("      %s : %ld allocation(s) pendant process()\n", format == kSample32 ? "32 bits" : "64 bits", gAllocations.load());
        OMNIS_CHECK(gAllocations.load() == 0);
        p->setProcessing(false);
        p->setActive(false);
        p->terminate();
    }
#endif
}

#endif // !_WIN32
