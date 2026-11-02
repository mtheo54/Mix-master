// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/ListenSim.h — Écoutes simulées : entendre le master comme sur des
//  écouteurs, un téléphone, TikTok, une voiture, un club, une plateforme
//  (normalisation de la sonie) ou un rendu spatial binaural.
//
//  ÉCOUTE SEULEMENT : appliqué après les mesures de la sortie, jamais en rendu
//  hors ligne (export), et le paramètre n'est pas gardé dans la session.
//
//  Les courbes des écouteurs sont des APPROXIMATIONS d'OMNIS (quelques filtres
//  par modèle : bas, médiums, aigus) établies d'après leurs caractéristiques
//  publiées ; ce ne sont pas des mesures et aucun fabricant n'y est associé.
//  Les marques ne servent qu'à désigner les modèles. Les cibles de sonie des
//  plateformes sont les valeurs couramment rapportées (elles peuvent changer).
//
//  Spatial binaural : deux haut-parleurs virtuels à ±30° (retard et ombre de
//  la tête sur l'oreille opposée, premières réflexions d'une pièce) — une
//  approximation générique, ni Dolby Atmos ni le rendu d'une marque.
// =============================================================================
#pragma once

#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace omnis {

class ListenSim {
public:
    /// Les écoutes, dans l'ordre du paramètre « listen ».
    enum Preset : int {
        Off = 0,
        AirPods,        ///< ouverts : peu de sub
        AirPodsPro,
        AirPodsMax,
        BeatsStudioPro, ///< grave appuyé
        SonyXm5,        ///< chaud
        GalaxyBuds,
        JblTune,        ///< entrée de gamme : grave gonflé, aigus durs
        Hd600,          ///< studio, quasi neutre
        AtM50x,         ///< studio, aigus vifs
        Phone,          ///< haut-parleur de téléphone (mono)
        TikTok,         ///< téléphone + normalisation + légère spatialisation
        Car,
        Club,
        Spatial,        ///< binaural, haut-parleurs virtuels
        Spotify,        ///< −14 LUFS
        Deezer,         ///< −15 LUFS
        YouTube,        ///< −14 LUFS
        AppleMusic,     ///< −16 LUFS
        Count
    };
    static std::span<const std::string_view> labels() noexcept;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void setPreset(int preset) noexcept;
    [[nodiscard]] int preset() const noexcept { return wanted_; }

    /// `outputLufs` : sonie intégrée de la sortie (normalisation des plateformes).
    void process(double* const* channels, int numSamples, double outputLufs) noexcept;

    /// Gain de normalisation appliqué (dB, ≤ 0) : affichage.
    [[nodiscard]] double normalizationDb() const noexcept { return normDb_; }

    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<double, 2> z1{}, z2{};
        double run(double x, int ch) noexcept {
            const double y = b0 * x + z1[static_cast<std::size_t>(ch)];
            z1[static_cast<std::size_t>(ch)] = b1 * x - a1 * y + z2[static_cast<std::size_t>(ch)];
            z2[static_cast<std::size_t>(ch)] = b2 * x - a2 * y;
            return y;
        }
    };
    enum class Filter { Peak, LowShelf, HighShelf, HighPass, LowPass };
    static Biquad design(Filter type, double sampleRate, double freq, double gainDb, double q) noexcept;
    /// Réponse (dB) d'un préréglage à `freq` : tests et courbe affichée.
    static double responseDb(int preset, double sampleRate, double freq) noexcept;

private:
    void build(int preset) noexcept;

    double sampleRate_ = 48000.0;
    int wanted_ = Off, active_ = Off;
    std::vector<Biquad> filters_;
    bool mono_ = false, sideLowCut_ = false;
    int spatial_ = 0; ///< 0 non, 1 léger (TikTok), 2 binaural
    double targetLufs_ = 0.0; ///< 0 : pas de normalisation
    double normDb_ = 0.0, normGain_ = 1.0;
    double fade_ = 1.0, fadeStep_ = 0.0; ///< fondu au changement d'écoute (pas de clic)
    bool switching_ = false;
    // Spatial : lignes à retard (30 ms), ombre de la tête (passe-bas 1 pôle).
    std::array<std::vector<double>, 2> ring_;
    int ringPos_ = 0, itd_ = 1;
    std::array<int, 4> taps_{};
    std::array<double, 2> shadow_{};
    double shadowCoeff_ = 0.0;
    Biquad sideHp_; ///< club : le Side sans grave (sub mono)
};

} // namespace omnis
