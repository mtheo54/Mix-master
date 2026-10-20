// =============================================================================
//  Abrasion Labs // OMNIS
//  core/OnePoleSmoother.h — Lissage de paramètres par passe-bas à un pôle.
//
//  Équation aux différences (filtre RC discrétisé par invariance impulsionnelle) :
//
//      y[n] = y[n-1] + a · (x[n] - y[n-1]),        a = 1 - exp(-1 / (τ · fs))
//
//  Réponse à un échelon : y(t) = x · (1 - e^(-t/τ)). Pas de dépassement, pas
//  d'oscillation : idéal contre le « zipper noise » sur gains, seuils, fréquences.
//
//  Convention OMNIS : le temps de lissage T fourni est le temps pour atteindre
//  99 % de la cible (erreur résiduelle -40 dB), d'où τ = T / ln(100) ≈ T / 4.605.
//
//  Quand l'erreur passe sous `snapThreshold`, on colle exactement à la cible :
//  (1) le processeur peut prendre un chemin rapide sans lissage,
//  (2) on évite une longue traîne de valeurs qui convergent sans jamais égaler
//      la cible (essentiel pour les tests de nullité bit-à-bit).
// =============================================================================
#pragma once

#include <cmath>

namespace omnis {

class OnePoleSmoother {
public:
    /// Temps de réponse à 99 % en millisecondes. 0 = pas de lissage.
    void prepare(double sampleRate, double smoothingTimeMs) noexcept {
        if (smoothingTimeMs <= 0.0 || sampleRate <= 0.0) {
            coefficient_ = 1.0;
            return;
        }
        const double tau = (smoothingTimeMs * 1.0e-3) / std::log(100.0);
        coefficient_ = 1.0 - std::exp(-1.0 / (tau * sampleRate));
    }

    void setSnapThreshold(double threshold) noexcept { snapThreshold_ = threshold; }

    void setTarget(double target) noexcept { target_ = target; }

    /// Saut immédiat (initialisation, chargement d'état, reset).
    void snapTo(double value) noexcept {
        target_ = value;
        current_ = value;
    }

    [[nodiscard]] double target() const noexcept { return target_; }
    [[nodiscard]] double current() const noexcept { return current_; }
    [[nodiscard]] bool isSmoothing() const noexcept { return current_ != target_; }

    /// Avance d'un échantillon et renvoie la valeur lissée.
    double next() noexcept {
        if (current_ != target_) {
            current_ += coefficient_ * (target_ - current_);
            if (std::fabs(target_ - current_) < snapThreshold_)
                current_ = target_;
        }
        return current_;
    }

    /// Remplit `dst` avec `numSamples` valeurs lissées consécutives.
    void fill(double* dst, int numSamples) noexcept {
        if (!isSmoothing()) {
            for (int i = 0; i < numSamples; ++i) dst[i] = current_;
            return;
        }
        for (int i = 0; i < numSamples; ++i) dst[i] = next();
    }

private:
    double coefficient_ = 1.0;
    double target_ = 0.0;
    double current_ = 0.0;
    double snapThreshold_ = 1.0e-9; // ~ -180 dB pour un gain linéaire
};

} // namespace omnis
