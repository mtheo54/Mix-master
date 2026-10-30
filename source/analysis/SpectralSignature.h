// =============================================================================
//  Abrasion Labs // OMNIS
//  analysis/SpectralSignature.h — Ce qui fait durablement le son d'un mix.
//
//  « Écouter le titre » et en garder les éléments PERSISTANTS, pas l'instant :
//    - balance tonale large, Mid et Side séparés, aux nœuds du Match EQ
//      (demi-octave, 20 Hz – 20 kHz) ;
//    - dynamique de chaque bande (écart P90 − P10 de son niveau dans le temps :
//      une 808 compressée « tient », une 808 brute varie de 15 dB) ;
//    - sonie intégrée, LRA, true peak, PLR.
//
//  Ce qui est volontairement ignoré : les détails fins du spectre. Ils
//  dépendent des notes et de la tonalité du morceau (une 808 en Fa et une 808
//  en La n'ont pas leur fondamentale au même endroit), pas des choix de mix.
//  D'où des bandes d'une demi-octave à support COMPACT (poids cos² en
//  log-fréquence, ±1 nœud, somme des poids = 1 sur chaque bin) : l'énergie
//  d'une note se répartit entre ses deux nœuds voisins sans fuir plus loin.
//  Un lissage gaussien avait été essayé d'abord : ses queues faisaient varier
//  le nœud 113 Hz de 5.8 dB entre une 808 à 41 et à 49 Hz (mesuré). Sous
//  80 Hz, seule l'ÉNERGIE totale du sub est persistante d'un titre à l'autre
//  (voir subEnergyDb et ai/MatchIntent).
//
//  Niveau d'un nœud = puissance par octave (densité × f) : un bruit rose est
//  plat. Trames de 0.37 s (Hann, recouvrement 50 %). Seul le CŒUR du morceau
//  est retenu : les trames à plus de 10 dB sous le 95e centile (intros,
//  breaks, fins calmes, silences) sont exclues — c'est sur ses passages
//  pleins qu'un master se juge. (Une porte à −20 dB laissait entrer les
//  intros.)
//
//  Non temps réel (alloue) : analyse d'une référence ou d'une prod complète,
//  sur un thread de calcul dans le plug-in. SignatureAccumulator fait la même
//  analyse au fil de l'eau (bloc par bloc, pendant la lecture dans le DAW) ;
//  analyzeSignature() n'en est que l'appel en une fois : résultats identiques.
// =============================================================================
#pragma once

#include "LoudnessMeter.h"
#include "MusicDescriptors.h"
#include "TruePeakMeter.h"

#include "dsp/Fft.h"
#include "params/Parameters.h"

#include <array>
#include <complex>
#include <vector>

namespace omnis::analysis {

using BandArray = std::array<double, params::kMatchBands>;

struct SpectralSignature {
    BandArray midDb{};      ///< niveau Mid par nœud (dB, puissance par octave)
    BandArray sideDb{};     ///< niveau Side par nœud
    BandArray midSpreadDb{}; ///< dynamique du Mid par nœud (P90 − P10 des trames, dB)
    double subEnergyDb = -200.0; ///< énergie Mid totale des nœuds ≤ 80 Hz (indépendante de la note)
    double integratedLufs = -70.0;
    double loudnessRangeLu = 0.0;
    double truePeakDb = -200.0;
    double plrDb = 0.0; ///< true peak − sonie intégrée
    int activeFrames = 0;
    /// Nœuds dignes de confiance, du grave vers l'aigu : un fichier compressé
    /// (MP3 faible, YouTube) est coupé vers 16 kHz ; son nœud 20 kHz décrit
    /// l'encodeur, pas le mix (voir detectCutoff).
    int trustedBands = params::kMatchBands;
    /// Fiche complète (tempo, tonalité, 808, sections, punch, stéréo…) :
    /// `music.valid` faux pour une fiche mesurée avant cette version.
    MusicDescriptors music;

    /// Coupure d'encodeur : le nœud 20 kHz plus de 20 dB sous celui de 10 kHz
    /// (un master sans perte : −9 à −13 dB, même les plus sombres > −20 ;
    /// un fichier coupé à 16 kHz : −25 à −38 dB, mesuré sur 97 titres). Le
    /// 14 kHz suit au-delà de 12 dB de chute.
    void detectCutoff() noexcept;
    [[nodiscard]] bool trusts(int band) const noexcept { return band < trustedBands; }

    /// Niveau du Mid relatif à son « corps » (moyenne des nœuds 200 Hz – 5 kHz).
    [[nodiscard]] double relativeMidDb(int band) const noexcept;
    [[nodiscard]] double bodyDb() const noexcept;
    static constexpr int kSubBands = 5; ///< nœuds 20 … 80 Hz
    /// Largeur : Side − Mid au nœud (dB).
    [[nodiscard]] double widthDb(int band) const noexcept { return sideDb[static_cast<std::size_t>(band)] - midDb[static_cast<std::size_t>(band)]; }
};

/// Analyse au fil de l'eau. Non temps réel : push() alloue quand le nombre de
/// trames grandit (à appeler depuis un thread d'analyse, jamais le thread audio).
class SignatureAccumulator {
public:
    /// Porte du cœur du morceau : trames à moins de 10 dB du 95e centile.
    static constexpr double kCoreGateDb = 10.0;

    void prepare(double sampleRate);
    void reset();
    void push(const double* left, const double* right, int numFrames);

    [[nodiscard]] double secondsAnalysed() const noexcept { return sampleRate_ > 0.0 ? static_cast<double>(samples_) / sampleRate_ : 0.0; }
    [[nodiscard]] double integratedLufs() const noexcept { return loudness_.integratedLufs(); }
    [[nodiscard]] SpectralSignature result() const;

    /// Un fichier entier, réparti sur `threads` threads (0 : tous les cœurs).
    /// Trames indépendantes et crête vraie découpée en segments amorcés par
    /// les 47 échantillons précédents : résultat identique au bit près à
    /// l'analyse séquentielle (testé).
    static SpectralSignature analyzeParallel(const double* left, const double* right, int numFrames, double sampleRate,
                                             unsigned threads = 0);

private:
    struct NodeWeights {
        int firstBin = 0;
        std::vector<double> weight; ///< déjà multiplié par f (puissance par octave)
        double norm = 0.0;
    };
    struct Frame {
        BandArray mid{}, side{};
        double total = 0.0;
    };
    struct Workspace {
        std::vector<std::complex<double>> buf;
        std::vector<double> pm, ps;
    };
    void analyseFrame(const double* left, const double* right, Workspace& ws, Frame& out) const;

    double sampleRate_ = 0.0;
    int n_ = 0;
    long long samples_ = 0;
    dsp::Fft fft_;
    std::vector<double> window_, pendingL_, pendingR_;
    int filled_ = 0;
    Workspace workspace_;
    double parallelTruePeak_ = -1.0; ///< crête vraie calculée hors de peaks_ (analyse parallèle)
    std::array<NodeWeights, params::kMatchBands> nodes_{};
    std::vector<Frame> frames_;
    LoudnessMeter loudness_;
    TruePeakMeter peaks_;
};

SpectralSignature analyzeSignature(const double* left, const double* right, int numFrames, double sampleRate);

} // namespace omnis::analysis
