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
#include <vector>

namespace omnis::tools {

/// Choix du décodeur d'après l'extension (.wav, .mp3, .flac ; casse ignorée).
/// Chemin natif : sous Windows, tout nom de fichier s'ouvre, quelle que soit la page de code.
bool readAudioFile(const std::filesystem::path& path, AudioFile& out, std::string& error);

/// Extensions lisibles (pour le parcours de dossiers).
bool isReadableAudio(const std::filesystem::path& path);

/// Une image intégrée à un FLAC (bloc METADATA_BLOCK_PICTURE) : ses octets (JPG /
/// PNG) et son type (3 : pochette avant, 8 : artiste, …).
struct EmbeddedPicture {
    std::string bytes;
    unsigned type = 0;
};
/// Les images d'un fichier FLAC (aucune pour un autre format).
std::vector<EmbeddedPicture> readEmbeddedPictures(const std::filesystem::path& path);
std::vector<EmbeddedPicture> readFlacPictures(const unsigned char* data, std::size_t size);

} // namespace omnis::tools
