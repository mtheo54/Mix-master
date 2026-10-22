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
