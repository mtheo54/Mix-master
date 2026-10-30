// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/Cartoon.h — Une image (photo d'artiste, pochette) redessinée en
//  cartoon, sans IA : traitement d'image classique, en local.
//
//    1. lissage qui garde les bords (filtre bilatéral, plusieurs passes) : la
//       peau, le ciel, les fonds deviennent des aplats ;
//    2. ombrage cartoon : chaque zone garde sa couleur (un peu plus vive), sa
//       luminosité tombe sur quelques paliers : des aplats comme un dessin
//       colorié, sans perdre les petits détails (yeux, reflets) ;
//    3. contours à l'encre (différence de gaussiennes sur la luminance, seuil
//       doux) : le trait noir du dessin ;
//    4. les deux réunis.
//
//  Droits : le cartoon reste tiré de l'image de départ. Photo ou pochette
//  Deezer : usage personnel ; dans le plug-in vendu, l'accord de l'artiste /
//  du label est nécessaire (docs/REFERENCES.md).
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace omnis::tools {

/// Image RGB, valeurs 0 … 1, rangée ligne par ligne.
struct RgbImage {
    int width = 0, height = 0;
    std::vector<float> rgb; ///< 3 × width × height
    [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0; }
};

/// JPG ou PNG (chemin UTF-8, accents compris).
bool loadImage(const std::string& path, RgbImage& out, std::string& error);
bool savePng(const std::string& path, const RgbImage& image, std::string& error);

struct CartoonOptions {
    int maxSide = 640;     ///< taille de travail (et de sortie)
    int colors = 10;       ///< finesse de l'ombrage (paliers de luminosité = colors / 2)
    int smoothPasses = 4;  ///< passes du lissage
    float ink = 0.55f;     ///< épaisseur / présence du trait (0 … 1)
    float vivid = 1.25f;   ///< couleurs plus franches
};

RgbImage cartoonize(const RgbImage& in, const CartoonOptions& options = {});

} // namespace omnis::tools
