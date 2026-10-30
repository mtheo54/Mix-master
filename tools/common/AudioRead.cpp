// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/AudioRead.cpp
// =============================================================================
#include "AudioRead.h"

#include "dr_flac.h"
#include "dr_mp3.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace omnis::tools {

namespace {

std::string extensionOf(const std::filesystem::path& path) {
    std::string e = utf8Path(path.extension());
    if (!e.empty()) e.erase(0, 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

/// Fichier entier en mémoire : dr_flac / dr_mp3 n'ouvrent que des noms `char`
/// (page de code sous Windows), la mémoire évite la question.
bool readBytes(const std::filesystem::path& path, std::vector<unsigned char>& data) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const auto end = in.tellg();
    if (end < 0) return false;
    const auto size = static_cast<std::size_t>(end);
    data.resize(size);
    in.seekg(0);
    return static_cast<bool>(in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)));
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

std::vector<EmbeddedPicture> readFlacPictures(const unsigned char* data, std::size_t size) {
    std::vector<EmbeddedPicture> out;
    const auto onMeta = [](void* user, drflac_metadata* meta) {
        if (!meta || meta->type != DRFLAC_METADATA_BLOCK_TYPE_PICTURE || !meta->data.picture.pPictureData) return;
        // Les octets ne vivent que pendant l'appel : copiés.
        auto& list = *static_cast<std::vector<EmbeddedPicture>*>(user);
        list.push_back({std::string(reinterpret_cast<const char*>(meta->data.picture.pPictureData), meta->data.picture.pictureDataSize),
                        meta->data.picture.type});
    };
    if (drflac* f = drflac_open_memory_with_metadata(data, size, onMeta, &out, nullptr)) drflac_close(f);
    return out;
}

std::vector<EmbeddedPicture> readEmbeddedPictures(const std::filesystem::path& path) {
    if (extensionOf(path) != "flac") return {};
    std::vector<unsigned char> data;
    if (!readBytes(path, data)) return {};
    return readFlacPictures(data.data(), data.size());
}

bool isReadableAudio(const std::filesystem::path& path) {
    const std::string e = extensionOf(path);
    return e == "wav" || e == "mp3" || e == "flac";
}

bool readAudioFile(const std::filesystem::path& path, AudioFile& out, std::string& error) {
    const std::string e = extensionOf(path);
    if (e != "mp3" && e != "flac") return readWav(path, out, error);
    std::vector<unsigned char> data;
    if (!readBytes(path, data)) {
        error = "impossible d'ouvrir " + utf8Path(path);
        return false;
    }
    if (e == "mp3") {
        drmp3_config config{};
        drmp3_uint64 frames = 0;
        float* pcm = drmp3_open_memory_and_read_pcm_frames_f32(data.data(), data.size(), &config, &frames, nullptr);
        data = {};
        const bool ok = fill(out, pcm, frames, config.channels, config.sampleRate);
        drmp3_free(pcm, nullptr);
        if (!ok) error = "MP3 illisible : " + utf8Path(path);
        return ok;
    }
    unsigned channels = 0, sampleRate = 0;
    drflac_uint64 frames = 0;
    float* pcm = drflac_open_memory_and_read_pcm_frames_f32(data.data(), data.size(), &channels, &sampleRate, &frames, nullptr);
    data = {};
    const bool ok = fill(out, pcm, frames, channels, sampleRate);
    drflac_free(pcm, nullptr);
    if (!ok) error = "FLAC illisible : " + utf8Path(path);
    return ok;
}

} // namespace omnis::tools
