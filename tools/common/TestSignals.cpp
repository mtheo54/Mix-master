// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/TestSignals.cpp
// =============================================================================
#include "TestSignals.h"

#include "core/Decibels.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>

namespace omnis::tools::signals {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

int framesFor(double sampleRate, double seconds) { return std::max(0, static_cast<int>(std::lround(sampleRate * seconds))); }

void normalizePeak(AudioFile& f, double peakDb) {
    double peak = 0.0;
    for (const auto& ch : f.channels)
        for (double v : ch) peak = std::max(peak, std::fabs(v));
    if (peak <= 0.0) return;
    const double g = dbToGain(peakDb) / peak;
    for (auto& ch : f.channels)
        for (double& v : ch) v *= g;
}

void normalizeRms(AudioFile& f, double rmsDb) {
    // RMS par canal ramené à la cible (le bruit est stationnaire).
    for (auto& ch : f.channels) {
        double sum = 0.0;
        for (double v : ch) sum += v * v;
        const double rms = ch.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(ch.size()));
        if (rms <= 0.0) continue;
        const double g = dbToGain(rmsDb) / rms;
        for (double& v : ch) v *= g;
    }
}

/// Bruit rose « refined » de Paul Kellet : somme de 7 filtres à un pôle
/// approximant une pente de -3 dB/octave à ±0.05 dB au-dessus de 9.2 Hz
/// (à 44.1 kHz ; la plage se décale proportionnellement à fs).
class PinkFilter {
public:
    double process(double white) {
        b0_ = 0.99886 * b0_ + white * 0.0555179;
        b1_ = 0.99332 * b1_ + white * 0.0750759;
        b2_ = 0.96900 * b2_ + white * 0.1538520;
        b3_ = 0.86650 * b3_ + white * 0.3104856;
        b4_ = 0.55000 * b4_ + white * 0.5329522;
        b5_ = -0.7616 * b5_ - white * 0.0168980;
        const double pink = b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * 0.5362;
        b6_ = white * 0.115926;
        return pink;
    }

private:
    double b0_ = 0, b1_ = 0, b2_ = 0, b3_ = 0, b4_ = 0, b5_ = 0, b6_ = 0;
};

/// Biquad RBJ minimal (forme directe I) pour la synthèse de la boucle.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    static Biquad bandPass(double fs, double f0, double q) {
        const double w = kTwoPi * f0 / fs, alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
        Biquad b;
        b.b0 = alpha / a0;
        b.b1 = 0;
        b.b2 = -alpha / a0;
        b.a1 = -2 * std::cos(w) / a0;
        b.a2 = (1 - alpha) / a0;
        return b;
    }
    static Biquad highPass(double fs, double f0, double q) {
        const double w = kTwoPi * f0 / fs, alpha = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + alpha;
        Biquad b;
        b.b0 = (1 + c) / 2 / a0;
        b.b1 = -(1 + c) / a0;
        b.b2 = (1 + c) / 2 / a0;
        b.a1 = -2 * c / a0;
        b.a2 = (1 - alpha) / a0;
        return b;
    }
    double process(double x) {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

std::complex<double> dftBin(const std::vector<double>& x, double frequency, double sampleRate) {
    // Rotation récursive du phaseur (pas de cos/sin par échantillon), renormalisée
    // périodiquement pour borner la dérive numérique.
    const std::complex<double> step = std::polar(1.0, -kTwoPi * frequency / sampleRate);
    std::complex<double> phasor{1.0, 0.0};
    std::complex<double> acc{0.0, 0.0};
    for (std::size_t n = 0; n < x.size(); ++n) {
        acc += x[n] * phasor;
        phasor *= step;
        if ((n & 1023u) == 1023u) phasor /= std::abs(phasor);
    }
    return acc;
}

} // namespace

