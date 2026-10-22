// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisState.cpp
// =============================================================================
#include "OmnisState.h"

#include "params/Parameters.h"

#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>

namespace omnis::vst3 {

using namespace Steinberg;

bool writeState(IBStream* stream, const std::function<double(std::uint32_t)>& getNormalized) {
    if (!stream) return false;
    IBStreamer streamer(stream, kLittleEndian);

    std::uint32_t count = 0;
    for (const auto& spec : params::allParameters())
        if (params::isPersistent(spec)) ++count;

    bool ok = streamer.writeInt32u(kStateMagic) && streamer.writeInt32u(kStateVersion) && streamer.writeInt32u(count);
    for (const auto& spec : params::allParameters()) {
        if (!params::isPersistent(spec)) continue;
        const auto id = static_cast<std::uint32_t>(spec.id);
        ok = ok && streamer.writeInt32u(id) && streamer.writeDouble(getNormalized(id));
    }
    return ok;
}

bool readState(IBStream* stream, const std::function<void(std::uint32_t, double)>& setNormalized) {
    if (!stream) return false;
    IBStreamer streamer(stream, kLittleEndian);

    uint32 magic = 0, version = 0, count = 0;
    if (!streamer.readInt32u(magic) || magic != kStateMagic) return false;
    if (!streamer.readInt32u(version) || version == 0) return false;
    if (!streamer.readInt32u(count)) return false;

    // Garde-fou contre un flux corrompu : jamais plus de 4096 entrées.
    count = std::min<uint32>(count, 4096);
    for (uint32 i = 0; i < count; ++i) {
        uint32 id = 0;
        double value = 0.0;
        if (!streamer.readInt32u(id) || !streamer.readDouble(value)) return false;
        const auto* spec = params::findParameter(id);
        if (!spec || !params::isPersistent(*spec) || !std::isfinite(value)) continue;
        setNormalized(id, std::clamp(value, 0.0, 1.0));
    }
    return true;
}

bool writeExtras(IBStream* stream, const std::string& text) {
    if (!stream) return false;
    IBStreamer streamer(stream, kLittleEndian);
    if (!streamer.writeInt32u(kExtrasMagic) || !streamer.writeInt32u(static_cast<uint32>(text.size()))) return false;
    return text.empty() || streamer.writeRaw(text.data(), static_cast<TSize>(text.size())) == static_cast<TSize>(text.size());
}

bool readExtras(IBStream* stream, std::string& text) {
    if (!stream) return false;
    IBStreamer streamer(stream, kLittleEndian);
    uint32 magic = 0, length = 0;
    if (!streamer.readInt32u(magic) || magic != kExtrasMagic || !streamer.readInt32u(length)) return false;
    if (length > (16u << 20)) return false; // garde-fou : flux corrompu
    text.assign(length, '\0');
    return length == 0 || streamer.readRaw(text.data(), static_cast<TSize>(length)) == static_cast<TSize>(length);
}

} // namespace omnis::vst3
