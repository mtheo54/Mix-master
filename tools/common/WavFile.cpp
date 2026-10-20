// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/WavFile.cpp
// =============================================================================
#include "WavFile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace omnis::tools {

namespace {

constexpr std::uint16_t kFormatPcm = 0x0001;
constexpr std::uint16_t kFormatFloat = 0x0003;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

std::uint16_t readU16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t readU32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

void putU16(std::vector<unsigned char>& b, std::uint16_t v) {
    b.push_back(static_cast<unsigned char>(v & 0xFF));
    b.push_back(static_cast<unsigned char>(v >> 8));
}
void putU32(std::vector<unsigned char>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
}
void putTag(std::vector<unsigned char>& b, const char* tag) { b.insert(b.end(), tag, tag + 4); }

} // namespace

bool parseSampleFormat(std::string_view text, SampleFormat& format) {
    std::string t(text);
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (t == "pcm16" || t == "16") format = SampleFormat::Pcm16;
    else if (t == "pcm24" || t == "24") format = SampleFormat::Pcm24;
    else if (t == "pcm32") format = SampleFormat::Pcm32;
    else if (t == "f32" || t == "float32" || t == "32f") format = SampleFormat::Float32;
    else if (t == "f64" || t == "float64" || t == "64f") format = SampleFormat::Float64;
    else return false;
    return true;
}

bool readWav(const std::string& path, AudioFile& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "impossible d'ouvrir " + path;
        return false;
    }
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
        if (data.size() >= 4 && (std::memcmp(data.data(), "RF64", 4) == 0 || std::memcmp(data.data(), "BW64", 4) == 0))
            error = "RF64/BW64 non pris en charge";
        else
            error = "fichier WAV RIFF invalide";
        return false;
    }

    std::uint16_t formatTag = 0, numChannels = 0, bitsPerSample = 0, blockAlign = 0;
    std::uint32_t sampleRate = 0;
    const unsigned char* sampleData = nullptr;
    std::size_t sampleBytes = 0;

    std::size_t pos = 12;
    while (pos + 8 <= data.size()) {
        const unsigned char* chunk = data.data() + pos;
        const std::uint32_t size = readU32(chunk + 4);
        const std::size_t bodyPos = pos + 8;
        const std::size_t available = std::min<std::size_t>(size, data.size() - bodyPos);
        if (std::memcmp(chunk, "fmt ", 4) == 0 && available >= 16) {
            const unsigned char* f = data.data() + bodyPos;
            formatTag = readU16(f);
            numChannels = readU16(f + 2);
            sampleRate = readU32(f + 4);
            blockAlign = readU16(f + 12);
            bitsPerSample = readU16(f + 14);
            if (formatTag == kFormatExtensible && available >= 26) formatTag = readU16(f + 24); // SubFormat GUID
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            sampleData = data.data() + bodyPos;
            sampleBytes = available; // tolère un data tronqué / taille 0xFFFFFFFF
        }
        pos = bodyPos + size + (size & 1u); // les chunks sont alignés sur 2 octets
    }

    if (!sampleData || numChannels == 0 || sampleRate == 0 || blockAlign == 0) {
        error = "chunks fmt/data manquants ou invalides";
        return false;
    }
    const int bytesPerSample = bitsPerSample / 8;
    const bool supported = (formatTag == kFormatPcm && (bitsPerSample == 16 || bitsPerSample == 24 || bitsPerSample == 32)) ||
                           (formatTag == kFormatFloat && (bitsPerSample == 32 || bitsPerSample == 64));
    if (!supported || blockAlign != numChannels * bytesPerSample) {
        error = "format non pris en charge (tag " + std::to_string(formatTag) + ", " + std::to_string(bitsPerSample) + " bits)";
        return false;
    }

    const std::size_t frames = sampleBytes / blockAlign;
    out.sampleRate = sampleRate;
    out.resize(numChannels, static_cast<int>(frames));
    for (std::size_t i = 0; i < frames; ++i) {
        for (int ch = 0; ch < numChannels; ++ch) {
            const unsigned char* p = sampleData + i * blockAlign + static_cast<std::size_t>(ch) * bytesPerSample;
            double v = 0.0;
            if (formatTag == kFormatFloat) {
                if (bitsPerSample == 32) {
                    float f;
                    std::uint32_t bits = readU32(p);
                    std::memcpy(&f, &bits, 4);
                    v = f;
                } else {
                    std::uint64_t bits = static_cast<std::uint64_t>(readU32(p)) | (static_cast<std::uint64_t>(readU32(p + 4)) << 32);
                    std::memcpy(&v, &bits, 8);
                }
            } else if (bitsPerSample == 16) {
                v = static_cast<std::int16_t>(readU16(p)) / 32768.0;
            } else if (bitsPerSample == 24) {
                std::int32_t s = static_cast<std::int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
                if (s & 0x800000) s -= 0x1000000; // extension de signe
                v = s / 8388608.0;
            } else {
                v = static_cast<std::int32_t>(readU32(p)) / 2147483648.0;
            }
            out.channels[static_cast<std::size_t>(ch)][i] = v;
        }
    }
    return true;
}

