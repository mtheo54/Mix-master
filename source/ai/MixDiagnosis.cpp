// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/MixDiagnosis.cpp
// =============================================================================
#include "MixDiagnosis.h"

#include "ReferenceLibrary.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace omnis::ai {

namespace {

std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

/// Écart moyen (dB) de la prod à la référence sur les nœuds [first, last], relatif au corps.
double regionGap(const analysis::SpectralSignature& p, const analysis::SpectralSignature& r, int first, int last) {
    double sum = 0.0;
    int n = 0;
    for (int b = first; b <= last; ++b) {
        if (!p.trusts(b) || !r.trusts(b)) continue;
        sum += p.relativeMidDb(b) - r.relativeMidDb(b);
        ++n;
    }
    return n ? sum / n : 0.0;
}

/// La note (classe 0 … 11, 0 = Do) d'une fréquence.
int pitchClass(double hz) { return ((static_cast<int>(std::lround(12.0 * std::log2(hz / 440.0))) + 9) % 12 + 12) % 12; }

bool inScale(int pc, int root, bool minor) {
    static constexpr int kMajor[7] = {0, 2, 4, 5, 7, 9, 11}, kMinor[7] = {0, 2, 3, 5, 7, 8, 10};
    const int d = ((pc - root) % 12 + 12) % 12;
    for (int k : minor ? kMinor : kMajor)
        if (k == d) return true;
    return false;
}

const analysis::SpectralSignature& baseAverage() {
    static const analysis::SpectralSignature avg = [] {
        std::vector<const ReferenceEntry*> all;
        for (const auto& e : ReferenceLibrary::builtin().entries())
            if (e.measured) all.push_back(&e);
        return aggregateSignature(all);
    }();
    return avg;
}

} // namespace

