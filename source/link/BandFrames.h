// =============================================================================
//  Abrasion Labs // OMNIS
//  link/BandFrames.h — L'empreinte de ce qu'entend une piste ou un bus.
//
//  Six filtres passe-bande (RBJ, gain 1 au centre ; bandes de link/LinkBus.h)
//  sur le Mid, l'énergie de chaque bande sommée par trame de kFrameHop
//  échantillons repérée par la position dans le projet : la trame k couvre
//  les échantillons [k·hop, (k+1)·hop) du projet, pour toutes les instances.
//  Les filtres sont linéaires : l'empreinte d'un bus est (à peu près) la
//  somme pondérée de celles de ses pistes — c'est ce qui permet de les y
//  retrouver (stems/Composition).
//
//  Temps réel : aucune allocation. Une trame n'est publiée que si elle est
//  entière (départ de la lecture, saut de la tête : la trame entamée est jetée).
// =============================================================================
#pragma once

#include "LinkBus.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace omnis::link {

class BandFrames {
public:
    void prepare(double sampleRate) noexcept {
        constexpr double kEdges[kBands + 1] = {20.0, 60.0, 150.0, 400.0, 2000.0, 6000.0, 20000.0};
        const double nyquist = 0.5 * sampleRate;
        for (int b = 0; b < kBands; ++b) {
            const double lo = kEdges[b], hi = std::min(kEdges[b + 1], 0.95 * nyquist);
            const double fc = std::sqrt(lo * hi), q = fc / std::max(1.0, hi - lo);
            const double w = 2.0 * std::numbers::pi * std::min(fc, 0.45 * sampleRate) / sampleRate;
            const double alpha = std::sin(w) / (2.0 * q), a0 = 1.0 + alpha;
            auto& f = filters_[static_cast<std::size_t>(b)];
            f = {};
            f.b0 = alpha / a0;
            f.b2 = -alpha / a0;
            f.a1 = -2.0 * std::cos(w) / a0;
            f.a2 = (1.0 - alpha) / a0;
        }
        reset();
    }
    void reset() noexcept {
        for (auto& f : filters_) f.z1 = f.z2 = 0.0;
        acc_.fill(0.0);
        current_ = -1;
        count_ = 0;
    }

    /// `projectStart` : position de la première trame du bloc (kNoPosition : transport arrêté).
    template <typename Sample, typename Publish>
    void process(const Sample* left, const Sample* right, int numFrames, std::int64_t projectStart, Publish&& publish) noexcept {
        for (int i = 0; i < numFrames; ++i) {
            double x = 0.5 * (static_cast<double>(left[i]) + static_cast<double>(right[i]));
            if (!std::isfinite(x)) x = 0.0;
            std::array<double, kBands> y{};
            for (int b = 0; b < kBands; ++b) y[static_cast<std::size_t>(b)] = filters_[static_cast<std::size_t>(b)].run(x);
            if (projectStart == kNoPosition) {
                current_ = -1;
                count_ = 0;
                continue;
            }
            const std::int64_t pos = projectStart + i;
            const std::int64_t k = pos >= 0 ? pos / kFrameHop : -1;
            if (k != current_) {
                if (current_ >= 0 && count_ == kFrameHop) {
                    std::array<float, kBands> e{};
                    for (int b = 0; b < kBands; ++b) e[static_cast<std::size_t>(b)] = static_cast<float>(acc_[static_cast<std::size_t>(b)] / kFrameHop);
                    publish(current_, e);
                }
                // Une trame commence à son premier échantillon, sinon elle est entamée : jetée.
                current_ = k;
                count_ = pos >= 0 && pos % kFrameHop == 0 ? 0 : -1000000;
                acc_.fill(0.0);
            }
            for (int b = 0; b < kBands; ++b) acc_[static_cast<std::size_t>(b)] += y[static_cast<std::size_t>(b)] * y[static_cast<std::size_t>(b)];
            ++count_;
        }
    }

private:
    struct Biquad {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double run(double x) noexcept {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            // Pas de dénormaux sur une piste qui se tait.
            if (std::fabs(z1) < 1e-30) z1 = 0.0;
            if (std::fabs(z2) < 1e-30) z2 = 0.0;
            return y;
        }
    };
    std::array<Biquad, kBands> filters_{};
    std::array<double, kBands> acc_{};
    std::int64_t current_ = -1;
    int count_ = 0;
};

} // namespace omnis::link