void applyFades(AudioFile& file, double fadeInMs, double fadeOutMs) {
    const int frames = file.numFrames();
    const int fadeIn = std::min(frames, framesFor(file.sampleRate, fadeInMs * 1e-3));
    const int fadeOut = std::min(frames, framesFor(file.sampleRate, fadeOutMs * 1e-3));
    for (auto& ch : file.channels) {
        for (int i = 0; i < fadeIn; ++i) ch[static_cast<std::size_t>(i)] *= 0.5 - 0.5 * std::cos(kPi * i / fadeIn);
        for (int i = 0; i < fadeOut; ++i)
            ch[static_cast<std::size_t>(frames - 1 - i)] *= 0.5 - 0.5 * std::cos(kPi * i / fadeOut);
    }
}

SweepResult logSweep(double sampleRate, double seconds, double f1, double f2, double levelDb, double fadeMs) {
    // x(t) = A·sin( ω1·L·(e^(t/L) − 1) ),  L = T / ln(ω2/ω1)
    // Fréquence instantanée : f(t) = f1·e^(t/L) -> temps égal par octave, donc
    // spectre d'énergie rose (∝ 1/f). Le filtre inverse est le sweep renversé
    // dans le temps et pondéré par e^(−t/L) (+6 dB/oct) pour aplatir le produit.
    const int n = framesFor(sampleRate, seconds);
    const double length = seconds / std::log(f2 / f1);
    const double amplitude = dbToGain(levelDb);
    SweepResult r;
    r.sweep.sampleRate = r.inverse.sampleRate = sampleRate;
    r.sweep.resize(2, n);
    r.inverse.resize(1, n);
    for (int i = 0; i < n; ++i) {
        const double t = i / sampleRate;
        const double v = amplitude * std::sin(kTwoPi * f1 * length * (std::exp(t / length) - 1.0));
        r.sweep.channels[0][static_cast<std::size_t>(i)] = v;
    }
    applyFades(r.sweep, fadeMs, std::min(fadeMs, 10.0));
    r.sweep.channels[1] = r.sweep.channels[0];

    auto& inv = r.inverse.channels[0];
    for (int i = 0; i < n; ++i) {
        const double t = i / sampleRate;
        inv[static_cast<std::size_t>(i)] = r.sweep.channels[0][static_cast<std::size_t>(n - 1 - i)] * std::exp(-t / length);
    }
    // Normalisation : |X(1 kHz)·F(1 kHz)| = 1, i.e. la déconvolution d'un
    // système neutre donne un Dirac d'amplitude 1 (0 dB à 1 kHz).
    const double fRef = std::clamp(1000.0, f1 * 2.0, f2 / 2.0);
    const double gain = std::abs(dftBin(r.sweep.channels[0], fRef, sampleRate) * dftBin(inv, fRef, sampleRate));
    if (gain > 0.0)
        for (double& v : inv) v /= gain;
    return r;
}

AudioFile pinkNoise(double sampleRate, double seconds, double rmsDb, std::uint64_t seed, bool stereoDecorrelated) {
    AudioFile f;
    f.sampleRate = sampleRate;
    const int n = framesFor(sampleRate, seconds);
    f.resize(2, n);
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> gauss(0.0, 1.0);
    PinkFilter filters[2];
    // Pré-chauffe : on laisse s'établir le pôle le plus lent (τ ≈ 880 éch.).
    for (int i = 0; i < 8192; ++i) {
        filters[0].process(gauss(rng));
        filters[1].process(gauss(rng));
    }
    for (int i = 0; i < n; ++i) {
        const double l = filters[0].process(gauss(rng));
        const double r = stereoDecorrelated ? filters[1].process(gauss(rng)) : l;
        f.channels[0][static_cast<std::size_t>(i)] = l;
        f.channels[1][static_cast<std::size_t>(i)] = r;
    }
    normalizeRms(f, rmsDb);
    applyFades(f, 20.0, 20.0);
    return f;
}

AudioFile whiteNoise(double sampleRate, double seconds, double rmsDb, std::uint64_t seed) {
    AudioFile f;
    f.sampleRate = sampleRate;
    const int n = framesFor(sampleRate, seconds);
    f.resize(2, n);
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> gauss(0.0, 1.0);
    for (auto& ch : f.channels)
        for (double& v : ch) v = gauss(rng);
    normalizeRms(f, rmsDb);
    applyFades(f, 20.0, 20.0);
    return f;
}

