// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/BusListener.h — Ce qu'il y a dans un bus, à l'écoute.
//
//  OMNIS posé sur un bus (RÔLE : Bus) ne voit que le mélange : le DAW ne dit
//  pas aux plug-ins quelles pistes y entrent. Il reconnaît donc à l'oreille ce
//  que le bus contient, sans rien poser sur les pistes :
//    - DRUMS : kick, snare / clap, hi-hat, open hat (stems/DrumBus, calibré
//      sur un vrai bus de drums) ;
//    - le reste après une séparation « tenu / frappé » (HPSS : médiane de
//      chaque fréquence dans le temps contre médiane de chaque trame dans les
//      fréquences ; ce qui tient dans le temps est harmonique, ce qui s'étale
//      dans les fréquences est percussif, avec une marge de 2 : le reste, un
//      bruit, n'est ni l'un ni l'autre) :
//        808 / BASSE : du grave harmonique qui TIENT (≥ 200 ms par note) ;
//        MÉLO : de l'harmonique dans le médium (300 Hz – 4 kHz) ; une nappe s'il
//               tient sans attaques, un lead / synthé s'il a des attaques ;
//        EFX : les montées de l'aigu (reverse cymbal : monte puis s'arrête net ;
//              riser : monte plus de 2,5 s), les impacts (gros coup large et
//              grave qui traîne plus de 0,7 s).
//  Le résultat dit ce qui a été entendu et à quel point c'est net ; jamais de
//  nom de piste (que le DAW ne donne pas).
//
//  Non temps réel : nourri par le thread du lien (copie du son du bus).
// =============================================================================
#pragma once

#include "DrumBus.h"
#include "Element.h"

#include "dsp/Fft.h"

#include <complex>
#include <deque>
#include <string>
#include <vector>

namespace omnis::stems {

/// Un élément entendu dans le bus.
struct HeardItem {
    Element element = Element::Auto;
    double confidence = 0.0; ///< 0 … 1
    int count = 0;           ///< coups, notes ou montées entendus
    double seconds = 0.0;    ///< durée tenue (808, mélo)
};

struct BusContent {
    std::vector<HeardItem> items; ///< dans l'ordre DRUMS, 808, MÉLO, EFX
    double secondsHeard = 0.0;
    [[nodiscard]] bool has(Element e) const noexcept;
    [[nodiscard]] Family mainFamily() const noexcept; ///< le groupe qui pèse le plus (Unknown : rien)
};

/// « Kick (24 coups) », « 808 (tenue 6 s) », « Nappe », « Reverse cymbal (2) ».
std::string describeItem(const HeardItem& item);

class BusListener {
public:
    /// Écoute minimale avant de croire l'analyse (secondes de son).
    static constexpr double kMinSeconds = 2.0;

    void prepare(double sampleRate);
    void reset();
    void push(const double* left, const double* right, int numFrames);
    [[nodiscard]] double secondsHeard() const noexcept;
    /// Analyse de tout ce qui a été écouté (les ~3 dernières minutes).
    [[nodiscard]] BusContent content() const;

    /// Bandes (exposées pour les tests) : grave 30–120, bas 120–300, médium 300–4000, aigu 4000–16000 Hz.
    enum Band { kSub = 0, kLow = 1, kMid = 2, kHigh = 3, kBands = 4 };

private:
    struct Frame {
        double h[kBands]{}, p[kBands]{}, r[kBands]{}; ///< énergie harmonique / percussive / ni l'un ni l'autre (bruit)
        double total = 0.0;
    };
    void analyse();

    double sampleRate_ = 48000.0;
    int size_ = 2048, hop_ = 512, filled_ = 0, sinceHop_ = 0, pos_ = 0;
    dsp::Fft fft_;
    std::vector<double> window_, mono_;
    std::vector<std::complex<double>> work_;
    int bins_ = 0;
    std::vector<int> bandOf_;                 ///< bande de chaque bin (−1 : hors bandes)
    std::deque<std::vector<float>> history_; ///< spectres de puissance des kTimeMedian dernières trames
    std::deque<Frame> frames_;
    std::size_t soundFrames_ = 0;
    DrumBusAnalyzer drums_;
};

} // namespace omnis::stems
