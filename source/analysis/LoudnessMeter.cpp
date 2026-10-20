// =============================================================================
//  Abrasion Labs // OMNIS
//  analysis/LoudnessMeter.cpp
// =============================================================================
#include "LoudnessMeter.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace omnis::analysis {

// -----------------------------------------------------------------------------
//  LoudnessHistogram
// -----------------------------------------------------------------------------
void LoudnessHistogram::allocate() {
    counts_ = std::make_unique<std::uint64_t[]>(kNumBins);
    powerSums_ = std::make_unique<double[]>(kNumBins);
    reset();
}

void LoudnessHistogram::reset() noexcept {
    if (!counts_) return;
    std::fill_n(counts_.get(), kNumBins, 0);
    std::fill_n(powerSums_.get(), kNumBins, 0.0);
    totalCount_ = 0;
    totalPower_ = 0.0;
}

void LoudnessHistogram::add(double power) noexcept {
    const double lufs = LoudnessMeter::powerToLufs(power);
    if (!(lufs >= kMinLufs)) return; // porte absolue (et rejet des NaN)
    const int bin = std::min(static_cast<int>((lufs - kMinLufs) / kBinWidth), kNumBins - 1);
    counts_[bin] += 1;
    powerSums_[bin] += power;
    totalCount_ += 1;
    totalPower_ += power;
}

double LoudnessHistogram::relativeThreshold(double relativeGateLu) const noexcept {
    // Γr = sonie moyenne des blocs passant la porte absolue, moins N LU.
    return LoudnessMeter::powerToLufs(totalPower_ / static_cast<double>(totalCount_)) - relativeGateLu;
}

int LoudnessHistogram::firstBinAbove(double thresholdLufs) const noexcept {
    // Première case dont la sonie moyenne réelle (puissance moyenne de ses
    // blocs) est >= seuil. La case du seuil est donc tranchée selon le contenu
    // réel et non sur sa borne, ce qui ramène l'erreur de quantification à
    // quasiment zéro.
    int bin = std::clamp(static_cast<int>((thresholdLufs - kMinLufs) / kBinWidth), 0, kNumBins - 1);
    if (counts_[bin] > 0) {
        const double binLufs = LoudnessMeter::powerToLufs(powerSums_[bin] / static_cast<double>(counts_[bin]));
        if (binLufs < thresholdLufs) ++bin;
    }
    return bin;
}

double LoudnessHistogram::gatedLoudness(double relativeGateLu) const noexcept {
    if (totalCount_ == 0) return LoudnessMeter::kNegativeInfinity;
    const int first = firstBinAbove(relativeThreshold(relativeGateLu));
    std::uint64_t count = 0;
    double power = 0.0;
    for (int b = first; b < kNumBins; ++b) {
        count += counts_[b];
        power += powerSums_[b];
    }
    if (count == 0) return LoudnessMeter::kNegativeInfinity;
    return LoudnessMeter::powerToLufs(power / static_cast<double>(count));
}

double LoudnessHistogram::loudnessRange(double relativeGateLu, double lowPercentile,
                                        double highPercentile) const noexcept {
    if (totalCount_ == 0) return 0.0;
    const int first = firstBinAbove(relativeThreshold(relativeGateLu));
    std::uint64_t count = 0;
    for (int b = first; b < kNumBins; ++b) count += counts_[b];
    if (count == 0) return 0.0;

    // Rang (0-based) des percentiles dans la distribution triée — même
    // convention que l'implémentation de référence (arrondi au plus proche).
    const auto rankOf = [count](double p) {
        return static_cast<std::uint64_t>(static_cast<double>(count - 1) * p + 0.5);
    };
    const std::uint64_t lowRank = rankOf(lowPercentile);
    const std::uint64_t highRank = rankOf(highPercentile);

    double low = 0.0, high = 0.0;
    bool lowFound = false;
    std::uint64_t cumulative = 0;
    for (int b = first; b < kNumBins; ++b) {
        if (counts_[b] == 0) continue;
        cumulative += counts_[b];
        const double binLufs = LoudnessMeter::powerToLufs(powerSums_[b] / static_cast<double>(counts_[b]));
        if (!lowFound && cumulative > lowRank) {
            low = binLufs;
            lowFound = true;
        }
        if (cumulative > highRank) {
            high = binLufs;
            break;
        }
    }
    return std::max(0.0, high - low);
}

// -----------------------------------------------------------------------------
//  LoudnessMeter
// -----------------------------------------------------------------------------
double LoudnessMeter::powerToLufs(double power) noexcept {
    return power > 0.0 ? -0.691 + 10.0 * std::log10(power) : kNegativeInfinity;
}

