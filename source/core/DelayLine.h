// =============================================================================
//  Abrasion Labs // OMNIS
//  core/DelayLine.h — Retard entier multicanal pour la compensation de latence.
//
//  Utilisée pour aligner le signal sec (bypass, dry/wet) sur le signal traité,
//  dont la latence est la somme des latences des étages (FIR à phase linéaire,
//  lookahead du limiteur, filtres d'oversampling). Un retard entier exact
//  garantit un null-test parfait entre entrée retardée et sortie bypassée.
//
//  Tampon circulaire de taille puissance de deux -> indexation par masque.
// =============================================================================
#pragma once

#include "AudioBuffer.h"

#include <algorithm>
#include <cassert>

namespace omnis {

class DelayLine {
public:
    /// Non temps réel. `maxDelay` en échantillons.
    void prepare(int numChannels, int maxDelay) {
        int size = 1;
        while (size < maxDelay + 1) size <<= 1;
        buffer_.allocate(numChannels, size);
        mask_ = size - 1;
        maxDelay_ = maxDelay;
        writePos_ = 0;
        delay_ = std::min(delay_, maxDelay_);
    }

    void reset() noexcept {
        buffer_.clear();
        writePos_ = 0;
    }

    /// Change le retard. Ne pas appeler pendant la lecture (discontinuité) :
    /// la latence OMNIS est fixe pour une fréquence d'échantillonnage donnée.
    void setDelay(int delaySamples) noexcept {
        assert(delaySamples >= 0 && delaySamples <= maxDelay_);
        delay_ = std::clamp(delaySamples, 0, maxDelay_);
    }

    [[nodiscard]] int delay() const noexcept { return delay_; }

    /// Retarde en place les `numChannels` premiers canaux de `io`.
    void process(double* const* io, int numChannels, int numSamples) noexcept {
        if (delay_ == 0) return;
        assert(numChannels <= buffer_.numChannels());
        int pos = writePos_;
        for (int ch = 0; ch < numChannels; ++ch) {
            double* line = buffer_.channel(ch);
            double* x = io[ch];
            pos = writePos_;
            for (int i = 0; i < numSamples; ++i) {
                line[pos] = x[i];
                x[i] = line[(pos - delay_) & mask_];
                pos = (pos + 1) & mask_;
            }
        }
        writePos_ = pos;
    }

private:
    AudioBuffer buffer_;
    int mask_ = 0;
    int maxDelay_ = 0;
    int delay_ = 0;
    int writePos_ = 0;
};

} // namespace omnis