AudioFile impulse(double sampleRate, double seconds, double levelDb, double positionSeconds) {
    AudioFile f;
    f.sampleRate = sampleRate;
    f.resize(2, framesFor(sampleRate, seconds));
    const int pos = std::clamp(framesFor(sampleRate, positionSeconds), 0, std::max(0, f.numFrames() - 1));
    if (f.numFrames() > 0)
        for (auto& ch : f.channels) ch[static_cast<std::size_t>(pos)] = dbToGain(levelDb);
    return f;
}

AudioFile levelSteps(double sampleRate, double frequency, double lowDb, double highDb, double lowSeconds,
                     double highSeconds, int repetitions) {
    AudioFile f;
    f.sampleRate = sampleRate;
    const int low = framesFor(sampleRate, lowSeconds);
    const int high = framesFor(sampleRate, highSeconds);
    const int total = repetitions * (low + high) + low;
    f.resize(2, total);
    const double aLow = dbToGain(lowDb), aHigh = dbToGain(highDb);
    for (int i = 0; i < total; ++i) {
        const int phaseInCycle = i % (low + high);
        const bool isHigh = i < repetitions * (low + high) && phaseInCycle >= low;
        // Phase continue : seul l'enveloppe saute (échelon idéal).
        const double v = (isHigh ? aHigh : aLow) * std::sin(kTwoPi * frequency * i / sampleRate);
        f.channels[0][static_cast<std::size_t>(i)] = v;
        f.channels[1][static_cast<std::size_t>(i)] = v;
    }
    applyFades(f, 5.0, 5.0);
    return f;
}