void LoudnessMeter::prepare(double sampleRate, int numChannels) {
    assert(numChannels >= 1 && numChannels <= kMaxChannels);
    numChannels_ = std::clamp(numChannels, 1, kMaxChannels);
    // Sous-bloc de 100 ms. Pour les fréquences usuelles (44.1k ... 384k) la
    // longueur est entière ; sinon l'arrondi introduit < 0.01 % d'erreur.
    subBlockLength_ = std::max(1, static_cast<int>(std::lround(sampleRate * 0.1)));
    for (auto& f : filters_) f.prepare(sampleRate);
    integratedHistogram_.allocate();
    rangeHistogram_.allocate();
    reset();
}

void LoudnessMeter::reset() noexcept {
    for (auto& f : filters_) f.reset();
    subBlockPosition_ = 0;
    subBlockSum_ = 0.0;
    subBlockPowers_.fill(0.0);
    ringIndex_ = 0;
    completedSubBlocks_ = 0;
    momentary_ = kNegativeInfinity;
    shortTerm_ = kNegativeInfinity;
    resetIntegrated();
}

void LoudnessMeter::resetIntegrated() noexcept {
    integratedHistogram_.reset();
    rangeHistogram_.reset();
    subBlocksSinceIntegratedReset_ = 0;
    maxMomentary_ = kNegativeInfinity;
    maxShortTerm_ = kNegativeInfinity;
}

void LoudnessMeter::process(const double* const* channels, int numSamples) noexcept {
    int offset = 0;
    while (offset < numSamples) {
        const int n = std::min(numSamples - offset, subBlockLength_ - subBlockPosition_);
        double sum = 0.0;
        for (int ch = 0; ch < numChannels_; ++ch) {
            const double* x = channels[ch] + offset;
            KWeightingFilter& filter = filters_[ch];
            for (int i = 0; i < n; ++i) {
                const double y = filter.process(x[i]);
                sum += y * y; // G_i = 1 pour L et R
            }
        }
        subBlockSum_ += sum;
        subBlockPosition_ += n;
        offset += n;
        if (subBlockPosition_ == subBlockLength_) completeSubBlock();
    }
}

void LoudnessMeter::completeSubBlock() noexcept {
    subBlockPowers_[ringIndex_] = subBlockSum_ / static_cast<double>(subBlockLength_);
    ringIndex_ = (ringIndex_ + 1) % kShortTermSubBlocks;
    subBlockSum_ = 0.0;
    subBlockPosition_ = 0;
    ++completedSubBlocks_;
    ++subBlocksSinceIntegratedReset_;

    // Fenêtres glissantes : moyenne des N derniers sous-blocs. Les sous-blocs
    // pas encore remplis valent 0 (le signal est supposé silencieux avant le
    // départ), ce qui correspond au comportement d'un afficheur EBU.
    double momentaryPower = 0.0;
    double shortTermPower = 0.0;
    for (int k = 0; k < kShortTermSubBlocks; ++k) {
        const int idx = (ringIndex_ - 1 - k + 2 * kShortTermSubBlocks) % kShortTermSubBlocks;
        shortTermPower += subBlockPowers_[idx];
        if (k < kMomentarySubBlocks) momentaryPower += subBlockPowers_[idx];
    }
    momentaryPower /= kMomentarySubBlocks;
    shortTermPower /= kShortTermSubBlocks;

    momentary_ = powerToLufs(momentaryPower);
    shortTerm_ = powerToLufs(shortTermPower);

    const bool momentaryValid = subBlocksSinceIntegratedReset_ >= kMomentarySubBlocks;
    const bool shortTermValid = subBlocksSinceIntegratedReset_ >= kShortTermSubBlocks;

    if (momentaryValid) {
        // Bloc de gating de 400 ms à 75 % de recouvrement = la fenêtre
        // momentary évaluée tous les 100 ms.
        integratedHistogram_.add(momentaryPower);
        maxMomentary_ = std::max(maxMomentary_, momentary_);
    }
    if (shortTermValid) {
        rangeHistogram_.add(shortTermPower);
        maxShortTerm_ = std::max(maxShortTerm_, shortTerm_);
    }

    if (stepCallback_) stepCallback_(stepUser_, momentary_, shortTerm_, shortTermValid);
}

double LoudnessMeter::integratedLufs() const noexcept {
    return integratedHistogram_.gatedLoudness(10.0);
}

double LoudnessMeter::loudnessRangeLu() const noexcept {
    return rangeHistogram_.loudnessRange(20.0, 0.10, 0.95);
}

} // namespace omnis::analysis