bool writeWav(const std::string& path, const AudioFile& in, SampleFormat format, std::string& error) {
    const int numChannels = in.numChannels();
    const int frames = in.numFrames();
    if (numChannels <= 0) {
        error = "aucun canal à écrire";
        return false;
    }

    int bits = 32;
    std::uint16_t tag = kFormatFloat;
    switch (format) {
        case SampleFormat::Pcm16: bits = 16; tag = kFormatPcm; break;
        case SampleFormat::Pcm24: bits = 24; tag = kFormatPcm; break;
        case SampleFormat::Pcm32: bits = 32; tag = kFormatPcm; break;
        case SampleFormat::Float32: bits = 32; tag = kFormatFloat; break;
        case SampleFormat::Float64: bits = 64; tag = kFormatFloat; break;
    }
    const int bytesPerSample = bits / 8;
    const std::uint32_t blockAlign = static_cast<std::uint32_t>(numChannels * bytesPerSample);
    const std::uint64_t dataBytes = static_cast<std::uint64_t>(frames) * blockAlign;
    if (dataBytes > 0xFFFFFFF0ull) {
        error = "fichier trop grand pour RIFF (> 4 Go)";
        return false;
    }

    std::vector<unsigned char> b;
    b.reserve(static_cast<std::size_t>(dataBytes) + 64);
    putTag(b, "RIFF");
    putU32(b, 0); // rempli à la fin
    putTag(b, "WAVE");
    putTag(b, "fmt ");
    putU32(b, 16);
    putU16(b, tag);
    putU16(b, static_cast<std::uint16_t>(numChannels));
    putU32(b, static_cast<std::uint32_t>(std::lround(in.sampleRate)));
    putU32(b, static_cast<std::uint32_t>(std::lround(in.sampleRate)) * blockAlign);
    putU16(b, static_cast<std::uint16_t>(blockAlign));
    putU16(b, static_cast<std::uint16_t>(bits));
    putTag(b, "data");
    putU32(b, static_cast<std::uint32_t>(dataBytes));

    for (int i = 0; i < frames; ++i) {
        for (int ch = 0; ch < numChannels; ++ch) {
            const double v = in.channels[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)];
            switch (format) {
                case SampleFormat::Float32: {
                    const float f = static_cast<float>(v);
                    std::uint32_t u;
                    std::memcpy(&u, &f, 4);
                    putU32(b, u);
                    break;
                }
                case SampleFormat::Float64: {
                    std::uint64_t u;
                    std::memcpy(&u, &v, 8);
                    putU32(b, static_cast<std::uint32_t>(u & 0xFFFFFFFFu));
                    putU32(b, static_cast<std::uint32_t>(u >> 32));
                    break;
                }
                case SampleFormat::Pcm16: {
                    const auto s = static_cast<std::int32_t>(std::clamp(std::lround(v * 32768.0), -32768L, 32767L));
                    putU16(b, static_cast<std::uint16_t>(s));
                    break;
                }
                case SampleFormat::Pcm24: {
                    const auto s = static_cast<std::int32_t>(std::clamp(std::lround(v * 8388608.0), -8388608L, 8388607L));
                    b.push_back(static_cast<unsigned char>(s & 0xFF));
                    b.push_back(static_cast<unsigned char>((s >> 8) & 0xFF));
                    b.push_back(static_cast<unsigned char>((s >> 16) & 0xFF));
                    break;
                }
                case SampleFormat::Pcm32: {
                    const auto s = static_cast<std::int64_t>(std::clamp(std::llround(v * 2147483648.0), -2147483648LL, 2147483647LL));
                    putU32(b, static_cast<std::uint32_t>(static_cast<std::int32_t>(s)));
                    break;
                }
            }
        }
    }
    const auto riffSize = static_cast<std::uint32_t>(b.size() - 8);
    for (int i = 0; i < 4; ++i) b[4 + static_cast<std::size_t>(i)] = static_cast<unsigned char>((riffSize >> (8 * i)) & 0xFF);

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "impossible de créer " + path;
        return false;
    }
    file.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    if (!file) {
        error = "erreur d'écriture " + path;
        return false;
    }
    return true;
}

} // namespace omnis::tools