AudioFile drumLoop(double sampleRate, int bars, double bpm, double peakDb, std::uint64_t seed) {
    AudioFile f;
    f.sampleRate = sampleRate;
    const double stepSeconds = 60.0 / bpm / 4.0; // double-croche
    const int stepsTotal = bars * 16;
    const int n = framesFor(sampleRate, stepsTotal * stepSeconds + 1.5);
    f.resize(2, n);
    auto& left = f.channels[0];
    auto& right = f.channels[1];
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);

    // Motif trap demi-tempo : 808 + kick sur 0/6/10, clap sur 8, hats en
    // double-croches avec roulements de triples-croches en fin de mesure.
    constexpr int kKickSteps[] = {0, 6, 10};
    constexpr double k808Notes[] = {49.0, 49.0, 58.27}; // G1, G1, A#1

    // --- 808 monophonique (une nouvelle note coupe la précédente) ---
    {
        struct Note {
            int start;
            double frequency;
        };
        std::vector<Note> notes;
        for (int step = 0; step < stepsTotal; ++step)
            for (int k = 0; k < 3; ++k)
                if (step % 16 == kKickSteps[k]) notes.push_back({framesFor(sampleRate, step * stepSeconds), k808Notes[k]});

        const int declickLength = std::max(1, framesFor(sampleRate, 0.005));
        std::size_t next = 0;
        int noteStart = -1;
        double noteFreq = 49.0;
        double phase = 0.0;
        double lastValue = 0.0;     // dernière valeur de la note coupée
        int declickRemaining = 0;
        for (int i = 0; i < n; ++i) {
            if (next < notes.size() && i == notes[next].start) {
                noteStart = i;
                noteFreq = notes[next].frequency;
                phase = 0.0;
                declickRemaining = declickLength;
                ++next;
            }
            if (noteStart < 0) continue;
            const double nt = (i - noteStart) / sampleRate;
            // Glissando 180 Hz -> note (τ = 30 ms) et décroissance longue.
            const double freq = noteFreq + (180.0 - noteFreq) * std::exp(-nt / 0.03);
            phase += kTwoPi * freq / sampleRate;
            const double env = (1.0 - std::exp(-nt / 0.001)) * std::exp(-nt / 0.6);
            double v = std::tanh(1.8 * env * std::sin(phase)) / std::tanh(1.8); // saturation 808 typique
            // Retrigger sans clic : la valeur où la note précédente a été
            // coupée décroît linéairement vers 0 en 5 ms.
            if (declickRemaining > 0) {
                v += lastValue * static_cast<double>(declickRemaining) / declickLength;
                --declickRemaining;
            } else {
                lastValue = v;
            }
            left[static_cast<std::size_t>(i)] += 0.8 * v;
            right[static_cast<std::size_t>(i)] += 0.8 * v;
        }
    }

    // --- Événements percussifs ---
    auto addHit = [&](int start, auto&& generator, int length, double panL, double panR) {
        for (int j = 0; j < length && start + j < n; ++j) {
            const double v = generator(j / sampleRate);
            left[static_cast<std::size_t>(start + j)] += panL * v;
            right[static_cast<std::size_t>(start + j)] += panR * v;
        }
    };

    for (int step = 0; step < stepsTotal; ++step) {
        const int start = framesFor(sampleRate, step * stepSeconds);
        const int s = step % 16;
        // Click de kick : impulsion 3 kHz très courte (transitoire d'attaque).
        for (int k : kKickSteps) {
            if (s == k) {
                addHit(start, [](double t) { return 0.6 * std::exp(-t / 0.002) * std::sin(kTwoPi * 3000.0 * t); },
                       framesFor(sampleRate, 0.02), 1.0, 1.0);
            }
        }
        // Clap : trois rafales de bruit passe-bande espacées de 10 ms.
        if (s == 8) {
            Biquad bp = Biquad::bandPass(sampleRate, 1200.0, 1.2);
            addHit(start,
                   [&](double t) {
                       const double burst = std::max({std::exp(-t / 0.004) * (t >= 0.0),
                                                      std::exp(-(t - 0.010) / 0.004) * (t >= 0.010),
                                                      std::exp(-(t - 0.020) / 0.15) * (t >= 0.020)});
                       return 2.5 * bp.process(uni(rng) * burst);
                   },
                   framesFor(sampleRate, 0.5), 1.0, 1.0);
        }
        // Hi-hats : bruit passe-haut 8 kHz, décroissance 25 ms, vélocité
        // variable, panoramique alterné (contenu Side).
        const int subdivisions = (s >= 14) ? 2 : 1;
        for (int sub = 0; sub < subdivisions; ++sub) {
            const int hatStart = start + framesFor(sampleRate, sub * stepSeconds / subdivisions);
            const double velocity = (s % 4 == 0 ? 0.35 : 0.22) * (1.0 + 0.15 * uni(rng));
            const bool leftSide = (step + sub) % 2 == 0;
            Biquad hp = Biquad::highPass(sampleRate, std::min(8000.0, 0.4 * sampleRate), 0.707);
            addHit(hatStart, [&](double t) { return velocity * hp.process(uni(rng)) * std::exp(-t / 0.025); },
                   framesFor(sampleRate, 0.12), leftSide ? 1.0 : 0.55, leftSide ? 0.55 : 1.0);
        }
    }

    // Bloqueur de DC à 5 Hz : l'enveloppe asymétrique de la 808 (premier
    // demi-cycle non compensé) crée un offset continu, absent d'un vrai mix
    // passé dans un passe-haut. y[n] = x[n] − x[n−1] + R·y[n−1], R = e^(−2π·fc/fs)
    const double r = std::exp(-kTwoPi * 5.0 / sampleRate);
    for (auto& ch : f.channels) {
        double x1 = 0.0, y1 = 0.0;
        for (double& v : ch) {
            const double y = v - x1 + r * y1;
            x1 = v;
            y1 = y;
            v = y;
        }
    }

    normalizePeak(f, peakDb);
    applyFades(f, 0.0, 200.0);
    return f;
}

