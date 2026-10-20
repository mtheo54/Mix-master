// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/WavFile.h — Lecture / écriture WAV minimale et sans dépendance.
//
//  Lecture  : PCM 16/24/32 bits, IEEE float 32/64 bits, WAVE_FORMAT_EXTENSIBLE.
//  Écriture : PCM 16/24 bits (arrondi + écrêtage), float 32/64 bits.
//  Les échantillons sont manipulés en double, normalisés dans [-1, 1).
//  RF64 / BW64 (> 4 Go) ne sont pas pris en charge (outils de banc d'essai).
// =============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace omnis::tools {

enum class SampleFormat { Pcm16, Pcm24, Pcm32, Float32, Float64 };

struct AudioFile {
    double sampleRate = 48000.0;
    std::vector<std::vector<double>> channels;

    [[nodiscard]] int numChannels() const noexcept { return static_cast<int>(channels.size()); }
    [[nodiscard]] int numFrames() const noexcept { return channels.empty() ? 0 : static_cast<int>(channels[0].size()); }
    [[nodiscard]] double durationSeconds() const noexcept { return numFrames() / sampleRate; }

    void resize(int numChannels, int numFrames) {
        channels.assign(static_cast<std::size_t>(numChannels), std::vector<double>(static_cast<std::size_t>(numFrames), 0.0));
    }
};

bool readWav(const std::string& path, AudioFile& out, std::string& error);
bool writeWav(const std::string& path, const AudioFile& in, SampleFormat format, std::string& error);

/// "pcm16", "pcm24", "pcm32", "f32", "f64" (insensible à la casse).
bool parseSampleFormat(std::string_view text, SampleFormat& format);

} // namespace omnis::tools
