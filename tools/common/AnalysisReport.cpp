// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/AnalysisReport.cpp
// =============================================================================
#include "AnalysisReport.h"

#include "analysis/LoudnessMeter.h"
#include "analysis/SignalStatistics.h"
#include "analysis/TruePeakMeter.h"
#include "core/Decibels.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace omnis::tools {

namespace {

std::string fmt(double v, int decimals = 2) {
    if (!std::isfinite(v)) return v < 0 ? "-inf" : "inf";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

std::string json(double v) {
    if (!std::isfinite(v)) return "null";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

} // namespace

AnalysisReport analyze(const AudioFile& file) {
    AnalysisReport r;
    r.sampleRate = file.sampleRate;
    r.durationSeconds = file.durationSeconds();
    const int numCh = std::min(file.numChannels(), 2);
    r.numChannels = numCh;
    if (numCh == 0) return r;

    const int frames = file.numFrames();
    analysis::LoudnessMeter loudness;
    loudness.prepare(file.sampleRate, numCh);
    analysis::TruePeakMeter truePeak;
    truePeak.prepare(numCh);

    std::vector<analysis::SignalStatistics> stats(static_cast<std::size_t>(numCh));
    double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;

    constexpr int kChunk = 4096;
    for (int pos = 0; pos < frames; pos += kChunk) {
        const int n = std::min(kChunk, frames - pos);
        const double* ptrs[2] = {file.channels[0].data() + pos,
                                 file.channels[static_cast<std::size_t>(numCh - 1)].data() + pos};
        loudness.process(ptrs, n);
        truePeak.process(ptrs, n);
        for (int ch = 0; ch < numCh; ++ch) stats[static_cast<std::size_t>(ch)].add(ptrs[ch], n);
        if (numCh == 2) {
            for (int i = 0; i < n; ++i) {
                const double l = ptrs[0][i], rr = ptrs[1][i];
                sumLR += l * rr;
                sumLL += l * l;
                sumRR += rr * rr;
            }
        }
    }
    // Vidage : la fenêtre du true-peak a 24 échantillons de retard.
    {
        std::vector<double> zeros(analysis::TruePeakMeter::kHalfLength + 1, 0.0);
        const double* ptrs[2] = {zeros.data(), zeros.data()};
        truePeak.process(ptrs, static_cast<int>(zeros.size()));
    }

    r.integratedLufs = loudness.integratedLufs();
    r.maxMomentaryLufs = loudness.maxMomentaryLufs();
    r.maxShortTermLufs = loudness.maxShortTermLufs();
    r.loudnessRangeLu = loudness.loudnessRangeLu();
    r.truePeakDb = gainToDb(truePeak.truePeakMax());
    r.samplePeakDb = gainToDb(truePeak.samplePeakMax());
    r.plrDb = r.truePeakDb - r.integratedLufs;
    r.psrDb = r.truePeakDb - r.maxShortTermLufs;

    if (numCh == 2) {
        const double denom = std::sqrt(sumLL * sumRR);
        r.correlation = denom > 0.0 ? sumLR / denom : 0.0;
        const double mid = 0.25 * (sumLL + 2.0 * sumLR + sumRR);
        const double side = 0.25 * (sumLL - 2.0 * sumLR + sumRR);
        r.sideToMidDb = mid > 0.0 ? powerToDb(std::max(side, 0.0) / mid) : kMinusInfinityDb;
    } else {
        r.correlation = 1.0;
        r.sideToMidDb = kMinusInfinityDb;
    }

    for (int ch = 0; ch < numCh; ++ch) {
        const auto& s = stats[static_cast<std::size_t>(ch)];
        ChannelReport c;
        c.rmsDb = gainToDb(s.rms());
        c.samplePeakDb = gainToDb(truePeak.samplePeak(ch));
        c.truePeakDb = gainToDb(truePeak.truePeak(ch));
        c.crestFactorDb = s.crestFactorDb();
        c.dcOffset = s.mean();
        c.skewness = s.skewness();
        c.excessKurtosis = s.excessKurtosis();
        // Plateaux d'écrêtage.
        const auto& x = file.channels[static_cast<std::size_t>(ch)];
        int run = 0;
        for (double v : x) {
            if (std::fabs(v) >= 0.999) {
                if (++run == 3) ++c.clippedRuns;
            } else {
                run = 0;
            }
        }
        r.channels.push_back(c);
    }
    return r;
}

std::string AnalysisReport::toText(const std::string& title) const {
    std::ostringstream o;
    if (!title.empty()) o << "== " << title << " ==\n";
    o << "Format          : " << fmt(sampleRate, 0) << " Hz, " << numChannels << " canal(aux), " << fmt(durationSeconds, 2)
      << " s\n";
    o << "Integrated      : " << fmt(integratedLufs, 1) << " LUFS\n";
    o << "Max Momentary   : " << fmt(maxMomentaryLufs, 1) << " LUFS\n";
    o << "Max Short-Term  : " << fmt(maxShortTermLufs, 1) << " LUFS\n";
    o << "Loudness Range  : " << fmt(loudnessRangeLu, 1) << " LU\n";
    o << "True Peak       : " << fmt(truePeakDb, 2) << " dBTP\n";
    o << "Sample Peak     : " << fmt(samplePeakDb, 2) << " dBFS\n";
    o << "PLR (TP - I)    : " << fmt(plrDb, 1) << " dB\n";
    o << "PSR (TP - Smax) : " << fmt(psrDb, 1) << " dB\n";
    if (numChannels == 2) {
        o << "Correlation     : " << fmt(correlation, 3) << "\n";
        o << "Side / Mid      : " << fmt(sideToMidDb, 1) << " dB\n";
    }
    static const char* const kNames[] = {"L", "R"};
    for (std::size_t ch = 0; ch < channels.size(); ++ch) {
        const auto& c = channels[ch];
        o << "[" << (numChannels == 1 ? "M" : kNames[ch]) << "] RMS " << fmt(c.rmsDb, 1) << " dBFS | crest "
          << fmt(c.crestFactorDb, 1) << " dB | TP " << fmt(c.truePeakDb, 2) << " dBTP | DC " << fmt(c.dcOffset * 1e6, 1)
          << " ppm | skew " << fmt(c.skewness, 3) << " | kurt " << fmt(c.excessKurtosis, 2) << " | clip runs "
          << c.clippedRuns << "\n";
    }
    return o.str();
}

std::string AnalysisReport::toJson() const {
    std::ostringstream o;
    o << "{\n";
    o << "  \"sample_rate\": " << json(sampleRate) << ",\n";
    o << "  \"duration_s\": " << json(durationSeconds) << ",\n";
    o << "  \"channels\": " << numChannels << ",\n";
    o << "  \"integrated_lufs\": " << json(integratedLufs) << ",\n";
    o << "  \"max_momentary_lufs\": " << json(maxMomentaryLufs) << ",\n";
    o << "  \"max_short_term_lufs\": " << json(maxShortTermLufs) << ",\n";
    o << "  \"loudness_range_lu\": " << json(loudnessRangeLu) << ",\n";
    o << "  \"true_peak_dbtp\": " << json(truePeakDb) << ",\n";
    o << "  \"sample_peak_dbfs\": " << json(samplePeakDb) << ",\n";
    o << "  \"plr_db\": " << json(plrDb) << ",\n";
    o << "  \"psr_db\": " << json(psrDb) << ",\n";
    o << "  \"correlation\": " << json(correlation) << ",\n";
    o << "  \"side_to_mid_db\": " << json(sideToMidDb) << ",\n";
    o << "  \"per_channel\": [\n";
    for (std::size_t ch = 0; ch < channels.size(); ++ch) {
        const auto& c = channels[ch];
        o << "    {\"rms_dbfs\": " << json(c.rmsDb) << ", \"sample_peak_dbfs\": " << json(c.samplePeakDb)
          << ", \"true_peak_dbtp\": " << json(c.truePeakDb) << ", \"crest_db\": " << json(c.crestFactorDb)
          << ", \"dc\": " << json(c.dcOffset) << ", \"skewness\": " << json(c.skewness)
          << ", \"excess_kurtosis\": " << json(c.excessKurtosis) << ", \"clipped_runs\": " << c.clippedRuns << "}"
          << (ch + 1 < channels.size() ? "," : "") << "\n";
    }
    o << "  ]\n}\n";
    return o.str();
}

} // namespace omnis::tools
