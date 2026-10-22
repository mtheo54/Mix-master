// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/AudioRead.h — Lire un son quel que soit son format.
//
//  WAV (lecteur maison, WavFile.h), MP3 et FLAC (dr_mp3 / dr_flac, domaine
//  public, third_party/dr_libs). Tout est décodé en local, en double
//  précision ; un fichier mono garde un canal (les outils lisent alors le
//  même canal à gauche et à droite).
// =============================================================================
#pragma once

#include "WavFile.h"

#include <string>

namespace omnis::tools {

/// Choix du décodeur d'après l'extension (.wav, .mp3, .flac ; casse ignorée).
bool readAudioFile(const std::string& path, AudioFile& out, std::string& error);

/// Extensions lisibles (pour le parcours de dossiers).
bool isReadableAudio(const std::string& path);

} // namespace omnis::tools
