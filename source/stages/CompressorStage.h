// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/CompressorStage.h — Compresseur de bus à caractères (« colle »).
//
//  Un compresseur large bande, sans anticipation ni latence, lié en stéréo
//  (le même gain sur L et R : l'image ne bouge pas). Le détecteur écoute la
//  prod passée par un passe-haut (« SC HPF », 90 Hz par défaut) : la 808 ne
//  fait pas pomper tout le mix.
//
//  Courbe statique à genou doux, en dB (x : niveau détecté, T seuil, R taux,
//  W largeur du genou) :
//      x < T − W/2          gr = 0
//      |x − T| ≤ W/2        gr = (1/R − 1)·(x − T + W/2)² / (2W)
//      x > T + W/2          gr = (1/R − 1)·(x − T)
//  puis balistique sur gr (attaque quand la réduction s'accroît, relâchement
//  sinon), gain = gr + compensation, mélange parallèle (« Mix »).
//
//  Caractères (principes reformulés, aucun nom de marque) :
//    Opto     RMS 10 ms, genou 10 dB ; relâchement qui dépend du programme :
//             rapide après un coup bref, lent quand la compression dure ;
//    FET      détection crête (maintenue, redescend en 10 ms), genou 3 dB :
//             rapide et mordant ;
//    VCA      RMS 5 ms, genou 6 dB : la colle propre du bus ;
//    Vari-mu  RMS 20 ms, genou 18 dB (le taux monte avec la réduction),
//             relâchement plus lent quand la réduction est profonde : arrondi.
//  Changer de caractère ne claque pas : la réduction suit sa balistique.
//  Éteint (module OFF, défaut) : l'étage n'est pas appelé (bypass exact).
// =============================================================================
#pragma once

#include "core/OnePoleSmoother.h"
#include "engine/Stage.h"

#include <array>

namespace omnis {

/// Courbe statique (sans état) : exposée pour les tests.
struct CompressorCurve {
    /// Réduction de gain (dB, ≤ 0) pour un niveau détecté x (dB).
    static double gainReductionDb(double x, double threshold, double ratio, double knee) noexcept;
    /// Largeur du genou (dB) du caractère (params::CompCharacter).
    static double kneeOf(int character) noexcept;
};

class CompressorStage final : public Stage {
public:
    CompressorStage();

    [[nodiscard]] const char* name() const noexcept override { return "Comp"; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(double* const* channels, int numSamples) noexcept override;
    void setParameter(params::ParamId id, double plainValue, bool immediate) noexcept override;

    /// Réduction de gain la plus forte du dernier bloc (dB, ≤ 0) : afficheur.
    [[nodiscard]] double gainReductionDb() const noexcept { return lastGrDb_; }

private:
    void updateTimes() noexcept;
    void updateSidechainFilter() noexcept;

    double sampleRate_ = 48000.0;
    int character_ = 2;
    double attackMs_ = 10.0, releaseMs_ = 200.0, scHz_ = 90.0;
    bool scDirty_ = true;
    double attackCoef_ = 0.0, releaseCoef_ = 0.0, rmsCoef_ = 0.0, holdCoef_ = 0.0, heldDecayCoef_ = 0.0, peakCoef_ = 0.0;

    OnePoleSmoother threshold_, ratio_, makeup_, mix_;

    // Passe-haut du détecteur (biquad Butterworth, forme directe transposée II), L et R.
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0, a1_ = 0.0, a2_ = 0.0;
    std::array<double, 2> z1_{}, z2_{};

    double meanSquare_ = 0.0; ///< détecteur RMS (puissance moyenne lissée)
    double peakHold_ = 0.0;   ///< détecteur crête (FET)
    double envDb_ = 0.0;      ///< réduction de gain courante (dB, ≤ 0)
    double heldDb_ = 0.0;     ///< Opto : réduction moyenne sur ~1 s (« la compression dure »)
    double lastGrDb_ = 0.0;
};

} // namespace omnis
