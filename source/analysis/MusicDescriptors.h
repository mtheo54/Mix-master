// =============================================================================
//  Abrasion Labs // OMNIS
//  analysis/MusicDescriptors.h — Ce qu'un morceau EST, au-delà de son spectre
//  moyen : la fiche complète d'une référence (et de la prod qu'on travaille).
//  Seulement des nombres, jamais d'audio. Analyse en flux (non temps réel).
//
//    tempo (BPM)       autocorrélation du flux des attaques (montées par zone, pas
//                      de 10 ms), score d'un tempo = sa période + la moitié de ses
//                      double et moitié, cherché entre 85 et 170 BPM (un trap à 142
//                      n'est pas lu à 71) ;
//    tonalité          chroma (120 Hz – 2 kHz) comparé aux 24 profils majeurs /
//                      mineurs (Krumhansl) ; confiance = écart au deuxième ;
//    note de la 808    fréquence du pic 30 – 110 Hz, la plus fréquente sur les
//                      blocs forts (pondérée par la puissance) ;
//    sections          sonie court terme (3 s) : 10e, 50e, 95e centiles du son
//                      non silencieux (couplet calme / refrain fort) ;
//    micro-dynamique   par zone (grave < 120 Hz, bas-médium 120 – 800, médium
//                      800 – 5k, aigu > 5k) : crête sur 10 ms moins niveau sur
//                      400 ms, médiane des fenêtres qui jouent (dB) : le « punch » ;
//    stéréo            corrélation, corrélation du grave (< 120 Hz), perte en mono ;
//    attaques          coups par seconde de son (montées dans n'importe quelle zone :
//                      les hats comptent, pas seulement le kick) ;
//    brillance         centre de gravité du spectre au-dessus de 200 Hz (amplitude) :
//                      la 808 ne tire pas tout vers le bas ;
//    crête, écrêtage   crête échantillon (dBFS), part des échantillons à −0,1 dBFS
//                      ou plus (un master écrêté exprès, rage).
// =============================================================================
#pragma once

#include "LoudnessMeter.h"

#include "dsp/Fft.h"

#include <array>
#include <complex>
#include <string>
#include <vector>

namespace omnis::analysis {

struct MusicDescriptors {
    bool valid = false;            ///< false : jamais mesuré (fiche ancienne)
    double seconds = 0.0;          ///< secondes de son analysées
    double bpm = 0.0;              ///< 0 : pas de tempo net
    int keyRoot = -1;              ///< 0 = Do … 11 = Si ; −1 : inconnue
    bool keyMinor = false;
    double keyConfidence = 0.0;    ///< 0 … 1
    double subHz = 0.0;            ///< 0 : pas de grave tenu
    std::array<double, 3> shortTermLufs{-70.0, -70.0, -70.0}; ///< P10, P50, P95
    std::array<double, 4> microDb{};  ///< grave, bas-médium, médium, aigu
    double correlation = 0.0, lowCorrelation = 0.0, monoLossDb = 0.0;
    double onsetsPerSecond = 0.0;
    double centroidHz = 0.0;
    double samplePeakDb = -200.0;
    double clipPercent = 0.0;

    /// « Fa# mineur », « Do majeur », « ? ».
    [[nodiscard]] std::string keyName() const;
    /// « F#m », « C » (clé du fichier) ; vide si inconnue.
    [[nodiscard]] std::string keyCode() const;
    bool parseKeyCode(const std::string& code);
    /// Note la plus proche d'une fréquence : « Fa# (46 Hz) ».
    static std::string noteName(double hz);
};

class MusicAnalyzer {
public:
    void prepare(double sampleRate);
    void reset();
    void push(const double* left, const double* right, int numFrames);
    [[nodiscard]] MusicDescriptors result() const;

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double run(double x) noexcept {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    void endHop();
    void endBlock();

    double sampleRate_ = 48000.0;
    long long samples_ = 0;
    // Stéréo, crêtes.
    double sLL_ = 0, sRR_ = 0, sLR_ = 0, lLL_ = 0, lRR_ = 0, lLR_ = 0, sMono_ = 0;
    double peak_ = 0.0;
    long long clipped_ = 0;
    std::array<Biquad, 2> lowL_{}, lowR_{};
    // Micro-dynamique : 4 zones, trames de 10 ms, fenêtres de 400 ms.
    std::array<std::array<Biquad, 2>, 4> zone_{};
    std::array<double, 4> hopAcc_{};
    std::array<std::vector<double>, 4> window10_; ///< énergies des 40 trames de la fenêtre
    std::array<std::vector<double>, 4> micro_;    ///< (crête 10 ms − niveau 400 ms) par fenêtre
    std::array<std::vector<double>, 4> windowLevel_;
    int hopSize_ = 480, hopPos_ = 0, hopsInWindow_ = 0;
    double fullAcc_ = 0.0;
    std::vector<double> envelope_; ///< énergie pleine bande par trame de 10 ms (dB)
    std::array<std::vector<double>, 4> zoneEnv_; ///< énergie de chaque zone par trame de 10 ms (dB)
    // Spectre : blocs de 8192.
    dsp::Fft fft_;
    int blockSize_ = 8192, blockPos_ = 0;
    std::vector<double> blockMid_, window_;
    std::vector<std::complex<double>> work_;
    std::array<double, 12> chroma_{};
    std::vector<double> subHist_; ///< 1 Hz par case, 30 … 110 Hz
    double centroidNum_ = 0.0, centroidDen_ = 0.0;
    std::vector<double> blockPower_;
    // Sonie court terme.
    LoudnessMeter loudness_;
    std::vector<double> shortTerm_;
};

/// Un fichier entier (outils).
MusicDescriptors analyzeMusic(const double* left, const double* right, int numFrames, double sampleRate);

} // namespace omnis::analysis
