// =============================================================================
//  Abrasion Labs // OMNIS
//  dsp/Halfband.cpp
// =============================================================================
#include "Halfband.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace omnis::dsp {

namespace {

constexpr double kPi = std::numbers::pi;

double accumulateNumerator(double q, int order, int c) {
    double result = 0.0, term;
    int i = 0, sign = 1;
    do {
        term = std::pow(q, i * (i + 1)) * std::sin((2 * i + 1) * c * kPi / order) * sign;
        result += term;
        sign = -sign;
        ++i;
    } while (std::fabs(term) > 1e-100 && i < 1000);
    return result;
}

double accumulateDenominator(double q, int order, int c) {
    double result = 0.0, term;
    int i = 1, sign = -1;
    do {
        term = std::pow(q, i * i) * std::cos(2 * i * c * kPi / order) * sign;
        result += term;
        sign = -sign;
        ++i;
    } while (std::fabs(term) > 1e-100 && i < 1000);
    return result;
}

} // namespace

std::vector<double> designHalfband(double attenuationDb, double transition) {
    double k = std::tan((1.0 - 2.0 * transition) * kPi / 4.0);
    k *= k;
    const double kk = std::pow(1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kk) / (1.0 + kk);
    const double e4 = e * e * e * e;
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));

    const double attnP2 = std::pow(10.0, -attenuationDb / 10.0);
    const double a = attnP2 / (1.0 - attnP2);
    int order = static_cast<int>(std::ceil(std::log(a * a / 16.0) / std::log(q)));
    if ((order & 1) == 0) ++order;
    order = std::max(order, 3);

    const int count = (order - 1) / 2;
    std::vector<double> coefs;
    coefs.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int c = i + 1;
        const double ww = accumulateNumerator(q, order, c) * std::pow(q, 0.25) / (accumulateDenominator(q, order, c) + 0.5);
        const double ww2 = ww * ww;
        const double x = std::sqrt((1.0 - ww2 * k) * (1.0 - ww2 / k)) / (1.0 + ww2);
        coefs.push_back((1.0 - x) / (1.0 + x));
    }
    return coefs;
}

void AllpassChain::setCoefficients(const double* coefs, int count) noexcept {
    count_ = std::min(count, kMaxSections);
    for (int i = 0; i < count_; ++i) c_[static_cast<std::size_t>(i)] = coefs[i];
}

void Halfband2x::setCoefficients(const std::vector<double>& coefs) noexcept {
    double even[AllpassChain::kMaxSections], odd[AllpassChain::kMaxSections];
    int ne = 0, no = 0;
    for (std::size_t i = 0; i < coefs.size(); ++i) {
        if (i % 2 == 0 && ne < AllpassChain::kMaxSections) even[ne++] = coefs[i];
        else if (i % 2 == 1 && no < AllpassChain::kMaxSections) odd[no++] = coefs[i];
    }
    for (AllpassChain* c : {&up0_, &down0_}) c->setCoefficients(even, ne);
    for (AllpassChain* c : {&up1_, &down1_}) c->setCoefficients(odd, no);
}

void Halfband2x::reset() noexcept {
    for (AllpassChain* c : {&up0_, &up1_, &down0_, &down1_}) c->reset();
    previousOdd_ = 0.0;
}

} // namespace omnis::dsp
