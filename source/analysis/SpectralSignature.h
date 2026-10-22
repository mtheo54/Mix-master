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
//  plat. Trames de 0.37 s (Hann, recouvrement 50 %) ; les silences, intros
//  et coupures sont exclus (trames à plus de 20 dB sous le 95e centile).
//
//  Non temps réel (alloue) : analyse d'une référence ou d'une prod complète,
//  sur un thread de calcul dans le plug-in.
// =============================================================================
#pragma once

#include "params/Parameters.h"

#include <array>

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

    /// Niveau du Mid relatif à son « corps » (moyenne des nœuds 200 Hz – 5 kHz).
    [[nodiscard]] double relativeMidDb(int band) const noexcept;
    [[nodiscard]] double bodyDb() const noexcept;
    static constexpr int kSubBands = 5; ///< nœuds 20 … 80 Hz
    /// Largeur : Side − Mid au nœud (dB).
    [[nodiscard]] double widthDb(int band) const noexcept { return sideDb[static_cast<std::size_t>(band)] - midDb[static_cast<std::size_t>(band)]; }
};

SpectralSignature analyzeSignature(const double* left, const double* right, int numFrames, double sampleRate);

} // namespace omnis::analysis
