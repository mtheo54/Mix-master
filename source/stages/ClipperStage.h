// =============================================================================
//  Abrasion Labs // OMNIS
//  stages/ClipperStage.h — Étage 5 : Smart Wave-Shaping / Mastering Clipper.
//
//  Clipper à DEUX BANDES (sub / reste), séparées par un crossover à phase
//  linéaire complémentaire :
//
//      x ─┬─► FIR LP Linkwitz-Riley (f_split) ───────────────► bas   ─► clip (drive + sub drive)
//         └─► retard N/2 + B ──► − bas ───────────────────────► haut  ─► clip (drive)
//                                                       sortie = bas' + haut'
//
//  Pourquoi : mesuré sur les mixes fournis (docs/CALIBRATION_MODULE3.md §6),
//  un clipper large bande écrête la 808 — qui porte 78 % de l'énergie — et la
//  voix, les snares et les synthés qui « roulent » sur sa forme d'onde se font
//  intermoduler : +8 dB de distorsion dans les médiums. Séparé, le sub peut
//  être écrêté franchement (« 808 ultra-clippée mais sub propre ») sans salir
//  le reste, et le haut n'est écrêté que par ses propres transitoires.
//  La séparation est exacte (haut = entrée retardée − bas) : sans écrêtage,
//  la sortie est l'entrée retardée, à l'arrondi près.
//
//  Chaque bande : suréchantillonnage (1x … 16x, défaut 8x, HIIR phase minimale),
//  courbe ADAA (Hard / Soft Knee / Diode, fondues sans clic), plafond commun.
//
//  PARTAGE DE MARGE (« Sub Make-Room ») : deux bandes chacune au plafond
//  peuvent sommer à +6 dB ; laisser le limiteur raboter ce dépassement coûtait
//  jusqu'à 2 LU (mesuré) et modulait tout le spectre au rythme de la 808. On
//  impose plutôt |g·bas + haut| <= plafond en baissant UNIQUEMENT le gain g de
//  la bande sub, lissé (anticipation 5 ms, release 80 ms) :
//        g_requis = min(1, (±C − haut) / bas)   (borne la plus haute admissible)
//  La 808 cède la place aux transitoires (l'effet « sidechain » du genre) et la
//  bande haute — voix, caisse claire — n'est jamais modulée par le sub.
//  DÉSACTIVÉ PAR DÉFAUT et borné à −6 dB : sans borne, à forte poussée, la 808
//  cédait en permanence et le sub perdait 14 dB (retour d'écoute : « plus de
//  sub »). Le garde-fou d'équilibre tonal de omnis_tune vérifie désormais tout réglage.
//
//  Latence constante : N/2 + B du crossover + retard de phase 16x arrondi
//  + anticipation du partage de marge.
// =============================================================================
#pragma once

#include "core/DelayLine.h"
#include "core/OnePoleSmoother.h"
#include "dsp/FirKernelBank.h"
#include "dsp/LinearPhaseDesigner.h"
#include "dsp/LookaheadGain.h"
#include "dsp/Oversampler.h"
#include "dsp/StereoConvolver.h"
#include "dsp/Waveshaper.h"
#include "engine/Stage.h"

#include <array>
#include <vector>

namespace omnis {

struct SplitKernelRequest {
    double splitHz = 120.0;
    [[nodiscard]] bool nearlyEquals(const SplitKernelRequest& o) const noexcept {
        return std::abs(splitHz - o.splitHz) <= 1e-4 * o.splitHz;
    }
    /// Passe-bas Linkwitz-Riley 24 dB/oct : |H| = 1 / (1 + (f/fc)⁴).
    [[nodiscard]] double lowMagnitude(double f) const noexcept {
        const double w = f / splitHz, w2 = w * w;
        return 1.0 / (1.0 + w2 * w2);
    }
};

/// Même passe-bas sur les deux canaux du convolueur (L et R).
class SplitKernelDesigner {
public:
    void prepare(double sampleRate, int kernelLength, int blockSize) { designer_.prepare(sampleRate, kernelLength, blockSize); }
    void design(const SplitKernelRequest& r, dsp::StereoConvolver::Kernel& out) noexcept {
        const auto mag = [&](double f) { return r.lowMagnitude(f); };
        designer_.design(mag, mag, out);
    }

private:
    dsp::LinearPhaseDesigner designer_;
};

class ClipperStage final : public Stage {
public:
    ClipperStage();
    ~ClipperStage() override;

    [[nodiscard]] const char* name() const noexcept override { return "Mastering Clipper"; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(double* const* channels, int numSamples) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override { return latency_; }
    void setParameter(params::ParamId id, double plainValue, bool immediate) noexcept override;
    void setRealtime(bool realtime) noexcept override { realtime_ = realtime; }

    /// Longueur du FIR de séparation (résolution ~5 Hz) et taille de partition.
    static int splitKernelLengthFor(double sampleRate) noexcept;

private:
    using SplitBank = dsp::FirKernelBank<SplitKernelRequest, SplitKernelDesigner>;

    struct Band {
        dsp::Oversampler oversampler;
        std::array<dsp::AdaaClipper, 2> clippers{};
        std::array<double, 2> dcX1{}, dcY1{};
        std::array<std::vector<double>, 2> buffer; ///< signal de la bande, taux de base
    };

    void applyOversamplingChoice() noexcept;
    void splitFrameBoundary() noexcept;
    void clipBand(Band& band, const std::vector<double>& gainIn, int numSamples) noexcept;

    double sampleRate_ = 48000.0;
    bool realtime_ = false;
    int latency_ = 0;
    int splitLatency_ = 0;
    int requestedStages_ = 3;
    std::array<int, dsp::Oversampler::kMaxStages + 1> paddingForStages_{};
    double dcCoefficient_ = 0.0;

    OnePoleSmoother drive_;    ///< gain linéaire
    OnePoleSmoother subDrive_; ///< gain linéaire supplémentaire de la bande basse
    OnePoleSmoother ceiling_;  ///< gain linéaire
    OnePoleSmoother knee_;     ///< 0..1
    OnePoleSmoother splitLog_; ///< ln(f_split)
    std::array<OnePoleSmoother, 3> modeWeight_; ///< Hard, Soft, Diode

    // Séparation de bandes.
    dsp::StereoConvolver splitter_;
    SplitBank bank_;
    SplitBank::Slot* activeSlot_ = nullptr;
    SplitBank::Slot* fadingSlot_ = nullptr;
    std::vector<double> fadeRamp_;
    int framePos_ = 0;
    std::array<std::vector<double>, 2> inputDelay_;
    int inputDelayPos_ = 0;

    Band low_, high_;
    DelayLine padding_;

    // Partage de marge.
    static constexpr double kRoomLookaheadMs = 5.0;
    static constexpr double kRoomReleaseMs = 80.0;
    static constexpr double kRoomMaxDepthDb = 6.0; ///< baisse maximale de la 808 à 100 %
    dsp::LookaheadGain room_;
    OnePoleSmoother roomAmount_; ///< 0..1
    double roomRelease_ = 0.0;
    double roomFloorAmount_ = -1.0, roomFloor_ = 1.0; ///< plancher du partage de marge, recalculé quand la dose change
    std::array<std::vector<double>, 2> roomDelayLow_, roomDelayHigh_;
    int roomDelayPos_ = 0;
    int roomLatency_ = 0;

    // Paramètres au taux de base pour le bloc courant.
    std::vector<double> gainLow_, gainHigh_, gainOut_, knees_, wHard_, wSoft_, wDiode_;
};

} // namespace omnis
