// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/AudioRead.cpp
// =============================================================================
#include "AudioRead.h"

#include "dr_flac.h"
#include "dr_mp3.h"

#include <algorithm>
#include <cctype>

namespace omnis::tools {

namespace {

std::string extensionOf(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot + 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

bool fill(AudioFile& out, const float* interleaved, unsigned long long frames, unsigned channels, unsigned sampleRate) {
    if (!interleaved || frames == 0 || channels == 0) return false;
    out.sampleRate = sampleRate;
    out.resize(static_cast<int>(channels), static_cast<int>(frames));
    for (unsigned long long i = 0; i < frames; ++i)
        for (unsigned c = 0; c < channels; ++c)
            out.channels[c][static_cast<std::size_t>(i)] = static_cast<double>(interleaved[i * channels + c]);
    return true;
}

} // namespace

bool isReadableAudio(const std::string& path) {
    const std::string e = extensionOf(path);
    return e == "wav" || e == "mp3" || e == "flac";
}

bool readAudioFile(const std::string& path, AudioFile& out, std::string& error) {
    const std::string e = extensionOf(path);
    if (e == "mp3") {
        drmp3_config config{};
        drmp3_uint64 frames = 0;
        float* pcm = drmp3_open_file_and_read_pcm_frames_f32(path.c_str(), &config, &frames, nullptr);
        const bool ok = fill(out, pcm, frames, config.channels, config.sampleRate);
        drmp3_free(pcm, nullptr);
        if (!ok) error = "MP3 illisible : " + path;
        return ok;
    }
    if (e == "flac") {
        unsigned channels = 0, sampleRate = 0;
        drflac_uint64 frames = 0;
        float* pcm = drflac_open_file_and_read_pcm_frames_f32(path.c_str(), &channels, &sampleRate, &frames, nullptr);
        const bool ok = fill(out, pcm, frames, channels, sampleRate);
        drflac_free(pcm, nullptr);
        if (!ok) error = "FLAC illisible : " + path;
        return ok;
    }
    return readWav(path, out, error);
}

} // namespace omnis::tools
