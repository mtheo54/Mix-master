// Implémentation de stb_image / stb_image_write (code tiers, compilé sans nos
// avertissements stricts). Chemins UTF-8 sous Windows (accents des dossiers).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include "stb_image_write.h"
