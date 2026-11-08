// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/LoudnessTuner.cpp
// =============================================================================
#include "LoudnessTuner.h"

#include "AnalysisReport.h"

#include "analysis/LoudnessMeter.h"
#include "engine/Engine.h"
#include "engine/MasteringChain.h"
#include "params/Parameters.h"

#include <algorithm>
#include <cmath>

namespace omnis::tools {

namespace {

struct StepCollector {
    std::vector<double> shortTerm;
    static void onStep(void* user, double, double st, bool) { static_cast<StepCollector*>(user)->shortTerm.push_back(st); }
};

} // namespace

AudioFile loudestExcerpt(const AudioFile& input, double seconds) {
    const int frames = input.numFrames();
    // Comparaison en double : seconds·fs peut dépasser la capacité d'un int.
    if (seconds * input.sampleRate >= static_cast<double>(frames)) return input;
    const int length = static_cast<int>(seconds * input.sampleRate);

    analysis::LoudnessMeter meter;
    meter.prepare(input.sampleRate, 2);
    StepCollector collector;
    meter.setStepCallback(&StepCollector::onStep, &collector);
    const double* ptrs[2] = {input.channels[0].data(), input.channels[std::min(1, input.numChannels() - 1)].data()};
    meter.process(ptrs, frames);

    // Fenêtre (en pas de 100 ms) maximisant la puissance short-term moyenne.
    const int window = static_cast<int>(seconds * 10.0);
    double best = -1.0, sum = 0.0;
    int bestEnd = window;
    std::vector<double> power(collector.shortTerm.size());
    for (std::size_t i = 0; i < power.size(); ++i)
        power[i] = std::isfinite(collector.shortTerm[i]) ? std::pow(10.0, collector.shortTerm[i] / 10.0) : 0.0;
    for (int i = 0; i < static_cast<int>(power.size()); ++i) {
        sum += power[static_cast<std::size_t>(i)];
        if (i >= window) sum -= power[static_cast<std::size_t>(i - window)];
        if (i >= window - 1 && sum > best) {
            best = sum;
            bestEnd = i + 1;
        }
    }
    // Le pas k couvre les 3 s précédant (k+1)·100 ms.
    const int endSample = std::min(frames, static_cast<int>(bestEnd * 0.1 * input.sampleRate));
    const int startSample = std::max(0, endSample - length);
    AudioFile out;
    out.sampleRate = input.sampleRate;
    out.resize(2, endSample - startSample);
    for (int ch = 0; ch < 2; ++ch) {
        const auto& src = input.channels[static_cast<std::size_t>(std::min(ch, input.numChannels() - 1))];
        std::copy(src.begin() + startSample, src.begin() + endSample, out.channels[static_cast<std::size_t>(ch)].begin());
    }
    return out;
}

AudioFile renderWithSettings(const AudioFile& input, const std::vector<std::pair<std::string, double>>& settings, bool lowLatency) {
    Engine engine;
    buildMasteringChain(engine);
    engine.setLowLatency(lowLatency);
    constexpr int kBlock = 512;
    engine.prepare(input.sampleRate, kBlock);
    for (const auto& [key, value] : settings)
        if (const auto* spec = params::findParameterByKey(key)) engine.setParameterImmediate(spec->id, value);

    const int latency = engine.latencySamples();
    const int frames = input.numFrames();
    const int total = frames + latency;
    std::vector<double> l(static_cast<std::size_t>(total), 0.0), r(static_cast<std::size_t>(total), 0.0);
    std::copy(input.channels[0].begin(), input.channels[0].end(), l.begin());
    std::copy(input.channels[std::min<std::size_t>(1, input.channels.size() - 1)].begin(),
              input.channels[std::min<std::size_t>(1, input.channels.size() - 1)].end(), r.begin());
    for (int pos = 0; pos < total; pos += kBlock) {
        const int n = std::min(kBlock, total - pos);
        double* io[2] = {l.data() + pos, r.data() + pos};
        engine.process(io, io, n);
    }
    AudioFile out;
    out.sampleRate = input.sampleRate;
    out.resize(2, frames);
    std::copy(l.begin() + latency, l.end(), out.channels[0].begin());
    std::copy(r.begin() + latency, r.end(), out.channels[1].begin());
    return out;
}

std::vector<std::pair<std::string, double>> settingsOf(const TuningResult& result) {
    auto s = result.strategy.fixed;
    // Chaque paramètre reçoit sa part du gain total ; s'il atteint sa borne, le
    // surplus est reporté sur le paramètre suivant (le dernier absorbe le reste).
    double carry = 0.0;
    const auto& shares = result.strategy.gainShares;
    for (std::size_t i = 0; i < shares.size(); ++i) {
        const auto& [key, share] = shares[i];
        double wanted = share * result.totalGainDb + carry;
        carry = 0.0;
        if (const auto* spec = params::findParameterByKey(key)) {
            const double clamped = std::min(wanted, spec->maxValue);
            if (i + 1 < shares.size()) carry = wanted - clamped;
            wanted = clamped;
        }
        s.emplace_back(key, wanted);
    }
    return s;
}

TuningResult tuneStrategy(const AudioFile& excerpt, const TuningStrategy& strategy, const TuningOptions& options) {
    TuningResult result;
    result.strategy = strategy;

    const auto evaluate = [&](double gainDb, AudioFile* keep) {
        result.totalGainDb = gainDb;
        AudioFile out = renderWithSettings(excerpt, settingsOf(result));
        const auto report = analyze(out);
        if (keep) *keep = std::move(out);
        return std::make_pair(report.integratedLufs, report.truePeakDb);
    };

    // Dichotomie sur le gain total : la sonie intégrée croît avec le gain.
    double lo = 0.0, hi = options.maxGainDb;
    auto [lufsHi, tpHi] = evaluate(hi, nullptr);
    if (lufsHi < options.targetLufs) {
        // Cible inatteignable avec cette stratégie : on garde le maximum.
        AudioFile out;
        auto [lufs, tp] = evaluate(hi, &out);
        result.integratedLufs = lufs;
        result.truePeakDb = tp;
        result.distortion = measureDistortion(excerpt, out);
        result.tonal = compareTonalBalance(options.tonalReference ? *options.tonalReference : excerpt, out);
        result.tonalOk = result.tonal.maxDeviationDb <= options.maxTonalDeviationDb;
        return result;
    }
    (void)tpHi;
    for (int iteration = 0; iteration < 14; ++iteration) {
        const double mid = 0.5 * (lo + hi);
        const auto [lufs, tp] = evaluate(mid, nullptr);
        (void)tp;
        if (std::fabs(lufs - options.targetLufs) <= options.toleranceLu) {
            lo = hi = mid;
            break;
        }
        (lufs < options.targetLufs ? lo : hi) = mid;
    }
    AudioFile out;
    const auto [lufs, tp] = evaluate(0.5 * (lo + hi), &out);
    result.integratedLufs = lufs;
    result.truePeakDb = tp;
    result.reached = std::fabs(lufs - options.targetLufs) <= 0.15;
    result.distortion = measureDistortion(excerpt, out);
    result.tonal = compareTonalBalance(options.tonalReference ? *options.tonalReference : excerpt, out);
    result.tonalOk = result.tonal.maxDeviationDb <= options.maxTonalDeviationDb;
    return result;
}

TuningResult loudestClean(const AudioFile& excerpt, const TuningStrategy& strategy, const TuningOptions& options) {
    TuningResult result;
    result.strategy = strategy;

    const auto evaluate = [&](double gainDb, AudioFile& out) {
        result.totalGainDb = gainDb;
        out = renderWithSettings(excerpt, settingsOf(result));
        // La cible reste un plafond : jamais plus fort que demandé.
        return compareTonalBalance(options.tonalReference ? *options.tonalReference : excerpt, out).maxDeviationDb <= options.maxTonalDeviationDb &&
               analyze(out).integratedLufs <= options.targetLufs + options.toleranceLu;
    };

    // Dichotomie sur le gain : l'écart tonal croît avec la poussée. lo respecte
    // toujours la prod (gain nul), hi la détruit ou dépasse la cible.
    double lo = 0.0, hi = options.maxGainDb;
    AudioFile out;
    if (!evaluate(hi, out))
        for (int iteration = 0; iteration < 10; ++iteration) {
            const double mid = 0.5 * (lo + hi);
            (evaluate(mid, out) ? lo : hi) = mid;
        }
    else
        lo = hi;
    evaluate(lo, out);
    const auto report = analyze(out);
    result.integratedLufs = report.integratedLufs;
    result.truePeakDb = report.truePeakDb;
    result.reached = report.integratedLufs >= options.targetLufs - 0.15;
    result.distortion = measureDistortion(excerpt, out);
    result.tonal = compareTonalBalance(options.tonalReference ? *options.tonalReference : excerpt, out);
    result.tonalOk = true;
    return result;
}

SearchResult searchMaster(const AudioFile& excerpt, const std::vector<std::pair<std::string, double>>& base, const SearchOptions& options) {
    SearchResult out;
    std::vector<std::pair<std::string, double>> fixed;
    for (const auto& kv : base)
        if (kv.first != "clip_drive" && kv.first != "lim_gain") fixed.push_back(kv);
    // Les répartitions essayées : le clip prend les transitoires (kick, 808), le limiteur le reste.
    const std::vector<std::pair<std::string, double>> splits{{"limiteur", 0.0}, {"clip 40 / 60", 0.4}, {"clip 70 / 30", 0.7}};
    const auto cancelled = [&] { return options.cancel && options.cancel->load(std::memory_order_relaxed); };
    const int total = static_cast<int>(splits.size());
    for (int i = 0; i < total; ++i) {
        if (cancelled()) {
            out.cancelled = true;
            return out;
        }
        TuningResult r;
        r.strategy.name = splits[static_cast<std::size_t>(i)].first;
        r.strategy.fixed = fixed;
        const double clipShare = splits[static_cast<std::size_t>(i)].second;
        if (clipShare > 0.0) r.strategy.gainShares.emplace_back("clip_drive", clipShare);
        r.strategy.gainShares.emplace_back("lim_gain", 1.0 - clipShare);
        const auto loudnessAt = [&](double gain, AudioFile* keep) {
            r.totalGainDb = gain;
            AudioFile rendered = renderWithSettings(excerpt, settingsOf(r), options.lowLatency);
            const double lufs = analyze(rendered).integratedLufs;
            if (keep) *keep = std::move(rendered);
            return lufs;
        };
        // Sécante bornée : 6 rendus au plus. La sonie croît avec le gain. Le rendu sans poussée
        // (gain 0) sert de référence tonale : seul compte ce que la compression change, pas la
        // couleur voulue par le Match EQ.
        AudioFile unpushed;
        double g0 = 0.0, l0 = loudnessAt(0.0, &unpushed);
        double g1 = std::clamp(options.targetLufs - l0, 0.0, options.maxGainDb), l1 = g1 > 0.0 ? loudnessAt(g1, nullptr) : l0;
        for (int step = 0; step < 4 && std::fabs(l1 - options.targetLufs) > 0.3 && !cancelled(); ++step) {
            if (l1 < options.targetLufs && g1 >= options.maxGainDb - 1e-9) break; // garde-fou atteint
            const double slope = std::fabs(l1 - l0) > 1e-6 ? (g1 - g0) / (l1 - l0) : 1.0;
            const double next = std::clamp(g1 + std::clamp(slope, 0.5, 4.0) * (options.targetLufs - l1), 0.0, options.maxGainDb);
            g0 = g1, l0 = l1;
            g1 = next;
            l1 = loudnessAt(g1, nullptr);
        }
        AudioFile rendered;
        r.integratedLufs = loudnessAt(g1, &rendered);
        r.reached = r.integratedLufs >= options.targetLufs - 0.3;
        r.distortion = measureDistortion(excerpt, rendered);
        r.tonal = compareTonalBalance(unpushed, rendered);
        r.tonalOk = r.tonal.maxDeviationDb <= options.maxTonalDeviationDb;
        const bool nothingToPush = r.totalGainDb <= 1e-9;
        out.tried.push_back(std::move(r));
        if (options.progress) options.progress(i + 1, total);
        // Déjà à la sonie visée sans poussée : toutes les répartitions donnent le même son.
        if (nothingToPush) {
            if (options.progress) options.progress(total, total);
            break;
        }
    }
    // Note : la distorsion (dB, plus bas = mieux), plus une pénalité si la balance tonale ou la
    // sonie ne tiennent pas. À sonie égale, la moins abîmante gagne.
    double bestScore = 1e9;
    for (std::size_t i = 0; i < out.tried.size(); ++i) {
        const auto& r = out.tried[i];
        double score = r.distortion.weightedDb;
        if (!r.tonalOk) score += 6.0 + 4.0 * (r.tonal.maxDeviationDb - options.maxTonalDeviationDb);
        if (!r.reached) score += 3.0 * std::max(0.0, options.targetLufs - r.integratedLufs);
        if (score < bestScore) bestScore = score, out.best = static_cast<int>(i);
    }
    return out;
}

} // namespace omnis::tools
