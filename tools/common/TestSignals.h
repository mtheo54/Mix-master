// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/TestSignals.h — Générateurs de signaux de mesure.
//
//  Chaque signal cible une propriété précise d'un étage DSP :
//
//   sweep          Sinus exponentiel (Farina) + filtre inverse : réponse
//                  impulsionnelle, réponse en fréquence/phase, distorsion par
//                  harmonique (les produits d'ordre k apparaissent AVANT l'IR
//                  linéaire après déconvolution, à Δt = L·ln k).
//   pink / white   Bruit rose (Kellet, -3 dB/oct) / blanc gaussien : réponse
//                  moyenne, calibrage des crossovers, comportement RMS.
//   impulse        Dirac : réponse impulsionnelle directe, latence, pré-ringing
//                  des FIR à phase linéaire.
//   steps          Sinus 1 kHz à paliers de niveau : temps d'attaque/release
//                  et ratio statique des compresseurs (méthode IEC 60268).
//   drums          Boucle trap synthétique 140 BPM (808 glissée longue, kick
//                  click, clap, hi-hats) : banc réaliste pour clipper/limiteur.
//   isp            Sinus fs/4 déphasé de 45° : crête inter-échantillon de
//                  +3.01 dB au-dessus du sample peak (test du True Peak).
//   imd-smpte      60 Hz + 7 kHz (4:1) : intermodulation basses fréquences.
//   imd-ccif       19 kHz + 20 kHz : intermodulation et repliement (aliasing)
//                  des saturations / clippers -> produit à 1 kHz et repliements.
//   hf-sweep       Sweep 1–20 kHz à fort niveau : cartographie de l'aliasing.
//   ebu3341-N      Signaux de conformité EBU Tech 3341 (cas 1 à 5).
//   stereo         Bruit rose mono / anti-phase / décorrélé : corrélateur,
//                  Mono Maker, image M/S.
// =============================================================================
#pragma once

#include "WavFile.h"

#include <cstdint>
#include <string>
#include <vector>

namespace omnis::tools::signals {

struct SweepResult {
    AudioFile sweep;
    AudioFile inverse; ///< convolution sweep ⊗ inverse ≈ Dirac (gain unitaire à 1 kHz)
};

SweepResult logSweep(double sampleRate, double seconds, double f1, double f2, double levelDb, double fadeMs = 50.0);
AudioFile pinkNoise(double sampleRate, double seconds, double rmsDb, std::uint64_t seed = 1, bool stereoDecorrelated = true);
AudioFile whiteNoise(double sampleRate, double seconds, double rmsDb, std::uint64_t seed = 2);
AudioFile impulse(double sampleRate, double seconds, double levelDb, double positionSeconds = 0.5);
AudioFile levelSteps(double sampleRate, double frequency, double lowDb, double highDb, double lowSeconds,
                     double highSeconds, int repetitions);
AudioFile drumLoop(double sampleRate, int bars, double bpm = 140.0, double peakDb = -1.0, std::uint64_t seed = 3);
AudioFile intersamplePeak(double sampleRate, double seconds, double truePeakDb);
AudioFile twoTone(double sampleRate, double seconds, double f1, double f2, double ratio, double peakDb);
AudioFile ebu3341(double sampleRate, int caseNumber);
AudioFile stereoImaging(double sampleRate, double secondsPerPart, double rmsDb, std::uint64_t seed = 4);

/// Applique des fondus cosinus en entrée et sortie.
void applyFades(AudioFile& file, double fadeInMs, double fadeOutMs);

/// Liste des noms de signaux reconnus par generate().
std::vector<std::string> availableSignals();

/// Génère un signal par nom avec les réglages par défaut du kit de test.
bool generate(const std::string& name, double sampleRate, AudioFile& out, std::string& description);

} // namespace omnis::tools::signals
