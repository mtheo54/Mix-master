// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/StemTimeline.h — Ce que le master a entendu d'un stem, et QUAND.
//
//  Deux pistes ne se masquent que si elles jouent en même temps : la mélodie
//  qui a du grave pendant le refrain, quand la 808 se tait, ne la gêne pas.
//  Chaque stem est donc rangé par blocs de 8192 trames de temps PROJET
//  (≈ 0,17 s à 48 kHz) : le bloc k de deux stems est le même instant du
//  morceau, quel que soit le moment où le master l'a entendu.
//
//  Par bloc : énergie du Mid dans 15 bandes (20 Hz … 20 kHz, serrées dans le
//  grave), et énergie du Side sous 150 Hz. Une FFT de 8192 (fenêtre de Hann)
//  par bloc : ~5 Hz de résolution, ce qu'il faut pour séparer une 808 à 40 Hz
//  d'un piano à 100 Hz. Un bloc entendu plusieurs fois (boucle) est moyenné.
//
//  Non temps réel (thread du lien du master, ou outil hors ligne).
// =============================================================================
#pragma once

#include "dsp/Fft.h"

#include <array>
#include <cstdint>
#include <vector>

namespace omnis::stems {

inline constexpr int kTimelineBands = 15;
inline constexpr std::array<double, kTimelineBands + 1> kTimelineEdges{
    20.0, 40.0, 60.0, 80.0, 100.0, 150.0, 200.0, 300.0, 500.0, 800.0, 1200.0, 2000.0, 3500.0, 6000.0, 10000.0, 20000.0};

class StemTimeline {
public:
    static constexpr int kBlockFrames = 8192;
    static constexpr int kMaxBlocks = 8192; ///< ~23 min à 48 kHz : au-delà, rien n'est rangé

    struct Block {
        std::array<float, kTimelineBands> mid{}; ///< puissance moyenne par bande (somme des passages)
        float sideLow = 0.0f;                    ///< puissance du Side sous 150 Hz
        float midLow = 0.0f;                     ///< puissance du Mid sous 150 Hz
        std::uint16_t passes = 0;                ///< 0 : jamais entendu
    };

    void prepare(double sampleRate);
    void reset();

    /// Trames jouées ; `projectStart` : position projet de la première. Les
    /// trames d'un même bloc peuvent arriver en plusieurs fois.
    void push(const double* left, const double* right, int numFrames, std::int64_t projectStart);
    /// Termine le bloc en cours (fin d'écoute, ou avant une analyse).
    void flush();

    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] double blockSeconds() const noexcept { return kBlockFrames / sampleRate_; }
    [[nodiscard]] int size() const noexcept { return static_cast<int>(blocks_.size()); }
    /// Bloc k (moyenne de ses passages), ou nullptr s'il n'a jamais été entendu.
    [[nodiscard]] const Block* block(int k) const noexcept;
    /// Puissance moyenne d'une bande dans le bloc k (passages moyennés), 0 si non entendu.
    [[nodiscard]] double bandPower(int k, int band) const noexcept;
    [[nodiscard]] double totalPower(int k) const noexcept;
    [[nodiscard]] int heardBlocks() const noexcept;

private:
    void analysePending();

    double sampleRate_ = 48000.0;
    dsp::Fft fft_;
    std::vector<dsp::Fft::Complex> spectrumMid_, spectrumSide_;
    std::vector<double> window_;
    std::array<int, kTimelineBands + 1> binEdges_{};
    int lowBin_ = 0; ///< premier bin au-dessus de 150 Hz

    std::vector<Block> blocks_;
    std::vector<double> pendingMid_, pendingSide_;
    std::vector<bool> pendingHas_;
    std::int64_t pendingIndex_ = -1;
    int pendingCount_ = 0;
};

} // namespace omnis::stems
