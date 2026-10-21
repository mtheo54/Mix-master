// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/LookaheadGain.h — Lissage de gain « brickwall » à anticipation.
//
//  Reçoit à chaque échantillon le gain REQUIS r[n] (<= 1) et rend un gain lisse
//  g[n] qui, appliqué au signal retardé de L échantillons, ne dépasse jamais le
//  gain requis à l'instant de chaque crête :
//    1. relâchement : e = r si r < e (attaque instantanée), sinon remontée
//       exponentielle e += a_rel·(r − e) ;
//    2. minimum glissant sur L + 1 échantillons (file monotone, sans allocation) ;
//    3. moyenne glissante sur L + 1 échantillons : rampe d'attaque de L
//       échantillons, sans discontinuité.
//  Garantie : toutes les valeurs moyennées à l'instant n0 + L proviennent d'un
//  minimum qui contient e[n0], donc g[n0 + L] <= e[n0] <= r[n0].
//  Utilisé par le limiteur et par le partage de marge du clipper deux bandes.
// =============================================================================
#pragma once

#include <algorithm>
#include <vector>

namespace omnis::dsp {

class LookaheadGain {
public:
    /// Non temps réel.
    void prepare(int lookaheadSamples) {
        lookahead_ = std::max(1, lookaheadSamples);
        const auto window = static_cast<std::size_t>(lookahead_ + 1);
        minValues_.assign(window + 1, 1.0);
        minIndices_.assign(window + 1, 0);
        boxRing_.assign(window, 1.0);
        reset();
    }

    void reset() noexcept {
        envelope_ = 1.0;
        minHead_ = 0;
        minCount_ = 0;
        index_ = 0;
        std::fill(boxRing_.begin(), boxRing_.end(), 1.0);
        boxSum_ = static_cast<double>(boxRing_.size());
        boxPos_ = 0;
    }

    [[nodiscard]] int lookahead() const noexcept { return lookahead_; }

    /// `required` : gain requis maintenant ; `release` : coefficient de remontée.
    double process(double required, double release) noexcept {
        envelope_ = required < envelope_ ? required : envelope_ + release * (required - envelope_);

        // Minimum glissant : on retire de la queue les valeurs >= nouvelle.
        const int ring = static_cast<int>(minValues_.size());
        while (minCount_ > 0) {
            const int last = (minHead_ + minCount_ - 1) % ring;
            if (minValues_[static_cast<std::size_t>(last)] < envelope_) break;
            --minCount_;
        }
        const int slot = (minHead_ + minCount_) % ring;
        minValues_[static_cast<std::size_t>(slot)] = envelope_;
        minIndices_[static_cast<std::size_t>(slot)] = index_;
        ++minCount_;
        while (minIndices_[static_cast<std::size_t>(minHead_)] <= index_ - (lookahead_ + 1)) {
            minHead_ = (minHead_ + 1) % ring;
            --minCount_;
        }
        const double held = minValues_[static_cast<std::size_t>(minHead_)];
        ++index_;

        // Moyenne glissante (recalcul exact à chaque tour d'anneau contre la dérive).
        boxSum_ += held - boxRing_[static_cast<std::size_t>(boxPos_)];
        boxRing_[static_cast<std::size_t>(boxPos_)] = held;
        if (++boxPos_ == static_cast<int>(boxRing_.size())) {
            boxPos_ = 0;
            double exact = 0.0;
            for (double v : boxRing_) exact += v;
            boxSum_ = exact;
        }
        return boxSum_ / static_cast<double>(boxRing_.size());
    }

private:
    int lookahead_ = 1;
    double envelope_ = 1.0;
    std::vector<double> minValues_;
    std::vector<long long> minIndices_;
    int minHead_ = 0, minCount_ = 0;
    long long index_ = 0;
    std::vector<double> boxRing_;
    int boxPos_ = 0;
    double boxSum_ = 0.0;
};

} // namespace omnis::dsp
