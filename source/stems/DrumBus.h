// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/DrumBus.h — Ce qu'il y a dans un bus de drums : kick, snare / clap,
//  hi-hat, open hat, reconnus à leurs coups (thread du lien, jamais l'audio).
//
//  Le son sec du bus est séparé en quatre zones (filtres du deuxième ordre) :
//  grave du kick (< 120 Hz), corps de la snare (~250 Hz), claquement
//  (~2,5 kHz), cymbales (> 7 kHz). Par trames de ~5 ms, un coup dans une zone,
//  c'est son énergie qui monte d'au moins 6 dB au-dessus des 40 ms d'avant,
//  à moins de 30 dB du plus fort entendu. À chaque coup :
//    - kick       : coup dans le grave, le grave domine le claquement de 6 dB ;
//    - snare/clap : coup dans le claquement, avec du corps, sans grave dominant ;
//    - hat        : coup dans les cymbales seulement ; « open » s'il met plus de
//                   150 ms à retomber de 15 dB.
//  Un élément est présent après 4 coups. Ce qui est reconnu règle la chaîne
//  du bus (stems/StemSettings, adaptDrumBus) et s'affiche par son nom.
// =============================================================================
#pragma once

#include <array>
#include <string>
#include <vector>

namespace omnis::stems {

struct DrumContent {
    struct Item {
        int hits = 0;
        bool present = false;
    };
    Item kick, snare, hat, openHat;
    double seconds = 0.0;
    [[nodiscard]] bool any() const noexcept { return kick.present || snare.present || hat.present || openHat.present; }
};

/// Ce que la chaîne du bus fait de chaque élément, une ligne par élément (non temps réel).
std::vector<std::string> describeDrumBus(const DrumContent& content);

class DrumBusAnalyzer {
public:
    void prepare(double sampleRate);
    void reset();
    void push(const double* left, const double* right, int numFrames);
    [[nodiscard]] const DrumContent& content() const noexcept { return content_; }

    static constexpr int kMinHits = 4;

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
    enum Band { kLow = 0, kBody = 1, kCrack = 2, kHats = 3, kBands = 4 };
    void endFrame();

    double sampleRate_ = 48000.0;
    int frameSize_ = 256, framePos_ = 0;
    std::array<Biquad, kBands> filters_{};
    std::array<double, kBands> acc_{}, peakDb_{}, holdUntil_{};
    std::array<std::array<double, 8>, kBands> history_{};
    int historyPos_ = 0;
    long frame_ = 0;
    // Retombée du dernier coup de hat : open ou fermé.
    bool hatPending_ = false;
    double hatOnsetDb_ = 0.0;
    long hatOnsetFrame_ = 0;
    int eventFrames_ = -1; ///< coup en cours de jugement (−1 : aucun)
    long eventFrame_ = 0;
    std::array<double, kBands> eventMax_{}, eventBase_{};
    DrumContent content_;
};

} // namespace omnis::stems
