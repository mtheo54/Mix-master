// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/Stage.h — Interface d'un étage de la chaîne de mastering.
//
//  Chaque module DSP (Sub/Mono Maker, Match EQ, Exciter M/S, Multibande,
//  Clipper, Limiteur, Dither) implémente cette interface. Contrat :
//
//   prepare()  : NON temps réel. Toutes les allocations (FIR, tampons
//                d'oversampling, lookahead) se font ici, dimensionnées pour
//                maxBlockSize. Après prepare(), latencySamples() est valide.
//   reset()    : temps réel. Vide les états internes sans allouer.
//   process()  : temps réel. Traitement en place sur 2 canaux double précision,
//                numSamples <= maxBlockSize. Ni allocation, ni verrou, ni I/O.
//   setParameter() : temps réel. Reçoit la valeur « plain » d'un paramètre de
//                la plage d'identifiants de l'étage ; l'étage lisse lui-même,
//                sauf si `immediate` (chargement d'état, initialisation).
//
//  Latence : elle doit être CONSTANTE pour une fréquence d'échantillonnage
//  donnée (indépendante des réglages). Un étage dont le coût en latence dépend
//  d'un mode (ex. oversampling 8x vs 16x, FIR 4096 vs 16384) doit compléter
//  par un retard pour toujours annoncer son pire cas. Raison : changer la
//  latence en cours de lecture impose un restartComponent(kLatencyChanged),
//  que beaucoup d'hôtes gèrent mal (coupure, réallocation du graphe, décalage
//  momentané). La latence n'étant pas une contrainte pour OMNIS, on paie le
//  pire cas une fois pour toutes.
// =============================================================================
#pragma once

#include "../params/Parameters.h"

namespace omnis {

struct ProcessSpec {
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
    int numChannels = 2;
    bool lowLatency = false; ///< noyaux FIR courts (voir lowLatencyLength)
    /// Avec lowLatency : le mode travail du plug-in (< 5 ms) — noyaux à phase minimale sans retard,
    /// trames de ~0,17 ms, anticipations courtes. Sans : les essais rapides d'AUTO (noyaux courts à
    /// phase linéaire, grandes trames, ~90 ms), plus économes en calcul.
    bool zeroDelay = false;
};

/// Longueur d'un noyau FIR en mode travail : 16 fois plus court, 1024 au minimum
/// (latence de ~11 ms par étage au lieu de 45 à 180 ms ; grave moins précis).
/// Trame du convolueur : N/32 en qualité max ; en mode travail, ~0,17 ms (8 échantillons à
/// 48 kHz) — avec des noyaux sans retard, c'est toute la latence d'un étage FIR.
inline int convolverBlock(int taps, double sampleRate, bool lowLatency) noexcept {
    if (!lowLatency) return taps / 32;
    return sampleRate <= 50000.0 ? 8 : (sampleRate <= 100000.0 ? 16 : 32);
}

inline int lowLatencyLength(int taps, bool lowLatency) noexcept {
    if (!lowLatency) return taps;
    const int shorter = taps / 16;
    return shorter < 1024 ? (taps < 1024 ? taps : 1024) : shorter;
}

class Stage {
public:
    virtual ~Stage() = default;

    [[nodiscard]] virtual const char* name() const noexcept = 0;

    virtual void prepare(const ProcessSpec& spec) = 0;
    virtual void reset() noexcept = 0;
    virtual void process(double* const* channels, int numSamples) noexcept = 0;

    [[nodiscard]] virtual int latencySamples() const noexcept { return 0; }

    virtual void setParameter(params::ParamId /*id*/, double /*plainValue*/, bool /*immediate*/) noexcept {}

    /// Non temps réel, avant prepare(). true : lecture temps réel (calculs lourds
    /// délégués à des threads de fond, résultats appliqués dès qu'ils sont prêts).
    /// false : rendu hors ligne déterministe (tout est calculé immédiatement).
    virtual void setRealtime(bool /*realtime*/) noexcept {}

    /// true : l'étage garantit la crête de sortie (limiteur brickwall).
    [[nodiscard]] virtual bool limitsPeaks() const noexcept { return false; }
    /// Appelé à chaque bloc : `guard` = aucun étage allumé après celui-ci ne
    /// garantit la crête (l'étage est le dernier rempart avant la sortie).
    virtual void setPeakGuard(bool /*guard*/) noexcept {}
};

} // namespace omnis
