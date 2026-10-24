// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/StemChain.cpp
// =============================================================================
#include "StemChain.h"

#include "core/Decibels.h"

#include <algorithm>
#include <cmath>

namespace omnis::stems {

namespace {

constexpr double kHpfOffHz = 5.0;     ///< passe-haut « éteint » : sous l'audible
constexpr double kFadeMs = 30.0;      ///< fondu Off <-> actif
constexpr double kControlMs = 100.0;  ///< lissage des réglages (constante de temps)
constexpr double kKneeDb = 6.0;
constexpr double kSilence = 1.0e-10;  ///< puissance sous laquelle la piste se tait (−100 dB)
constexpr double kHabitMs = 4000.0;   ///< équilibre habituel de la piste
constexpr double kAboveHabitDb = 3.0; ///< une bande ne coupe qu'au-dessus de son habitude

double coefFor(double ms, double rate) noexcept { return 1.0 - std::exp(-1.0 / (ms * 1.0e-3 * rate)); }

/// Vers la cible, en échelle logarithmique (fréquences), collée à l'arrivée.
double glideLog(double cur, double goal, double c) noexcept {
    if (std::fabs(cur / goal - 1.0) < 1.0e-4) return goal;
    return cur * std::pow(goal / cur, c);
}

} // namespace

void StemChain::prepare(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double controlRate = sampleRate_ / kControlInterval;
    controlCoef_ = coefFor(kControlMs, controlRate);
    mixStep_ = 1.0 / (kFadeMs * 1.0e-3 * sampleRate_);
    envAttack_ = coefFor(5.0, sampleRate_);
    envRelease_ = coefFor(80.0, sampleRate_);
    habitCoef_ = coefFor(kHabitMs, sampleRate_ / kControlInterval);
    avgCoef_ = coefFor(2000.0, sampleRate_);
    slowRelease_ = std::exp(-1.0 / (3.0 * sampleRate_));
    levelRelease_ = std::exp(-1.0 / (0.025 * sampleRate_));
    satRelease_ = std::exp(-1.0 / (1.0 * sampleRate_));
    const auto coefs = dsp::designHalfband(kHalfbandAttenuationDb, kHalfbandTransition);
    for (auto& f : halfband_) f.setCoefficients(coefs);
    for (int ch = 0; ch < 2; ++ch) {
        dry_[static_cast<std::size_t>(ch)].assign(kChunk, 0.0);
        satUp_[static_cast<std::size_t>(ch)].assign(2 * kChunk, 0.0);
        satOut_[static_cast<std::size_t>(ch)].assign(kChunk, 0.0);
    }
    peaks_.assign(kChunk, 0.0);
    target_ = StemSettings{};
    setSettings(target_);
    mix_ = 0.0;
    reset();
}

void StemChain::reset() noexcept {
    for (auto& s : hpf1State_) s.reset();
    for (auto& s : hpf2State_) s.reset();
    hpfRunning_ = false;
    for (auto& b : dyn_) {
        b.detectState.reset();
        for (auto& s : b.bellState) s.reset();
        b.envelope = 0.0;
        b.habitDb = 0.0;
        b.habitReady = false;
        b.cutDb = b.appliedCutDb = 0.0;
        b.appliedHz = b.appliedQ = 0.0;
    }
    totalEnvelope_ = 0.0;
    grDb_ = avgGrDb_ = compPeak_ = level_ = 0.0;
    satPeak_ = 0.0;
    satRunning_ = false;
    for (auto& f : halfband_) f.reset();
    for (auto& s : side_) s.reset();
    msBlend_ = 0.0;
    for (auto& f : fixes_) f = MasterFix{};
    controlCountdown_ = 0;
    snapToTarget();
}

void StemChain::setSettings(const StemSettings& settings) noexcept {
    const bool wasActive = target_.active;
    target_ = clampSettings(settings);
    const StemSettings& s = target_;
    Smoothed& g = goal_;
    g.trimGain = dbToGain(s.trimDb);
    const bool hpf = s.hpfHz > 0.0;
    g.hpf1Hz = hpf ? s.hpfHz : kHpfOffHz;
    g.hpf2Hz = hpf && s.hpfSteep ? s.hpfHz : kHpfOffHz;
    g.hpfQ1 = hpf && s.hpfSteep ? dsp::butterworthQ(4, 0) : dsp::butterworthQ(2, 0);
    g.hpfQ2 = hpf && s.hpfSteep ? dsp::butterworthQ(4, 1) : dsp::butterworthQ(2, 0);
    for (int b = 0; b < kDynamicBands; ++b) {
        const auto i = static_cast<std::size_t>(b);
        const DynamicBand& d = s.dynamic[i];
        // Une bande éteinte garde sa fréquence courante : rien ne glisse pour rien.
        g.bandHz[i] = d.maxCutDb > 0.0 || cur_.bandHz[i] <= 0.0 ? d.hz : cur_.bandHz[i];
        g.bandQ[i] = d.maxCutDb > 0.0 || cur_.bandQ[i] <= 0.0 ? d.q : cur_.bandQ[i];
        g.bandAllow[i] = d.allowDb;
        g.bandMaxCut[i] = d.maxCutDb;
    }
    g.ratio = s.compRatio;
    g.depthDb = s.compDepthDb;
    g.saturation = s.saturation;
    g.monoHz = s.monoBelowHz > 0.0 ? s.monoBelowHz : (cur_.monoHz > 0.0 ? cur_.monoHz : 120.0);
    g.monoAmount = s.monoAmount;
    g.width = s.width;
    compAttack_ = coefFor(s.compAttackMs, sampleRate_);
    compRelease_ = coefFor(s.compReleaseMs, sampleRate_);
    // Réveil depuis Off : on part directement des nouveaux réglages (le fondu
    // depuis le son sec couvre la transition), états vides.
    if (s.active && !wasActive && mix_ <= 0.0) reset();
}

void StemChain::snapToTarget() noexcept {
    cur_ = goal_;
    prevSaturation_ = cur_.saturation;
    prevMonoAmount_ = cur_.monoAmount;
    prevWidth_ = cur_.width;
    // Le fondu depuis le son sec couvre l'entrée : retouches et M/S d'emblée en place.
    for (int i = 0; i < kMaxCorrections; ++i) {
        const Correction& want = target_.corrections[static_cast<std::size_t>(i)];
        MasterFix& f = fixes_[static_cast<std::size_t>(i)];
        f.type = want.type;
        f.hz = want.hz;
        f.gainDb = want.gainDb;
        f.q = want.q;
        f.amount = want.type != Correction::Type::None ? 1.0 : 0.0;
    }
    msBlend_ = goal_.monoAmount > 0.0 || goal_.width != 1.0 ? 1.0 : 0.0;
    control();
}

void StemChain::control() noexcept {
    const double c = controlCoef_;
    Smoothed& s = cur_;
    const Smoothed& g = goal_;
    s.trimGain += c * (g.trimGain - s.trimGain);
    s.hpf1Hz = glideLog(s.hpf1Hz, g.hpf1Hz, c);
    s.hpf2Hz = glideLog(s.hpf2Hz, g.hpf2Hz, c);
    s.hpfQ1 += c * (g.hpfQ1 - s.hpfQ1);
    s.hpfQ2 += c * (g.hpfQ2 - s.hpfQ2);
    hpf1_ = dsp::SvfCoefficients::make(dsp::SvfType::HighPass, sampleRate_, s.hpf1Hz, s.hpfQ1);
    hpf2_ = dsp::SvfCoefficients::make(dsp::SvfType::HighPass, sampleRate_, s.hpf2Hz, s.hpfQ2);
    // Passe-haut éteint et arrivé : plus traversé du tout.
    const bool hpfOff = s.hpf1Hz == kHpfOffHz && s.hpf2Hz == kHpfOffHz && g.hpf1Hz == kHpfOffHz && g.hpf2Hz == kHpfOffHz;
    if (hpfOff && hpfRunning_) {
        for (auto& st : hpf1State_) st.reset();
        for (auto& st : hpf2State_) st.reset();
    }
    hpfRunning_ = !hpfOff;

    const bool silent = totalEnvelope_ < kSilence;
    for (int b = 0; b < kDynamicBands; ++b) {
        const auto i = static_cast<std::size_t>(b);
        DynBand& d = dyn_[i];
        s.bandHz[i] = s.bandHz[i] > 0.0 ? glideLog(s.bandHz[i], g.bandHz[i], c) : g.bandHz[i];
        s.bandQ[i] = s.bandQ[i] > 0.0 ? s.bandQ[i] + c * (g.bandQ[i] - s.bandQ[i]) : g.bandQ[i];
        s.bandAllow[i] += c * (g.bandAllow[i] - s.bandAllow[i]);
        s.bandMaxCut[i] += c * (g.bandMaxCut[i] - s.bandMaxCut[i]);
        if (s.bandMaxCut[i] < 1.0e-3 && g.bandMaxCut[i] <= 0.0) s.bandMaxCut[i] = 0.0;
        d.detect = dsp::SvfCoefficients::make(dsp::SvfType::BandPass, sampleRate_, s.bandHz[i], s.bandQ[i]);
        // Équilibre bande / reste de la piste. Il coupe quand il dépasse à la
        // fois celui d'un bruit rose + marge (la zone prend trop de place) et
        // l'habitude de la piste de 3 dB (un moment : note qui résonne,
        // accumulation). Un caractère permanent est respecté : c'est au master
        // de le proposer, s'il gêne un autre stem.
        double cut = 0.0;
        if (!silent) {
            const double rest = std::max(totalEnvelope_ - d.envelope, 1.0e-3 * totalEnvelope_);
            // Habitude : moyenne en dB (l'équilibre typique), pas en énergie,
            // sinon les moments forts la feraient monter eux-mêmes.
            const double balance = powerToDb(d.envelope / rest);
            d.habitDb = d.habitReady ? d.habitDb + habitCoef_ * (balance - d.habitDb) : balance;
            d.habitReady = true;
            if (s.bandMaxCut[i] > 0.0) {
                const double over = std::min(balance - pinkBalanceDb(s.bandQ[i]) - s.bandAllow[i], balance - d.habitDb - kAboveHabitDb);
                cut = std::clamp(0.5 * over, 0.0, s.bandMaxCut[i]);
            }
        }
        d.cutDb = cut;
        const bool moved = std::fabs(cut - d.appliedCutDb) > 0.01 || s.bandHz[i] != d.appliedHz || s.bandQ[i] != d.appliedQ;
        if (moved) {
            d.appliedCutDb = cut < 0.005 ? 0.0 : cut;
            d.appliedHz = s.bandHz[i];
            d.appliedQ = s.bandQ[i];
            d.bell = dsp::SvfCoefficients::make(dsp::SvfType::Bell, sampleRate_, d.appliedHz, d.appliedQ, -d.appliedCutDb);
        }
    }

    // Retouches du master : une sorte qui change s'efface d'abord, puis la
    // nouvelle entre en fondu.
    for (int i = 0; i < kMaxCorrections; ++i) {
        MasterFix& f = fixes_[static_cast<std::size_t>(i)];
        const Correction& want = target_.corrections[static_cast<std::size_t>(i)];
        if (f.type != want.type) {
            f.amount -= c * f.amount;
            if (f.amount < 1.0e-3 || f.type == Correction::Type::None) {
                f.type = want.type;
                f.hz = want.hz;
                f.gainDb = want.gainDb;
                f.q = want.q;
                f.amount = 0.0;
                for (auto& st : f.state) st.reset();
            }
        } else if (f.type != Correction::Type::None) {
            f.amount = f.amount > 0.999 ? 1.0 : f.amount + c * (1.0 - f.amount);
            f.hz = glideLog(f.hz, want.hz, c);
            f.gainDb += c * (want.gainDb - f.gainDb);
            f.q += c * (want.q - f.q);
        }
        switch (f.type) {
            case Correction::Type::LowCut:
                f.coef = dsp::SvfCoefficients::make(dsp::SvfType::HighPass, sampleRate_, kHpfOffHz * std::pow(f.hz / kHpfOffHz, f.amount), 0.7071);
                break;
            case Correction::Type::Bell:
                f.coef = dsp::SvfCoefficients::make(dsp::SvfType::Bell, sampleRate_, f.hz, f.q, f.gainDb * f.amount);
                break;
            case Correction::Type::LowShelf:
                f.coef = dsp::SvfCoefficients::make(dsp::SvfType::LowShelf, sampleRate_, f.hz, 0.7071, f.gainDb * f.amount);
                break;
            case Correction::Type::None:
            default:
                break;
        }
    }

    s.ratio += c * (g.ratio - s.ratio);
    s.depthDb += c * (g.depthDb - s.depthDb);
    s.saturation += c * (g.saturation - s.saturation);
    if (s.saturation < 1.0e-5 && g.saturation <= 0.0) s.saturation = 0.0;
    s.monoHz = glideLog(s.monoHz, g.monoHz, c);
    s.monoAmount += c * (g.monoAmount - s.monoAmount);
    s.width += c * (g.width - s.width);
    // Séparation Linkwitz-Riley 24 dB/oct du Side (passe-bas + passe-haut = passe-tout).
    monoLp_ = dsp::SvfCoefficients::make(dsp::SvfType::LowPass, sampleRate_, s.monoHz, dsp::butterworthQ(2, 0));
    monoHp_ = dsp::SvfCoefficients::make(dsp::SvfType::HighPass, sampleRate_, s.monoHz, dsp::butterworthQ(2, 0));
}

void StemChain::process(double* left, double* right, int numSamples) noexcept {
    // Off : rien n'est touché (au bit près).
    if (!target_.active && mix_ <= 0.0) return;
    for (int offset = 0; offset < numSamples; offset += kChunk)
        processChunk(left + offset, right + offset, std::min(kChunk, numSamples - offset));
}

void StemChain::processChunk(double* left, double* right, int n) noexcept {
    double* dryL = dry_[0].data();
    double* dryR = dry_[1].data();
    // Son sec du fondu (un NaN de l'hôte n'y passe pas).
    for (int i = 0; i < n; ++i) {
        dryL[i] = std::isfinite(left[i]) ? left[i] : 0.0;
        dryR[i] = std::isfinite(right[i]) ? right[i] : 0.0;
    }

    // ---- 1 … 4 : trim, passe-haut, EQ dynamique, compresseur ----------------
    for (int i = 0; i < n; ++i) {
        if (--controlCountdown_ < 0) {
            controlCountdown_ = kControlInterval - 1;
            control();
        }
        double l = std::isfinite(left[i]) ? left[i] * cur_.trimGain : 0.0;
        double r = std::isfinite(right[i]) ? right[i] * cur_.trimGain : 0.0;
        if (hpfRunning_) {
            l = hpf2State_[0].process(hpf2_, hpf1State_[0].process(hpf1_, l));
            r = hpf2State_[1].process(hpf2_, hpf1State_[1].process(hpf1_, r));
        }
        for (auto& f : fixes_)
            if (f.type != Correction::Type::None) {
                l = f.state[0].process(f.coef, l);
                r = f.state[1].process(f.coef, r);
            }

        const double mid = 0.5 * (l + r);
        const double p = mid * mid;
        totalEnvelope_ += (p > totalEnvelope_ ? envAttack_ : envRelease_) * (p - totalEnvelope_);
        for (auto& d : dyn_) {
            const double bp = d.detectState.process(d.detect, mid);
            const double e = bp * bp;
            d.envelope += (e > d.envelope ? envAttack_ : envRelease_) * (e - d.envelope);
        }
        for (auto& d : dyn_)
            if (d.appliedCutDb > 0.0) {
                l = d.bellState[0].process(d.bell, l);
                r = d.bellState[1].process(d.bell, r);
            }

        // Compresseur : niveau de crête (relâche 25 ms, tient l'alternance d'une
        // 808), seuil sous la crête récente, réduction lissée.
        const double peak = std::max(std::fabs(l), std::fabs(r));
        level_ = std::max(peak, level_ * levelRelease_);
        compPeak_ = std::max(peak, compPeak_ * slowRelease_);
        double grStatic = 0.0;
        if (cur_.ratio > 1.0001 && cur_.depthDb > 0.01 && compPeak_ > 1.0e-5) {
            const double over = gainToDb(level_) - (gainToDb(compPeak_) - cur_.depthDb);
            const double slope = 1.0 - 1.0 / cur_.ratio;
            if (over >= 0.5 * kKneeDb) grStatic = over * slope;
            else if (over > -0.5 * kKneeDb) grStatic = slope * (over + 0.5 * kKneeDb) * (over + 0.5 * kKneeDb) / (2.0 * kKneeDb);
        }
        grDb_ += (grStatic > grDb_ ? compAttack_ : compRelease_) * (grStatic - grDb_);
        avgGrDb_ += avgCoef_ * (grDb_ - avgGrDb_);
        if (grDb_ > 1.0e-6 || avgGrDb_ > 1.0e-6) {
            const double g = dbToGain(avgGrDb_ - grDb_);
            l *= g;
            r *= g;
        } else {
            grDb_ = avgGrDb_ = 0.0;
        }
        left[i] = l;
        right[i] = r;
    }

    // ---- 5 : saturation (courbe de l'Inflator, partie non linéaire en 2x) ---
    const double satFrom = prevSaturation_, satTo = cur_.saturation;
    prevSaturation_ = satTo;
    if (satFrom > 0.0 || satTo > 0.0) {
        if (!satRunning_) {
            for (auto& f : halfband_) f.reset();
            satRunning_ = true;
        }
        for (int i = 0; i < n; ++i) {
            satPeak_ = std::max({std::fabs(left[i]), std::fabs(right[i]), satPeak_ * satRelease_});
            peaks_[static_cast<std::size_t>(i)] = satPeak_;
        }
        const double* in[2] = {left, right};
        double* out[2] = {satOut_[0].data(), satOut_[1].data()};
        for (int ch = 0; ch < 2; ++ch) {
            double* x = satUp_[static_cast<std::size_t>(ch)].data();
            halfband_[static_cast<std::size_t>(ch)].upsampleBlock(in[ch], x, n);
            for (int j = 0; j < 2 * n; ++j) {
                const double peakValue = peaks_[static_cast<std::size_t>(j >> 1)];
                if (peakValue < 1.0e-9) {
                    x[j] = 0.0;
                    continue;
                }
                // h(u) = g(u) − 1.5·u, g : courbe de l'Inflator d'exposant 3.
                const double u = x[j] / peakValue;
                const double h = std::fabs(u) < 1.0 ? -0.5 * u * u * u : (u > 0.0 ? 1.0 : -1.0) - 1.5 * u;
                x[j] = peakValue * h;
            }
            halfband_[static_cast<std::size_t>(ch)].downsampleBlock(x, out[ch], n);
        }
        const double step = (satTo - satFrom) / n;
        for (int i = 0; i < n; ++i) {
            const double dose = satFrom + step * (i + 1);
            left[i] += dose * (0.5 * left[i] + out[0][i]);
            right[i] += dose * (0.5 * right[i] + out[1][i]);
        }
    } else {
        satRunning_ = false;
        satPeak_ = 0.0;
    }

    // ---- 6 : M/S — mono dans le grave, largeur au-dessus ---------------------
    const double monoFrom = prevMonoAmount_, monoTo = cur_.monoAmount;
    const double widthFrom = prevWidth_, widthTo = cur_.width;
    prevMonoAmount_ = monoTo;
    prevWidth_ = widthTo;
    const double monoStep = (monoTo - monoFrom) / n, widthStep = (widthTo - widthFrom) / n;
    // Neutre partout (réglage, valeurs courantes, bloc précédent) : l'étage s'efface.
    const bool neutral = goal_.monoAmount <= 0.0 && goal_.width == 1.0 && monoFrom <= 1.0e-4 && monoTo <= 1.0e-4 &&
                         std::fabs(widthFrom - 1.0) <= 1.0e-4 && std::fabs(widthTo - 1.0) <= 1.0e-4;
    const double blendFrom = msBlend_;
    const double blendTo = neutral ? std::max(0.0, msBlend_ - mixStep_ * n) : std::min(1.0, msBlend_ + mixStep_ * n);
    msBlend_ = blendTo;
    if (blendFrom > 0.0 || blendTo > 0.0) {
        const double blendStep = (blendTo - blendFrom) / n;
        for (int i = 0; i < n; ++i) {
            const double mid = 0.5 * (left[i] + right[i]);
            const double side = 0.5 * (left[i] - right[i]);
            const double sideLow = side_[1].process(monoLp_, side_[0].process(monoLp_, side));
            const double sideHigh = side_[3].process(monoHp_, side_[2].process(monoHp_, side));
            const double mono = monoFrom + monoStep * (i + 1), width = widthFrom + widthStep * (i + 1);
            const double blend = blendFrom + blendStep * (i + 1);
            const double s = side + blend * (sideLow * (1.0 - mono) + sideHigh * width - side);
            left[i] = mid + s;
            right[i] = mid - s;
        }
    } else {
        for (auto& st : side_) st.reset();
    }

    // ---- Fondu avec le son sec, garde-fou ------------------------------------
    bool finite = true;
    for (int i = 0; i < n; ++i) finite = finite && std::isfinite(left[i]) && std::isfinite(right[i]);
    if (!finite) {
        std::copy(dryL, dryL + n, left);
        std::copy(dryR, dryR + n, right);
        mix_ = 0.0;
        reset();
        return;
    }
    const double mixTarget = target_.active ? 1.0 : 0.0;
    if (mix_ != mixTarget || mixTarget < 1.0)
        for (int i = 0; i < n; ++i) {
            mix_ = mixTarget > mix_ ? std::min(mixTarget, mix_ + mixStep_) : std::max(mixTarget, mix_ - mixStep_);
            left[i] = dryL[i] + mix_ * (left[i] - dryL[i]);
            right[i] = dryR[i] + mix_ * (right[i] - dryR[i]);
        }
    if (!target_.active && mix_ <= 0.0) reset(); // effacée : la prochaine activation repart de zéro
}

} // namespace omnis::stems
