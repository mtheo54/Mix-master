// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/ElementClassifier.h — Reconnaître ce que joue une piste en l'écoutant.
//
//  Les noms des sons de packs ne disent rien (« JAYY_SPLASH_03 », « Cmin_Ghost »)
//  et certains sons ont des fréquences « bizarres » ou une distorsion voulue :
//  la reconnaissance se fait sur ce que le son FAIT, pas sur ce qu'il s'appelle.
//
//  Trames de ~46 ms (pas ~12 ms) : énergie dans six bandes (20–60, 60–150,
//  150–400, 400–2000, 2000–6000, 6000–20000 Hz), planéité spectrale (0 : note,
//  1 : bruit), largeur stéréo (Side / Mid), crête et carré moyen. Puis, sur
//  tout ce qui a été écouté :
//    - les attaques (flux spectral positif) et, pour chaque coup, sa décroissance
//      de 20 dB, sa répartition en bandes à l'attaque et dans le corps ;
//    - un score par élément, à partir de règles lisibles (un kick : des coups,
//      du grave à l'attaque, une décroissance courte ; un open hat : de l'aigu
//      qui tient plus de 150 ms…). Les seuils sont des points de départ, à
//      calibrer sur de vrais stems.
//  Le nom de la piste précise ensuite l'écoute à l'intérieur d'une famille ; en
//  cas de désaccord franc entre famille entendue et nom, l'écoute gagne et le
//  désaccord est signalé.
//
//  Non temps réel : nourri par le thread du lien (copie de l'audio du stem).
// =============================================================================
#pragma once

#include "Element.h"

#include "dsp/Fft.h"

#include <array>
#include <complex>
#include <deque>
#include <string_view>
#include <vector>

namespace omnis::stems {

inline constexpr int kAnalysisBands = 6;

/// Ce que l'écoute a mesuré : sert à reconnaître, puis à régler la chaîne sans
/// trahir le caractère du son (une crête basse sur un kick saturé est voulue).
struct Character {
    double crestDb = 0.0;         ///< crête / RMS médian des coups (ou de tout le son)
    double noisiness = 0.0;       ///< planéité spectrale médiane, 0 note … 1 bruit
    double decayMs = 0.0;         ///< décroissance médiane de 20 dB après un coup
    double attackMs = 0.0;        ///< montée médiane, du coup à son maximum
    double onsetsPerSecond = 0.0; ///< coups par seconde de son
    double sideToMidDb = -120.0;  ///< largeur stéréo
    double brightnessHz = 0.0;    ///< centre de gravité spectral
    std::array<double, kAnalysisBands> bandShare{}; ///< part de l'énergie par bande (somme 1)
};

struct Identification {
    Element element = Element::Auto; ///< résultat final (Auto : rien d'entendu ni de nommé)
    Element heard = Element::Auto;   ///< ce que dit l'écoute seule
    double confidence = 0.0;         ///< écart entre le premier et le deuxième score (0 … 1)
    NameHint name;
    bool nameDisagrees = false;      ///< famille du nom ≠ famille entendue (l'écoute a gagné)
    bool manual = false;             ///< choisi par la personne
    double secondsHeard = 0.0;
    Character character;
    std::array<double, kElementCount> scores{};
};

class ElementClassifier {
public:
    /// Écoute minimale avant de croire l'analyse (en secondes de son).
    static constexpr double kMinSeconds = 1.5;
    /// Trames gardées : les ~3 dernières minutes.
    static constexpr std::size_t kMaxFrames = 16384;

    void prepare(double sampleRate);
    void reset();
    void push(const double* left, const double* right, int numFrames);

    [[nodiscard]] double secondsHeard() const noexcept;
    /// `manual` : choix de la personne (Auto : laisser reconnaître).
    [[nodiscard]] Identification identify(Element manual, std::string_view trackName) const;

private:
    struct Frame {
        std::array<double, kAnalysisBands> band{};
        double total = 0.0;
        double flatness = 0.0;
        double mid = 0.0, side = 0.0; ///< énergies temporelles du pas
        double peak = 0.0, meanSquare = 0.0;
        double centroid = 0.0;
    };
    void analyseFrame();
    [[nodiscard]] Character measure(std::vector<double>& eventAttackLow, std::vector<double>& eventAttackHigh) const;

    double sampleRate_ = 48000.0;
    int size_ = 2048, hop_ = 512, filled_ = 0, sinceHop_ = 0, pos_ = 0;
    dsp::Fft fft_;
    std::vector<double> window_, mono_, side_, power_;
    std::vector<std::complex<double>> work_;
    std::array<int, kAnalysisBands + 1> edges_{};
    int flatFrom_ = 0, flatTo_ = 0;
    std::deque<Frame> frames_;
    std::size_t soundFrames_ = 0; ///< trames non silencieuses écoutées (toutes, même oubliées)
};

/// Décision à partir des scores de l'écoute et du nom (exposée pour les tests).
Identification combine(const std::array<double, kElementCount>& heardScores, const NameHint& name, Element manual,
                       double secondsHeard);

} // namespace omnis::stems
