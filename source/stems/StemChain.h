// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/StemChain.h — La chaîne d'un stem, sans aucune latence.
//
//  Dans l'ordre (chaque étage n'agit que si ses réglages le demandent) :
//    1. trim ;
//    2. passe-haut SVF, 12 ou 24 dB/oct (deux sections Butterworth ; en
//       12 dB/oct la seconde descend à 5 Hz : pas de changement de topologie,
//       on glisse d'une pente à l'autre) ;
//    3. EQ dynamique, 3 bandes : détecteur passe-bande sur le Mid, son énergie
//       rapportée à celle du reste de la piste (mêmes temps d'enveloppe,
//       5 / 80 ms), comparée à l'équilibre d'un bruit rose + marge. Coupe 2:1
//       au-delà, en cloche SVF recalculée toutes les 16 échantillons ;
//    4. compresseur lié en stéréo, sur le niveau de crête (relâche 25 ms) :
//       seuil = crête récente (relâche 3 s) −
//       profondeur, genou 6 dB, attaque / relâche sur la réduction, gain de
//       compensation = réduction moyenne (2 s) : le niveau reste celui de la
//       piste, seules les crêtes descendent ;
//    5. saturation, courbe de l'Inflator (exposant 3) rapportée à la crête
//       récente P : y = x + dose·[0.5·x + P·h(x/P)], h(u) = −u³/2 (|u| < 1).
//       Le bas monte (+3,5 dB au plus, ×dose), les crêtes restent en place
//       dans le grave (kick, 808 : g(1) = 1). Seule la partie non linéaire
//       passe en 2x (demi-bande HIIR à phase minimale) : un cube ne crée que
//       l'harmonique 3, qui à 2x se replie au-dessus de 24 kHz et est retiré à
//       la descente — sans repliement audible, et le son direct n'a aucun
//       retard. Revers : dans le médium et l'aigu, l'harmonique arrive avec le
//       petit retard du demi-bande et la crête peut monter (+1 dB environ à
//       1 kHz plein niveau, dose maximale) ;
//    6. M/S : Side séparé en Linkwitz-Riley 24 dB/oct à `monoBelowHz` (la
//       somme est un passe-tout : sans latence, un complément exact par
//       soustraction laisserait passer trop de grave) ; le bas réduit de
//       `monoAmount`, le haut × `width`. Le Mid n'est jamais touché.
//
//  Off (réglages inactifs) : la sortie est l'entrée, au bit près. Passage
//  Off <-> actif et changements de réglages lissés (aucun clic).
//  Temps réel : aucune allocation hors prepare().
// =============================================================================
#pragma once

#include "StemSettings.h"

#include "dsp/Halfband.h"
#include "dsp/SvfFilter.h"

#include <array>
#include <vector>

namespace omnis::stems {

class StemChain {
public:
    /// Non temps réel.
    void prepare(double sampleRate);
    void reset() noexcept;

    /// Temps réel : nouveaux réglages (bornés), rejoints en douceur.
    void setSettings(const StemSettings& settings) noexcept;

    /// Temps réel, en place, n quelconque.
    void process(double* left, double* right, int numSamples) noexcept;

    [[nodiscard]] static constexpr int latencySamples() noexcept { return 0; }
    /// true tant que la chaîne agit (ou s'efface).
    [[nodiscard]] bool processing() const noexcept { return mix_ > 0.0 || target_.active; }
    /// Réduction actuelle du compresseur (dB, ≥ 0) et coupe de chaque bande dynamique.
    [[nodiscard]] double compressionDb() const noexcept { return grDb_; }
    [[nodiscard]] double dynamicCutDb(int band) const noexcept { return dyn_[static_cast<std::size_t>(band)].cutDb; }
    [[nodiscard]] const StemSettings& settings() const noexcept { return target_; }

    static constexpr int kChunk = 256;
    /// Demi-bande de la saturation : le premier étage de l'Oversampler (bande
    /// passante 0.46·fs, 120 dB).
    static constexpr double kHalfbandTransition = 0.02;
    static constexpr double kHalfbandAttenuationDb = 120.0;
    static constexpr int kControlInterval = 16;

private:
    struct Smoothed {
        double trimGain = 1.0;
        double hpf1Hz = 5.0, hpf2Hz = 5.0, hpfQ1 = 0.7071, hpfQ2 = 0.7071;
        std::array<double, kDynamicBands> bandHz{}, bandQ{}, bandAllow{}, bandMaxCut{};
        double ratio = 1.0, depthDb = 0.0;
        double saturation = 0.0;
        double monoHz = 120.0, monoAmount = 0.0, width = 1.0;
    };
    struct DynBand {
        dsp::SvfCoefficients detect{}, bell{};
        dsp::SvfState detectState{};
        std::array<dsp::SvfState, 2> bellState{};
        double envelope = 0.0; ///< puissance du passe-bande
        double cutDb = 0.0, appliedCutDb = 0.0;
        double appliedHz = 0.0, appliedQ = 0.0;
    };

    void control() noexcept;       ///< toutes les kControlInterval échantillons
    void processChunk(double* left, double* right, int n) noexcept;
    void snapToTarget() noexcept;

    double sampleRate_ = 48000.0;
    StemSettings target_{};
    Smoothed cur_{}, goal_{};
    double controlCoef_ = 0.0;     ///< lissage des réglages (≈ 100 ms)
    double mix_ = 0.0, mixStep_ = 0.0;
    int controlCountdown_ = 0;

    // Passe-haut.
    dsp::SvfCoefficients hpf1_{}, hpf2_{};
    std::array<dsp::SvfState, 2> hpf1State_{}, hpf2State_{};
    bool hpfRunning_ = false;

    // EQ dynamique.
    std::array<DynBand, kDynamicBands> dyn_{};
    double totalEnvelope_ = 0.0;
    double envAttack_ = 0.0, envRelease_ = 0.0;

    // Compresseur.
    double grDb_ = 0.0, avgGrDb_ = 0.0, compPeak_ = 0.0, level_ = 0.0;
    double compAttack_ = 0.0, compRelease_ = 0.0, avgCoef_ = 0.0, slowRelease_ = 0.0, levelRelease_ = 0.0;

    // Saturation.
    double satPeak_ = 0.0, satRelease_ = 0.0;
    bool satRunning_ = false;
    std::array<dsp::Halfband2x, 2> halfband_{};
    std::array<std::vector<double>, 2> satUp_, satOut_;
    std::vector<double> peaks_;
    // Valeurs du bloc précédent : rampes à l'échantillon près dans le bloc.
    double prevSaturation_ = 0.0, prevMonoAmount_ = 0.0, prevWidth_ = 1.0;

    // M/S.
    dsp::SvfCoefficients monoLp_{}, monoHp_{};
    std::array<dsp::SvfState, 4> side_{}; ///< passe-bas ×2, passe-haut ×2

    // Son sec (fondu Off <-> actif).
    std::array<std::vector<double>, 2> dry_;
};

} // namespace omnis::stems