AudioFile intersamplePeak(double sampleRate, double seconds, double truePeakDb) {
    // x[n] = A·sin(π·n/2 + π/4) : les échantillons tombent à ±A/√2, la crête
    // réelle A se situe exactement entre deux échantillons.
    AudioFile f;
    f.sampleRate = sampleRate;
    const int n = framesFor(sampleRate, seconds);
    f.resize(2, n);
    const double a = dbToGain(truePeakDb);
    for (int i = 0; i < n; ++i) {
        const double v = a * std::sin(kPi * 0.5 * i + kPi * 0.25);
        f.channels[0][static_cast<std::size_t>(i)] = v;
        f.channels[1][static_cast<std::size_t>(i)] = v;
    }
    // Fondus : un démarrage abrupt est un transitoire large bande dont le
    // ringing (Gibbs) dépasserait la crête stationnaire que l'on veut mesurer.
    applyFades(f, 20.0, 20.0);
    return f;
}

AudioFile twoTone(double sampleRate, double seconds, double f1, double f2, double ratio, double peakDb) {
    AudioFile f;
    f.sampleRate = sampleRate;
    const int n = framesFor(sampleRate, seconds);
    f.resize(2, n);
    const double peak = dbToGain(peakDb);
    const double a1 = peak * ratio / (ratio + 1.0);
    const double a2 = peak / (ratio + 1.0);
    for (int i = 0; i < n; ++i) {
        const double t = i / sampleRate;
        const double v = a1 * std::sin(kTwoPi * f1 * t) + a2 * std::sin(kTwoPi * f2 * t);
        f.channels[0][static_cast<std::size_t>(i)] = v;
        f.channels[1][static_cast<std::size_t>(i)] = v;
    }
    applyFades(f, 20.0, 20.0);
    return f;
}

AudioFile ebu3341(double sampleRate, int caseNumber) {
    // EBU Tech 3341 (v4) — sinus 1 kHz stéréo, niveaux en dBFS crête.
    struct Segment {
        double levelDb;
        double seconds;
    };
    std::vector<Segment> segments;
    switch (caseNumber) {
        case 1: segments = {{-23.0, 20.0}}; break;
        case 2: segments = {{-33.0, 20.0}}; break;
        case 3: segments = {{-36.0, 10.0}, {-23.0, 60.0}, {-36.0, 10.0}}; break;
        case 4: segments = {{-72.0, 10.0}, {-36.0, 10.0}, {-23.0, 60.0}, {-36.0, 10.0}, {-72.0, 10.0}}; break;
        case 5: segments = {{-26.0, 20.0}, {-20.0, 20.1}, {-26.0, 20.0}}; break;
        default: return {};
    }
    AudioFile f;
    f.sampleRate = sampleRate;
    int total = 0;
    for (const auto& s : segments) total += framesFor(sampleRate, s.seconds);
    f.resize(2, total);
    int i = 0;
    for (const auto& s : segments) {
        const int len = framesFor(sampleRate, s.seconds);
        const double a = dbToGain(s.levelDb);
        for (int j = 0; j < len; ++j, ++i) {
            const double v = a * std::sin(kTwoPi * 1000.0 * i / sampleRate);
            f.channels[0][static_cast<std::size_t>(i)] = v;
            f.channels[1][static_cast<std::size_t>(i)] = v;
        }
    }
    return f;
}

AudioFile stereoImaging(double sampleRate, double secondsPerPart, double rmsDb, std::uint64_t seed) {
    const AudioFile mono = pinkNoise(sampleRate, secondsPerPart, rmsDb, seed, false);
    const AudioFile wide = pinkNoise(sampleRate, secondsPerPart, rmsDb, seed + 1, true);
    AudioFile f;
    f.sampleRate = sampleRate;
    const int part = mono.numFrames();
    f.resize(2, 3 * part);
    for (int i = 0; i < part; ++i) {
        const auto k = static_cast<std::size_t>(i);
        // 1. Mono (L = R)       -> corrélation +1, Side nul
        f.channels[0][k] = mono.channels[0][k];
        f.channels[1][k] = mono.channels[0][k];
        // 2. Anti-phase (L = −R) -> corrélation −1, Mid nul
        f.channels[0][k + static_cast<std::size_t>(part)] = mono.channels[0][k];
        f.channels[1][k + static_cast<std::size_t>(part)] = -mono.channels[0][k];
        // 3. Décorrélé           -> corrélation ≈ 0, M/S ≈ 0 dB
        f.channels[0][k + 2 * static_cast<std::size_t>(part)] = wide.channels[0][k];
        f.channels[1][k + 2 * static_cast<std::size_t>(part)] = wide.channels[1][k];
    }
    return f;
}

