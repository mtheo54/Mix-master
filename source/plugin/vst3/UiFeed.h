// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/UiFeed.h — Ce que le processeur montre à l'interface, sans
//  passer par les paramètres de sortie (trop lents et trop maigres pour un
//  spectre ou un goniomètre).
//
//  Le processeur et le contrôleur d'une instance vivent dans le même processus
//  (le module du plug-in est chargé une fois) : le processeur inscrit son flux
//  dans un registre sous un jeton, et donne le jeton au contrôleur par message.
//  Le flux est partagé (shared_ptr) : il survit à celui des deux qui part en
//  premier.
//    - thread audio (un seul rédacteur) : mesures des modules ~30 fois par
//      seconde (triple tampon), échantillons de sortie pour le spectre et le
//      goniomètre (file SPSC, perdus si personne ne lit) ;
//    - thread de l'interface (un seul lecteur) : la dernière photo et les
//      échantillons arrivés depuis la dernière lecture.
//  L'interface lève `wanted` tant qu'elle est ouverte : fermée, le processeur
//  ne copie rien.
// =============================================================================
#pragma once

#include "core/SpscRingBuffer.h"
#include "core/TripleBuffer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace omnis::vst3 {

struct UiMeters {
    float inPeakDb[2] = {-120.0f, -120.0f};  ///< entrée, crête du dernier intervalle
    float outPeakDb[2] = {-120.0f, -120.0f}; ///< sortie
    float limiterGrDb = 0.0f;                ///< <= 0
    float clipperGrDb = 0.0f;
    float multibandGrDb[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float gainMatchDb = 0.0f;                ///< compensation de Gain Match
    float latencyMs = 0.0f;
    float samplePeakMaxDb = -120.0f;
    std::uint32_t sequence = 0;
};

class UiFeed {
public:
    UiFeed() { scope.allocate(std::size_t{1} << 15); }
    TripleBuffer<UiMeters> meters;
    SpscRingBuffer<float> scope; ///< sortie entrelacée L, R
    std::atomic<bool> wanted{false};
    std::atomic<double> sampleRate{48000.0};
};

/// Registre du processus (non temps réel, protégé par un verrou).
std::uint64_t registerFeed(std::shared_ptr<UiFeed> feed);
void unregisterFeed(std::uint64_t token);
std::shared_ptr<UiFeed> findFeed(std::uint64_t token);

} // namespace omnis::vst3