std::vector<MixFinding> diagnoseMix(const analysis::SpectralSignature& prod, const analysis::SpectralSignature* reference) {
    using L = MixFinding::Level;
    std::vector<MixFinding> out;
    const auto& ref = reference ? *reference : baseAverage();
    const auto& m = prod.music;

    // 1. Ce que le master ne peut pas réparer : un mix qui écrête déjà, sans marge.
    if (m.valid && (m.clipPercent > 0.02 || m.samplePeakDb > -0.1))
        out.push_back({L::Important, "Ton mix touche déjà 0 dBFS" + (m.clipPercent > 0.02 ? fmt(" (%.2f %% d'échantillons écrêtés)", m.clipPercent) : std::string()) + ".",
                       "Baisse le master de FL d'environ 6 dB (crêtes vers -6 dBFS) et retire limiteur / clipper du master : OMNIS a besoin de marge."});
    else if (prod.integratedLufs > -9.0 && prod.plrDb < 8.0)
        out.push_back({L::Important, "Le mix est déjà très fort et écrasé" + fmt(" (%.1f LUFS", prod.integratedLufs) + fmt(", crête - sonie %.1f dB).", prod.plrDb),
                       "Un limiteur ou un clipper est sans doute resté sur le master de FL : enlève-le, laisse OMNIS faire la sonie."});

    // 2. Le grave doit être mono (club, téléphone, vinyle).
    if (m.valid && m.lowCorrelation < 0.8)
        out.push_back({L::Important, fmt("Le grave n'est pas mono (corrélation %.2f sous 120 Hz).", m.lowCorrelation),
                       "La 808 ou le sub a de la largeur (chorus, reverb, sample stéréo) : passe-les en mono dans FL, ou monte le Mono Maker du module Sub."});
    if (m.valid && m.monoLossDb > 2.5)
        out.push_back({L::Advice, fmt("En mono, le mix perd %.1f dB.", m.monoLossDb),
                       "Des éléments sont en opposition de phase (doubleurs, élargisseurs, pans extrêmes) : vérifie-les en mono dans FL et réduis leur largeur."});

    // 3. L'équilibre tonal, comparé à la référence.
    const double sub = regionGap(prod, ref, 2, 4), lowMid = regionGap(prod, ref, 7, 9), mid = regionGap(prod, ref, 12, 14);
    const double presence = regionGap(prod, ref, 15, 17), air = regionGap(prod, ref, 18, 20);
    if (sub > 3.0)
        out.push_back({L::Advice, fmt("Le sub / la 808 est plus fort que la référence (+%.1f dB).", sub),
                       "Baisse la 808 de quelques dB dans le mixeur : au master, trop de sub mange la sonie de tout le reste."});
    else if (sub < -4.0)
        out.push_back({L::Advice, fmt("Le sub manque par rapport à la référence (%.1f dB).", sub),
                       "Monte la 808, ou vérifie qu'un EQ / un passe-haut ne la coupe pas. Choisis une 808 qui descend vraiment (40-60 Hz)."});
    if (lowMid > 2.5)
        out.push_back({L::Advice, fmt("Le bas-médium s'accumule vers 250-450 Hz (+%.1f dB) : son boueux.", lowMid),
                       "Creuse 250-400 Hz sur la mélodie, les pads et le haut de la 808 (pas sur la voix) : chaque piste y laisse un peu, tout s'empile."});
    if (mid > 3.0)
        out.push_back({L::Advice, fmt("Les médiums 1-2,5 kHz ressortent (+%.1f dB) : son dur, \"nasal\".", mid),
                       "Cherche la piste qui pique (lead, sample, voix trop compressée) et baisse-la de 2-3 dB vers 1,5 kHz."});
    if (presence > 3.0)
        out.push_back({L::Advice, fmt("Les aigus 3,5-7 kHz sont durs (+%.1f dB) : fatigue, sifflantes.", presence),
                       "Mets un de-esser sur la voix et baisse un peu les hats / le clap : au master, ça ne ferait que tout assombrir."});
    else if (presence < -3.5)
        out.push_back({L::Advice, fmt("Il manque de présence (%.1f dB vers 4-6 kHz) : la voix paraît loin.", presence),
                       "Monte la voix de 1-2 dB ou ajoute de la présence sur elle (3-5 kHz) plutôt que sur tout le mix."});
    if (air < -4.0 && prod.trusts(18))
        out.push_back({L::Info, fmt("Il manque d'air au-dessus de 10 kHz (%.1f dB).", air),
                       "Normal si c'est voulu (son sombre) ; sinon, un peu d'aigu sur la voix et les hats dans le mix, le module Match fera le reste."});

    // 4. Le punch : kick et 808 qui se masquent.
    if (m.valid && ref.music.valid && m.microDb[0] < ref.music.microDb[0] - 2.0)
        out.push_back({L::Advice, fmt("Le grave manque d'attaque (%.1f dB de punch de moins que la référence).", m.microDb[0] - ref.music.microDb[0]),
                       "Souvent le kick et la 808 tombent ensemble et se masquent : sidechain la 808 sur le kick, raccourcis le début de la 808, ou accorde le kick avec elle."});

    // 5. Remarque seulement : la 808 hors de la gamme (parfois voulu).
    if (m.valid && m.subHz > 25.0 && m.keyRoot >= 0 && m.keyConfidence >= 0.5 && !inScale(pitchClass(m.subHz), m.keyRoot, m.keyMinor))
        out.push_back({L::Info, "La 808 tient " + analysis::MusicDescriptors::noteName(m.subHz) + ", hors de la gamme du morceau (" + m.keyName() + ").",
                       "Si c'est voulu (dissonance, côté méchant), garde-la. Sinon, accorde-la (pitch de la 808 dans FL)."});

    std::stable_sort(out.begin(), out.end(), [](const MixFinding& a, const MixFinding& b) { return a.level > b.level; });
    return out;
}

std::string formatDiagnosis(const std::vector<MixFinding>& findings) {
    if (findings.empty()) return "Rien d'alarmant dans le mix : équilibre, grave mono et marge corrects. OMNIS peut masteriser tel quel.";
    std::string s;
    for (const auto& f : findings) {
        const char* tag = f.level == MixFinding::Level::Important ? "[IMPORTANT] " : f.level == MixFinding::Level::Advice ? "[CONSEIL] " : "[INFO] ";
        s += std::string(tag) + f.problem + " -> " + f.fix + "\n";
    }
    return s;
}

} // namespace omnis::ai