std::vector<std::string> availableSignals() {
    return {"sweep",     "sweep-inverse", "pink",      "white",      "impulse",    "steps",
            "drums",     "isp",           "imd-smpte", "imd-ccif",   "hf-sweep",   "stereo",
            "ebu3341-1", "ebu3341-2",     "ebu3341-3", "ebu3341-4",  "ebu3341-5"};
}

bool generate(const std::string& name, double fs, AudioFile& out, std::string& description) {
    const double fTop = std::min(20000.0, 0.45 * fs);
    if (name == "sweep" || name == "sweep-inverse") {
        auto r = logSweep(fs, 10.0, 20.0, fTop, -6.0);
        out = name == "sweep" ? std::move(r.sweep) : std::move(r.inverse);
        description = name == "sweep" ? "Sweep exponentiel 20 Hz - 20 kHz, 10 s, -6 dBFS (Farina)"
                                      : "Filtre inverse du sweep (deconvolution -> reponse impulsionnelle)";
    } else if (name == "pink") {
        out = pinkNoise(fs, 30.0, -20.0);
        description = "Bruit rose stereo decorrele, 30 s, -20 dBFS RMS";
    } else if (name == "white") {
        out = whiteNoise(fs, 30.0, -20.0);
        description = "Bruit blanc gaussien stereo, 30 s, -20 dBFS RMS";
    } else if (name == "impulse") {
        out = impulse(fs, 2.0, -6.0, 0.5);
        description = "Dirac a t = 0.5 s, -6 dBFS (latence, pre-ringing FIR)";
    } else if (name == "steps") {
        out = levelSteps(fs, 1000.0, -30.0, -10.0, 2.0, 1.0, 4);
        description = "Sinus 1 kHz, paliers -30/-10 dBFS (2 s / 1 s) x4 : attaque/release compresseur";
    } else if (name == "drums") {
        out = drumLoop(fs, 8);
        description = "Boucle trap 140 BPM, 808 saturee + click + clap + hats, crete -1 dBFS";
    } else if (name == "isp") {
        out = intersamplePeak(fs, 10.0, 0.0);
        description = "Sinus fs/4 a 45 deg : sample peak -3.01 dBFS, true peak 0.00 dBTP";
    } else if (name == "imd-smpte") {
        out = twoTone(fs, 10.0, 60.0, 7000.0, 4.0, -6.0);
        description = "IMD SMPTE 60 Hz + 7 kHz (4:1), crete -6 dBFS";
    } else if (name == "imd-ccif") {
        out = twoTone(fs, 10.0, std::min(19000.0, 0.43 * fs), std::min(20000.0, 0.45 * fs), 1.0, -6.0);
        description = "IMD CCIF 19 kHz + 20 kHz (1:1), crete -6 dBFS : produit a 1 kHz + repliements";
    } else if (name == "hf-sweep") {
        out = logSweep(fs, 10.0, 1000.0, fTop, -1.0).sweep;
        description = "Sweep 1 - 20 kHz a -1 dBFS : cartographie de l'aliasing des saturations";
    } else if (name == "stereo") {
        out = stereoImaging(fs, 10.0, -20.0);
        description = "Bruit rose : 10 s mono, 10 s anti-phase, 10 s decorrele";
    } else if (name.rfind("ebu3341-", 0) == 0 && name.size() == 9 && name[8] >= '1' && name[8] <= '5') {
        const int c = name[8] - '0';
        out = ebu3341(fs, c);
        static const char* const kExpected[] = {"", "M = S = I = -23.0 LUFS", "M = S = I = -33.0 LUFS",
                                                "I = -23.0 LUFS", "I = -23.0 LUFS", "I = -23.0 LUFS"};
        description = std::string("EBU Tech 3341 cas ") + name[8] + " : attendu " + kExpected[c] + " (+/-0.1)";
    } else {
        return false;
    }
    return true;
}

} // namespace omnis::tools::signals
