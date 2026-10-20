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
//                la plage d'identifiants de l'étage ; l'étage lisse lui-même.
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
};

class Stage {
public:
    virtual ~Stage() = default;

    [[nodiscard]] virtual const char* name() const noexcept = 0;

    virtual void prepare(const ProcessSpec& spec) = 0;
    virtual void reset() noexcept = 0;
    virtual void process(double* const* channels, int numSamples) noexcept = 0;

    [[nodiscard]] virtual int latencySamples() const noexcept { return 0; }

    virtual void setParameter(params::ParamId /*id*/, double /*plainValue*/) noexcept {}
};

} // namespace omnis
