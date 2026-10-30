// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisEditor.cpp
// =============================================================================
#include "OmnisEditor.h"

#include "OmnisController.h"
#include "UiFeed.h"
#include "gui/Look.h"
#include "gui/Widgets.h"

#include "dsp/Fft.h"
#include "dsp/Waveshaper.h"
#include "core/UserDataDir.h"
#include "params/Parameters.h"
#include "stages/InflatorStage.h"

#include "public.sdk/source/vst/utility/stringconvert.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cviewcontainer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <numbers>

using namespace VSTGUI;
using namespace Steinberg;
using omnis::params::ParamId;

namespace omnis::vst3 {

using namespace gui;

namespace {

std::uint32_t id(ParamId p) { return static_cast<std::uint32_t>(p); }

constexpr double kPad = 10.0;
// Colonne de travail | colonne Master.
constexpr double kWorkL = 10.0, kWorkR = 932.0, kMasterL = 942.0, kMasterR = 1190.0;
constexpr double kRibbon1 = 10.0, kRibbon2 = 58.0, kRackT = 106.0, kRackB = 306.0, kDrawerT = 314.0, kDrawerB = 604.0;
constexpr double kScreensT = 612.0, kScreensB = 740.0;
constexpr double kDrawerSplit = 557.0; // réglages (60 %) | gorge | écran (40 %)

const std::array<const char*, 7> kModuleTitles{"Sub", "Match EQ", "Inflator", "Imager", "Multiband", "Clipper", "Limiter"};

/// Un fond de panneau : face anodisée, découpe usinée.
class Panel : public CViewContainer {
public:
    explicit Panel(const CRect& r, bool insetLook = true) : CViewContainer(r), insetLook_(insetLook) { setTransparency(false); }
    void drawBackgroundRect(CDrawContext* c, const CRect&) override {
        c->setDrawMode(kAntiAliasing | kNonIntegralMode);
        const CRect r(0, 0, getViewSize().getWidth(), getViewSize().getHeight());
        if (insetLook_) inset(c, r);
        else {
            c->setFillColor(color::kMetal);
            c->drawRect(r, kDrawFilled);
        }
    }

private:
    bool insetLook_;
};

/// La face avant : aluminium brossé très fin.
class Face : public CViewContainer {
public:
    explicit Face(const CRect& r) : CViewContainer(r) { setTransparency(false); }
    void drawBackgroundRect(CDrawContext* c, const CRect&) override {
        const CRect r(0, 0, getViewSize().getWidth(), getViewSize().getHeight());
        c->setFillColor(color::kVoid);
        c->drawRect(r, kDrawFilled);
        c->setFillColor(CColor(24, 25, 29));
        c->drawRect(CRect(r).inset(2, 2), kDrawFilled);
        c->setFillColor(color::kMetal);
        c->drawRect(CRect(r.left + 2, r.top + 80, r.right - 2, r.bottom - 2), kDrawFilled);
        c->setFillColor(CColor(19, 20, 23));
        c->drawRect(CRect(r.left + 2, r.bottom - 200, r.right - 2, r.bottom - 2), kDrawFilled);
        c->setLineWidth(1.0);
        c->setFrameColor(CColor(255, 255, 255, 3));
        for (CCoord x = 3; x < r.right - 2; x += 3) c->drawLine(CPoint(x, 3), CPoint(x, r.bottom - 3));
        c->setFrameColor(CColor(255, 255, 255, 15));
        c->drawLine(CPoint(3, 2.5), CPoint(r.right - 3, 2.5));
    }
};

/// Gorge usinée entre les réglages et l'écran du tiroir.
class Groove : public CView {
public:
    explicit Groove(const CRect& r) : CView(r) {}
    void draw(CDrawContext* c) override {
        const CRect r = getViewSize();
        c->setFillColor(CColor(0, 0, 0, 255));
        c->drawRect(r, kDrawFilled);
        c->setFillColor(CColor(11, 11, 12));
        c->drawRect(CRect(r.left + 3, r.top, r.right - 3, r.bottom), kDrawFilled);
        c->setFrameColor(CColor(255, 255, 255, 12));
        c->drawLine(CPoint(r.right - 0.5, r.top), CPoint(r.right - 0.5, r.bottom));
        setDirty(false);
    }
};

/// Texte fixe dessiné (en-têtes, aides).
class Caption : public CView {
public:
    Caption(const CRect& r, std::string s, CColor col, SharedPointer<CFontDesc> font, CHoriTxtAlign align = kLeftText)
        : CView(r), s_(std::move(s)), col_(col), font_(std::move(font)), align_(align) {
        setMouseEnabled(false);
    }
    void draw(CDrawContext* c) override {
        text(c, s_, getViewSize(), col_, font_, align_);
        setDirty(false);
    }

private:
    std::string s_;
    CColor col_;
    SharedPointer<CFontDesc> font_;
    CHoriTxtAlign align_;
};

/// Écouteur d'une commande VSTGUI (menus, champ de texte) par une fonction.
class FnListener : public IControlListener {
public:
    explicit FnListener(std::function<void(CControl*)> f) : f_(std::move(f)) {}
    void valueChanged(CControl* c) override { f_(c); }

private:
    std::function<void(CControl*)> f_;
};

/// Menu RÉFÉRENCE : un menu déroulant (sous-menus Styles, Artistes, Titres)
/// dont la face montre le choix en cours, pas l'entrée du menu principal.
class RefMenu : public COptionMenu {
public:
    using Thumb = std::function<void(CDrawContext*, const CRect&)>;
    RefMenu(const CRect& r, IControlListener* l, std::function<std::string()> label, Thumb thumb)
        : COptionMenu(r, l, -1), label_(std::move(label)), thumb_(std::move(thumb)) {}
    void draw(CDrawContext* c) override {
        const CRect r = getViewSize();
        c->setFillColor(color::kScreen);
        c->setFrameColor(CColor(0, 0, 0, 255));
        c->setLineWidth(1.0);
        c->drawRect(r, kDrawFilledAndStroked);
        std::string name = label_();
        const auto font = mono(10, !name.empty());
        // Choisie : sa pochette à la place de « RÉF ».
        if (name.empty()) engraved(c, "RÉF", CRect(r.left + 7, r.top, r.left + 36, r.bottom));
        else thumb_(c, CRect(r.left + 3, r.top + 3, r.left + 3 + r.getHeight() - 6, r.bottom - 3));
        // Un titre long est coupé (« … ») avant le triangle.
        const CRect area(r.left + (name.empty() ? 36 : r.getHeight() + 2), r.top, r.right - 18, r.bottom);
        c->setFont(font);
        const bool none = name.empty();
        if (!none && c->getStringWidth(name.c_str()) > area.getWidth()) {
            while (!name.empty() && c->getStringWidth((name + "…").c_str()) > area.getWidth()) {
                unsigned char last = 0;
                do { // un caractère UTF-8 entier
                    last = static_cast<unsigned char>(name.back());
                    name.pop_back();
                } while (!name.empty() && (last & 0xC0) == 0x80);
            }
            name += "…";
        }
        text(c, none ? std::string("aucune") : name, area, none ? color::kText3 : color::kAccent, font);
        // Petit triangle : ça se déroule.
        const double x = r.right - 10, y = r.getCenter().y;
        auto path = VSTGUI::owned(c->createGraphicsPath());
        if (path) {
            path->beginSubpath(CPoint(x - 4, y - 2));
            path->addLine(CPoint(x + 4, y - 2));
            path->addLine(CPoint(x, y + 3));
            path->closeSubpath();
            c->setFillColor(color::kText2);
            c->drawGraphicsPath(path, CDrawContext::kPathFilled);
        }
        setDirty(false);
    }

private:
    std::function<std::string()> label_;
    Thumb thumb_;
};

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

/// Abscisse logarithmique d'une fréquence dans [lo, hi].
double xOfHz(double hz, double lo, double hi, const CRect& r) {
    return r.left + (std::log(std::max(hz, lo) / lo) / std::log(hi / lo)) * r.getWidth();
}
double hzOfX(double x, double lo, double hi, const CRect& r) { return lo * std::pow(hi / lo, clamp01((x - r.left) / r.getWidth())); }

std::string fmt(const char* f, double v) {
    char b[48];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

std::string hzText(double hz) { return hz >= 1000.0 ? fmt(hz >= 10000.0 ? "%.0fk" : "%.1fk", hz / 1000.0) : fmt("%.0f", hz); }

void gridLines(CDrawContext* c, const CRect& r, double lo, double hi, bool labels) {
    c->setLineWidth(1.0);
    for (const double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0}) {
        if (f <= lo || f >= hi) continue;
        const double x = std::round(xOfHz(f, lo, hi, r)) + 0.5;
        c->setFrameColor(color::kGrid);
        c->drawLine(CPoint(x, r.top + 4), CPoint(x, r.bottom - 4));
        if (labels) text(c, hzText(f), CRect(x - 20, r.bottom - 14, x + 20, r.bottom - 2), color::kText3, mono(8), kCenterText);
    }
}

} // namespace

// -----------------------------------------------------------------------------
//  Tutoriel : un calque qui éclaire une zone à la fois
// -----------------------------------------------------------------------------
namespace {

struct TutorialStep {
    CRect focus; ///< zone éclairée (coordonnées du cadre)
    const char* title;
    std::array<const char*, 4> lines;
};

const std::array<TutorialStep, 10> kTutorial{{
    {CRect(0, 0, 0, 0), "BIENVENUE DANS OMNIS",
     {"OMNIS mixe et masterise ta prod. Deux façons de l'utiliser :", "1. seul sur le master : il masterise la prod entière ;",
      "2. sur tes pistes (Stem) + un Master : chaque piste est travaillée,", "   et le master écoute tout le monde. Suis le guide (2 minutes)."}},
    {CRect(942, 560, 1190, 730), "RÔLE ET GROUPE",
     {"Seul : OMNIS sur le master, rien d'autre à faire.", "Stem : sur une piste (808, drums, voix…), sans aucune latence.",
      "Master : sur le bus master ; il entend les stems du même groupe.", "Un groupe par projet ouvert (1 à 8)."}},
    {CRect(10, 106, 932, 306), "LE RACK : 7 MODULES",
     {"Chaque module a un gros bouton (le réglage principal) et un voyant ON.", "Clique un module pour ouvrir TOUS ses réglages en dessous.",
      "Glisse un module par son nom pour changer l'ordre de la chaîne.", "La barre d'état montre ce qu'il fait en ce moment."}},
    {CRect(10, 314, 932, 604), "LE TIROIR : TOUT RÉGLER",
     {"À gauche, tous les réglages du module ouvert ; à droite, son écran.", "Glisser vers le haut / le bas · Maj : réglage fin · double-clic : défaut.",
      "Match EQ : glisse les nœuds sur l'écran. Multiband : glisse les traits.", "Rien n'est caché : tu choisis précisément ce que tu changes."}},
    {CRect(10, 58, 932, 98), "LA BARRE IA",
     {"Écris ce que tu veux, en français, en anglais ou en argot :", "« plus de punch », « garde ma 808 », « genre rage », « moins boueux »…",
      "OMNIS écoute ta prod, comprend, règle les modules et te dit ce qu'il a fait.", "RÉF : choisis un style, un artiste ou un titre de ta base ; AUTO le vise."}},
    {CRect(196, 10, 272, 50), "AUTO",
     {"Clique AUTO, puis joue le passage le plus fort (refrain, drop).", "OMNIS écoute 10 s, règle toute la chaîne vers ton style",
      "et amène le volume à la bonne sonie. Tout reste réglable après.", "La cible : la RÉF choisie, sinon le style écrit (« rap us », « rage »)."}},
    {CRect(270, 10, 830, 50), "ÉCOUTER HONNÊTEMENT",
     {"A / B : deux réglages complets ; A>B copie l'un dans l'autre.", "MATCH : écoute au même volume que l'original (ça clignote : pense à l'enlever).",
      "DELTA : n'entends que ce que le module ouvert change.", "STÉRÉO / MID / SIDE : vérifie le centre et les côtés."}},
    {CRect(942, 10, 1190, 552), "LES MESURES",
     {"Entrée / sortie, crêtes gauche / droite, sonie, réduction du limiteur.", "Chaque mesure a son verdict en mots : BON, FORT, DENSE, PUNCHY…",
      "TRUE PEAK doit rester OK (sous -1 dBTP) pour le streaming.", "PLR = écart crête / sonie : plus il est grand, plus ça respire."}},
    {CRect(942, 654, 1190, 720), "CHEF D'ORCHESTRE ET PISTE",
     {"Sur le Master : les propositions quand un son en gêne un autre", "(OUI / NON, rien n'est appliqué sans ton accord).",
      "Sur un Stem : la page PISTE montre l'élément reconnu et sa chaîne.", ""}},
    {CRect(796, 10, 830, 50), "C'EST PARTI",
     {"Le bouton ? relance ce guide quand tu veux.", "Astuce : en mastering, plusieurs petites touches valent mieux", "qu'un gros réglage.", "Bon son !"}},
}};

std::filesystem::path tutorialFlag() { return userDataDir() / "tutoriel_vu.txt"; }

} // namespace

class TutorialOverlay : public CView {
public:
    TutorialOverlay(const CRect& r, OmnisEditor& editor) : CView(r), editor_(editor) {}
    void draw(CDrawContext* c) override {
        c->setDrawMode(kAntiAliasing | kNonIntegralMode);
        const int step = std::clamp(editor_.tutorialStep_, 0, static_cast<int>(kTutorial.size()) - 1);
        const TutorialStep& s = kTutorial[static_cast<std::size_t>(step)];
        const CRect all = getViewSize();
        const CColor shade(0, 0, 0, 185);
        c->setFillColor(shade);
        if (s.focus.getWidth() <= 0) {
            c->drawRect(all, kDrawFilled);
        } else {
            const CRect f = CRect(s.focus).extend(4, 4);
            c->drawRect(CRect(all.left, all.top, all.right, f.top), kDrawFilled);
            c->drawRect(CRect(all.left, f.bottom, all.right, all.bottom), kDrawFilled);
            c->drawRect(CRect(all.left, f.top, f.left, f.bottom), kDrawFilled);
            c->drawRect(CRect(f.right, f.top, all.right, f.bottom), kDrawFilled);
            c->setLineWidth(2.0);
            c->setFrameColor(color::kAccent);
            c->drawRect(f, kDrawStroked);
            c->setLineWidth(1.0);
        }
        card_ = cardRect(s.focus);
        inset(c, card_, CColor(20, 21, 24));
        c->setFrameColor(color::kAccent35);
        c->drawRect(CRect(card_).inset(0.5, 0.5), kDrawStroked);
        text(c, s.title, CRect(card_.left + 18, card_.top + 14, card_.right - 90, card_.top + 34), color::kAccent, sans(14, true));
        text(c, std::to_string(step + 1) + " / " + std::to_string(kTutorial.size()), CRect(card_.right - 90, card_.top + 14, card_.right - 18, card_.top + 34),
             color::kText3, mono(10), kRightText);
        for (int i = 0; i < 4; ++i)
            text(c, s.lines[static_cast<std::size_t>(i)], CRect(card_.left + 18, card_.top + 44 + 20 * i, card_.right - 18, card_.top + 62 + 20 * i), color::kText,
                 mono(10.5));
        const bool last = step == static_cast<int>(kTutorial.size()) - 1;
        next_ = CRect(card_.right - 130, card_.bottom - 40, card_.right - 18, card_.bottom - 14);
        skip_ = CRect(card_.left + 18, card_.bottom - 40, card_.left + 130, card_.bottom - 14);
        back_ = CRect(card_.right - 196, card_.bottom - 40, card_.right - 140, card_.bottom - 14);
        const auto button = [&](const CRect& r, const char* label, bool accent) {
            inset(c, r, CColor(34, 35, 39));
            text(c, label, r, accent ? color::kAccent : color::kText2, mono(10.5, accent), kCenterText);
        };
        button(next_, last ? "TERMINER" : "SUIVANT", true);
        if (!last) button(skip_, "PASSER", false);
        if (step > 0) button(back_, "<", false);
        setDirty(false);
    }
    void onMouseDownEvent(MouseDownEvent& e) override {
        e.consumed = true; // le guide est modal
        e.ignoreFollowUpMoveAndUpEvents(true);
        const CPoint p = e.mousePosition;
        const int step = editor_.tutorialStep_;
        if (next_.pointInside(p)) editor_.startTutorial(step + 1 >= static_cast<int>(kTutorial.size()) ? -1 : step + 1);
        else if (skip_.pointInside(p) && step + 1 < static_cast<int>(kTutorial.size())) editor_.startTutorial(-1);
        else if (back_.pointInside(p) && step > 0) editor_.startTutorial(step - 1);
    }

private:
    CRect cardRect(const CRect& focus) const {
        const double w = 560, h = 190;
        const CRect all = getViewSize();
        if (focus.getWidth() <= 0) return CRect((all.getWidth() - w) / 2, (all.getHeight() - h) / 2, (all.getWidth() + w) / 2, (all.getHeight() + h) / 2);
        // Sous la zone si la place le permet, sinon au-dessus ; à gauche de la colonne Master.
        double x = std::clamp(focus.left, 20.0, 930.0 - w);
        if (focus.left >= 940.0) x = 930.0 - w;
        double y = focus.bottom + 16;
        if (y + h > all.bottom - 10) y = std::max(10.0, focus.top - h - 16);
        if (focus.left >= 940.0) y = std::clamp(focus.top, 20.0, all.bottom - h - 20);
        return CRect(x, y, x + w, y + h);
    }
    OmnisEditor& editor_;
    CRect card_, next_, skip_, back_;
};

void OmnisEditor::startTutorial(int step) {
    tutorialStep_ = step;
    if (step < 0) {
        std::error_code ec;
        std::filesystem::create_directories(tutorialFlag().parent_path(), ec);
        std::ofstream(tutorialFlag()) << "vu\n";
    }
    if (tutorial_) {
        tutorial_->setVisible(step >= 0);
        tutorial_->invalid();
    }
}

// -----------------------------------------------------------------------------
//  Analyse du flux (thread de l'interface)
// -----------------------------------------------------------------------------
struct OmnisEditor::Analysis {
    static constexpr int kFft = 2048, kKeep = 4096, kBands = 64, kHistory = 240;
    std::vector<float> ring = std::vector<float>(2 * kKeep, 0.0f);
    int ringPos = 0;
    std::vector<float> pop = std::vector<float>(1 << 15);
    dsp::Fft fft{kFft};
    std::vector<std::complex<double>> work = std::vector<std::complex<double>>(kFft);
    std::array<float, kBands> spectrum{};
    std::array<float, kHistory> limHistory{}, clipHistory{};
    int historyPos = 0;
    UiMeters meters{};
    std::array<float, 2> peakHold{-120.0f, -120.0f};
    std::array<int, 2> holdTicks{};
    double sampleRate = 48000.0;
    std::uint32_t lastSequence = 0;
    std::map<std::string, SharedPointer<CBitmap>> pictures; ///< images/ : chargées une fois (nullptr : illisible)
    std::map<std::string, SharedPointer<CBitmap>> icons;    ///< icônes du menu RÉF
    Analysis() { spectrum.fill(-120.0f); }
    void push(const float* x, std::size_t frames) {
        for (std::size_t i = 0; i < frames; ++i) {
            ring[static_cast<std::size_t>(2 * ringPos)] = x[2 * i];
            ring[static_cast<std::size_t>(2 * ringPos + 1)] = x[2 * i + 1];
            ringPos = (ringPos + 1) % kKeep;
        }
    }
    [[nodiscard]] float sample(int ago, int ch) const {
        const int i = ((ringPos - 1 - ago) % kKeep + kKeep) % kKeep;
        return ring[static_cast<std::size_t>(2 * i + ch)];
    }
    void analyse() {
        for (int i = 0; i < kFft; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (kFft - 1));
            const int ago = kFft - 1 - i;
            work[static_cast<std::size_t>(i)] = {w * 0.5 * (sample(ago, 0) + sample(ago, 1)), 0.0};
        }
        fft.forward(work.data());
        // Bandes de 1/6 d'octave (20 Hz … 20 kHz), pente +4,5 dB/oct (un bruit rose est plat).
        const double binHz = sampleRate / kFft;
        for (int b = 0; b < kBands; ++b) {
            const double f0 = 20.0 * std::pow(1000.0, static_cast<double>(b) / kBands);
            const double f1 = 20.0 * std::pow(1000.0, static_cast<double>(b + 1) / kBands);
            int k0 = static_cast<int>(f0 / binHz), k1 = std::max(k0 + 1, static_cast<int>(f1 / binHz));
            k1 = std::min(k1, kFft / 2);
            double p = 0.0;
            for (int k = k0; k < k1; ++k) p = std::max(p, std::norm(work[static_cast<std::size_t>(k)]));
            const double fc = std::sqrt(f0 * f1);
            const double db = 10.0 * std::log10(std::max(p, 1e-20) * 4.0 / (kFft * kFft / 4.0)) + 4.5 * std::log2(fc / 1000.0);
            float& s = spectrum[static_cast<std::size_t>(b)];
            s = static_cast<float>(db > s ? db : std::max(db, static_cast<double>(s) - 1.5));
        }
    }
};

// -----------------------------------------------------------------------------
OmnisEditor::OmnisEditor(OmnisController* controller) : VSTGUIEditor(controller), an_(std::make_unique<Analysis>()) {
    ViewRect r(0, 0, kWidth, kHeight);
    setRect(r);
}

OmnisEditor::~OmnisEditor() = default;

OmnisController* OmnisEditor::omnis() const { return static_cast<OmnisController*>(getController()); }

double OmnisEditor::norm(std::uint32_t pid) const { return omnis()->getParamNormalized(pid); }

double OmnisEditor::plain(std::uint32_t pid) const {
    const auto* spec = params::findParameter(pid);
    return spec ? params::toPlain(*spec, norm(pid)) : 0.0;
}

void OmnisEditor::setNorm(std::uint32_t pid, double v) { omnis()->editParameter(pid, clamp01(v)); }

void OmnisEditor::setPlain(std::uint32_t pid, double v) {
    if (const auto* spec = params::findParameter(pid)) setNorm(pid, params::toNormalized(*spec, params::clampPlain(*spec, v)));
}

int OmnisEditor::role() const { return static_cast<int>(std::lround(plain(id(ParamId::LinkRole)))); }

std::string OmnisEditor::valueText(std::uint32_t pid) const {
    const auto* spec = params::findParameter(pid);
    if (!spec) return {};
    std::string s = params::formatValue(*spec, params::toPlain(*spec, norm(pid)));
    if (!spec->unit.empty() && spec->kind != params::ParamKind::Choice && spec->kind != params::ParamKind::Toggle) s += " " + std::string(spec->unit);
    return s;
}

Model OmnisEditor::model(std::uint32_t pid) const {
    Model m;
    OmnisController* c = omnis();
    const auto* spec = params::findParameter(pid);
    m.get = [c, pid] { return c->getParamNormalized(pid); };
    m.set = [c, pid](double v) { c->performGesture(pid, v); };
    m.begin = [c, pid] { c->beginGesture(pid); };
    m.end = [c, pid] { c->endGesture(pid); };
    m.text = [this, pid] { return valueText(pid); };
    if (spec) {
        m.def = params::toNormalized(*spec, spec->defaultValue);
        // Les LED partent du neutre : 0 dB, 100 % de largeur, ou la butée gauche.
        double rest = spec->minValue;
        if (spec->minValue <= 0.0 && spec->maxValue >= 0.0) rest = 0.0; // seuils et plafonds : depuis 0 dB
        if (spec->unit == "%" && spec->maxValue >= 150.0) rest = 100.0;
        m.origin = params::toNormalized(*spec, rest);
        m.steps = spec->stepCount;
    }
    return m;
}

// -----------------------------------------------------------------------------
//  Ouverture / fermeture
// -----------------------------------------------------------------------------
bool PLUGIN_API OmnisEditor::open(void* parent, const PlatformType& platformType) {
    if (frame) return false;
    frame = new CFrame(CRect(0, 0, kWidth, kHeight), this);
    buildInto(frame);
    frame->open(parent, platformType);
    timer_ = new CVSTGUITimer([this](CVSTGUITimer*) { tick(); }, 33, true);
    return true;
}

void PLUGIN_API OmnisEditor::close() {
    if (timer_) {
        timer_->forget();
        timer_ = nullptr;
    }
    if (feed_) feed_->wanted.store(false);
    feed_.reset();
    live_.clear();
    syncs_.clear();
    slots_ = {};
    pages_ = {};
    pageScreens_ = {};
    spectrum_ = scope_ = meter_ = readouts_ = stemBanner_ = cardScreen_ = nullptr;
    matchButton_ = nullptr;
    askField_ = nullptr;
    askAnswer_ = nullptr;
    rack_ = nullptr;
    tutorial_ = nullptr;
    if (frame) {
        frame->forget();
        frame = nullptr;
    }
}

IControlListener* OmnisEditor::listen(std::function<void(CControl*)> f) {
    listeners_.push_back(std::make_unique<FnListener>(std::move(f)));
    return listeners_.back().get();
}

void OmnisEditor::buildInto(CViewContainer* root) {
    live_.clear();
    syncs_.clear();
    auto* face = new Face(CRect(0, 0, kWidth, kHeight));
    root->addView(face);
    buildRibbons(face);
    buildRack(face);
    buildDrawer(face);
    buildScreens(face);
    buildMaster(face);
    omnis()->requestFeed();
    omnis()->requestProposals();
    selectPage(role() == static_cast<int>(params::LinkRole::Stem) ? kStemPage : page_);
    // Le guide, par-dessus tout ; montré tout seul à la toute première ouverture.
    tutorial_ = new TutorialOverlay(CRect(0, 0, kWidth, kHeight), *this);
    root->addView(tutorial_);
    std::error_code ec;
    startTutorial(std::filesystem::exists(tutorialFlag(), ec) ? -1 : 0);
}

// -----------------------------------------------------------------------------
//  Rubans : transport et demande à l'IA
// -----------------------------------------------------------------------------
void OmnisEditor::buildRibbons(CViewContainer* root) {
    auto* r1 = new Panel(CRect(kWorkL, kRibbon1, kWorkR, kRibbon1 + 40));
    root->addView(r1);
    r1->addView(new Caption(CRect(10, 8, 110, 32), "OMNIS", color::kText, sans(18, true)));
    r1->addView(new Caption(CRect(96, 13, 190, 29), "ABRASION LABS", color::kText3, mono(8.5)));
    OmnisController* c = omnis();
    const auto add = [&](HwButton* b) {
        r1->addView(b);
        live_.push_back(b);
        return b;
    };
    auto* autoButton = add(new HwButton(CRect(196, 7, 262, 33), "AUTO", [] { return false; }, [this] {
        answer_ = "AUTO : joue le passage le plus fort (refrain, drop) ; OMNIS écoute 10 s puis règle la chaîne.";
        omnis()->startAuto();
    }));
    autoButton->setAccentText(true);
    add(new HwButton(CRect(270, 7, 300, 33), "A", [c] { return c->abSlot() == 0; }, [c] { c->selectAB(0); }, false));
    add(new HwButton(CRect(302, 7, 332, 33), "B", [c] { return c->abSlot() == 1; }, [c] { c->selectAB(1); }, false));
    add(new HwButton(CRect(336, 7, 392, 33), c->abSlot() == 0 ? "A>B" : "B>A", [] { return false; }, [this, c] {
        c->copyABToOther();
        answer_ = c->abSlot() == 0 ? "A copié dans B." : "B copié dans A.";
    }, false));
    add(new HwButton(CRect(400, 7, 478, 33), "BYPASS", [this] { return plain(id(ParamId::Bypass)) >= 0.5; },
                     [this] { setPlain(id(ParamId::Bypass), plain(id(ParamId::Bypass)) >= 0.5 ? 0.0 : 1.0); }));
    matchButton_ = add(new HwButton(CRect(484, 7, 556, 33), "MATCH", [this] { return plain(id(ParamId::GainMatch)) >= 0.5; },
                                    [this] { setPlain(id(ParamId::GainMatch), plain(id(ParamId::GainMatch)) >= 0.5 ? 0.0 : 1.0); }));
    matchButton_->setBlink(true);
    add(new HwButton(CRect(562, 7, 636, 33), "DELTA", [this] { return plain(id(ParamId::DeltaListen)) >= 0.5; }, [this] {
        const bool on = plain(id(ParamId::DeltaListen)) >= 0.5;
        // On écoute ce que le module ouvert change.
        setPlain(id(ParamId::DeltaListen), on || page_ > 6 ? 0.0 : static_cast<double>(page_ + 1));
    }));
    auto* mon = new SegControl(CRect(644, 8, 790, 32), {"STÉRÉO", "MID", "SIDE"}, [this] { return static_cast<int>(std::lround(plain(id(ParamId::Monitor)))); },
                               [this](int i) { setPlain(id(ParamId::Monitor), i); });
    r1->addView(mon);
    live_.push_back(mon);
    add(new HwButton(CRect(796, 7, 826, 33), "?", [] { return false; }, [this] { startTutorial(0); }, false));
    auto* lat = new Screen(CRect(832, 9, 914, 31), [this](CDrawContext* dc, const CRect& r) {
        engraved(dc, "LAT", CRect(r.left + 6, r.top, r.left + 30, r.bottom));
        const double ms = an_->meters.latencyMs;
        text(dc, ms > 0.0 ? fmt("%.0f ms", ms) : "—", CRect(r.left + 28, r.top, r.right - 6, r.bottom), color::kText2, mono(10), kRightText);
    });
    r1->addView(lat);
    live_.push_back(lat);
    lat->setMouseEnabled(false);

    // Ruban 2 : la demande à l'IA.
    auto* r2 = new Panel(CRect(kWorkL, kRibbon2, kWorkR, kRibbon2 + 40));
    root->addView(r2);
    r2->addView(new Caption(CRect(10, 8, 34, 32), "IA", color::kAccent, mono(11, true)));
    auto* listener = listen([this](CControl*) { askAi(); });
    askField_ = new CTextEdit(CRect(36, 7, 330, 33), listener, 1, "");
    askField_->setFont(mono(11));
    askField_->setFontColor(color::kText);
    askField_->setBackColor(color::kScreen);
    askField_->setFrameColor(CColor(0, 0, 0, 255));
    askField_->setTextInset(CPoint(8, 0));
    askField_->setHoriAlign(kLeftText);
    askField_->setPlaceholderString("ex. plus de punch, garde ma 808");
    r2->addView(askField_);
    buildReferenceMenu(r2, CRect(336, 7, 540, 33));
    auto* applyButton = new HwButton(CRect(546, 7, 640, 33), "APPLIQUER", [] { return false; }, [this] { askAi(); }, false);
    applyButton->setAccentText(true);
    r2->addView(applyButton);
    live_.push_back(applyButton);
    auto* answer = new Screen(CRect(646, 7, 914, 33), [this](CDrawContext* dc, const CRect& r) {
        const std::string& s = answer_.empty() ? std::string("Écris ce que tu veux, OMNIS règle.") : answer_;
        text(dc, s, CRect(r.left + 8, r.top, r.right - 6, r.bottom), answer_.empty() ? color::kText3 : color::kAccent, mono(9.5));
    }, true);
    answer->setMouseEnabled(false);
    r2->addView(answer);
    live_.push_back(answer);
    // Le bouton AUTO est relié à la même zone de réponse.
    (void)autoButton;
}

struct OmnisEditor::Card {
    std::string key = "\x01";
    OmnisController::ReferenceCard data;
};

CBitmap* OmnisEditor::picture(const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = an_->pictures.find(path);
    if (it == an_->pictures.end()) {
        SharedPointer<CBitmap> b;
        if (auto platform = getPlatformFactory().createBitmapFromPath(path.c_str())) b = makeOwned<CBitmap>(platform);
        it = an_->pictures.emplace(path, b).first;
    }
    return it->second;
}

void OmnisEditor::paintCover(CDrawContext* c, const CRect& r, const std::string& imagePath, const analysis::SpectralSignature& sig,
                             const std::string& seed) {
    if (CBitmap* b = picture(imagePath)) {
        // L'image remplit le carré (recadrée au centre).
        const CPoint size = b->getSize();
        if (size.x > 0 && size.y > 0) {
            const double k = std::max(r.getWidth() / size.x, r.getHeight() / size.y);
            CRect clip;
            c->getClipRect(clip);
            CRect inside = r;
            inside.bound(clip);
            c->setClipRect(inside);
            {
                CDrawContext::Transform t(*c, CGraphicsTransform()
                                                  .scale(k, k)
                                                  .translate(r.left + (r.getWidth() - size.x * k) / 2.0, r.top + (r.getHeight() - size.y * k) / 2.0));
                c->drawBitmap(b, CRect(0, 0, size.x, size.y));
            }
            c->setClipRect(clip);
            return;
        }
    }
    // Pochette dessinée d'après la fiche : la teinte suit la tonalité (cercle des
    // quintes ; mineur plus sombre), la silhouette est la courbe Mid du titre, le
    // grain suit le nombre d'attaques. Deux titres différents ne se ressemblent pas.
    std::uint32_t h = 2166136261u;
    for (unsigned char ch : seed) h = (h ^ ch) * 16777619u;
    const auto& m = sig.music;
    const double hue = m.keyRoot >= 0 ? std::fmod(m.keyRoot * 7.0 * 30.0 + (h % 24), 360.0) : static_cast<double>(h % 360);
    CColor bg, fg, line;
    bg.fromHSV(hue, 0.55, m.keyMinor ? 0.15 : 0.22);
    fg.fromHSV(std::fmod(hue + 28.0, 360.0), 0.80, 1.0);
    line.fromHSV(hue, 0.30, 0.85);
    c->setFillColor(bg);
    c->drawRect(r, kDrawFilled);
    const double top = *std::max_element(sig.midDb.begin(), sig.midDb.end());
    const double cy = r.getCenter().y, half = r.getHeight() * 0.42;
    if (auto path = VSTGUI::owned(c->createGraphicsPath())) {
        const int n = static_cast<int>(sig.midDb.size());
        const auto x = [&](int k) { return r.left + r.getWidth() * (k + 0.5) / n; };
        const auto v = [&](int k) { return std::clamp((sig.midDb[static_cast<std::size_t>(k)] - (top - 30.0)) / 30.0, 0.04, 1.0); };
        path->beginSubpath(CPoint(r.left, cy));
        for (int k = 0; k < n; ++k) path->addLine(CPoint(x(k), cy - half * v(k)));
        path->addLine(CPoint(r.right, cy));
        for (int k = n - 1; k >= 0; --k) path->addLine(CPoint(x(k), cy + half * v(k)));
        path->closeSubpath();
        c->setFillColor(withAlpha(fg, 0.85));
        c->drawGraphicsPath(path, CDrawContext::kPathFilled);
    }
    const int grain = std::clamp(static_cast<int>(std::lround(m.onsetsPerSecond)), 2, 12);
    c->setLineWidth(1.0);
    c->setFrameColor(withAlpha(line, 0.18));
    for (int i = 1; i <= grain; ++i) {
        const double y = std::round(r.top + r.getHeight() * i / (grain + 1.0)) + 0.5;
        c->drawLine(CPoint(r.left, y), CPoint(r.right, y));
    }
    c->setFrameColor(color::kEdgeLo);
    c->drawRect(r, kDrawStroked);
}

void OmnisEditor::paintReferenceCard(CDrawContext* c, const CRect& r) {
    if (!card_) return;
    const auto& d = card_->data;
    if (d.name.empty()) {
        engraved(c, "RÉFÉRENCE VISÉE", CRect(r.left + 10, r.top + 6, r.right - 10, r.top + 20));
        text(c, "Aucune : choisis un style, un artiste ou un titre dans RÉF.", CRect(r.left + 10, r.top + 26, r.right - 10, r.top + 42),
             color::kText3, mono(9.5));
        return;
    }
    const double side = r.getHeight() - 12;
    CRect cover(r.left + 6, r.top + 6, r.left + 6 + side, r.top + 6 + side);
    paintCover(c, cover, d.cover, d.signature, d.name);
    double x = cover.right + 8;
    if (!d.photo.empty()) {
        const CRect photo(x, cover.top, x + side, cover.bottom);
        paintCover(c, photo, d.photo, d.signature, d.name);
        x = photo.right + 10;
    }
    engraved(c, "RÉFÉRENCE VISÉE", CRect(x, r.top + 6, r.right - 8, r.top + 20));
    text(c, d.name, CRect(x, r.top + 22, r.right - 8, r.top + 40), color::kAccent, mono(11, true));
    text(c, d.detail, CRect(x, r.top + 42, r.right - 8, r.top + 56), color::kText2, mono(9.5));
    // Une photo libre ne se montre qu'avec son auteur et sa licence.
    if (!d.credit.empty()) text(c, d.credit, CRect(x, r.bottom - 20, r.right - 8, r.bottom - 6), color::kText3, mono(8.5));
}

void OmnisEditor::buildReferenceMenu(CViewContainer* parent, const CRect& r) {
    using Kind = OmnisController::RefKind;
    auto* c = omnis();
    const auto tree = c ? c->referenceTree() : OmnisController::ReferenceTree{};
    card_ = std::make_shared<Card>();
    // Ce que chaque entrée choisit, menu par menu (l'indice compte les séparateurs).
    struct Pick {
        Kind kind;
        std::string name;
    };
    auto picks = std::make_shared<std::map<COptionMenu*, std::map<int32_t, Pick>>>();
    auto* menu = new RefMenu(r, listen([this, picks](CControl* ctl) {
        auto* m = dynamic_cast<COptionMenu*>(ctl);
        if (!m || !omnis()) return;
        int32_t idx = -1;
        COptionMenu* from = m->getLastItemMenu(idx);
        const auto list = picks->find(from);
        if (list == picks->end()) return;
        const auto pick = list->second.find(idx);
        if (pick != list->second.end()) answer_ = omnis()->chooseReference(pick->second.kind, pick->second.name);
    }), [this] {
        auto* oc = omnis();
        return oc && oc->referenceKind() != Kind::None ? oc->referenceName() : std::string();
    }, [this](CDrawContext* dc, const CRect& rr) { paintCover(dc, rr, card_->data.cover, card_->data.signature, card_->data.name); });
    menu->setFont(mono(10.5));
    menu->setFontColor(color::kText);
    menu->setBackColor(color::kScreen);
    const auto add = [picks](COptionMenu* to, const std::string& label, Kind kind, const std::string& name) {
        auto* item = to->addEntry(UTF8String(label));
        (*picks)[to][to->getNbEntries() - 1] = Pick{kind, name};
        return item;
    };
    // Petite icône de menu : la pochette (ou la photo), dessinée hors écran.
    const auto icon = [this](const std::string& path, const analysis::SpectralSignature& sig, const std::string& seed) -> CBitmap* {
        const std::string key = seed + "\x1f" + path;
        auto it = an_->icons.find(key);
        if (it == an_->icons.end()) {
            SharedPointer<CBitmap> b;
            if (auto ctx = COffscreenContext::create(CPoint(20, 20))) {
                ctx->beginDraw();
                paintCover(ctx, CRect(0, 0, 20, 20), path, sig, seed);
                ctx->endDraw();
                b = ctx->getBitmap();
            }
            it = an_->icons.emplace(key, b).first;
        }
        return it->second;
    };
    const auto plural = [](std::size_t n) { return std::to_string(n) + (n > 1 ? " titres" : " titre"); };
    // Un artiste : tous ses titres, puis chacun (pochette en icône).
    const auto artistMenu = [&](COptionMenu* to, const OmnisController::ReferenceArtist& a) {
        auto* sub = new COptionMenu();
        sub->setNbItemsPerColumn(30);
        add(sub, "Tous les titres de " + a.name + " (" + plural(a.titles.size()) + ")", Kind::Artist, a.name);
        sub->addSeparator();
        for (const auto& t : a.titles)
            if (auto* item = add(sub, t.title, Kind::Title, t.name)) item->setIcon(icon(t.cover, t.signature, t.name));
        auto* item = to->addEntry(sub, UTF8String(a.name + "  (" + std::to_string(a.titles.size()) + ")"));
        if (item && !a.titles.empty()) item->setIcon(icon(a.photo, a.titles.front().signature, a.name));
        sub->forget();
    };
    add(menu, "Aucune (OMNIS suit tes mots)", Kind::None, {});
    menu->addSeparator();
    auto* styles = new COptionMenu();
    styles->setNbItemsPerColumn(30);
    for (const auto& st : tree.styles) {
        auto* sub = new COptionMenu();
        sub->setNbItemsPerColumn(30);
        add(sub, "Tout le style " + st.name + " (" + plural(st.titleCount()) + ")", Kind::Style, st.name);
        sub->addSeparator();
        for (const auto& a : st.artists) artistMenu(sub, a);
        styles->addEntry(sub, UTF8String(st.name + "  (" + std::to_string(st.titleCount()) + ")"));
        sub->forget();
    }
    if (tree.styles.empty()) styles->addEntry("(rien de mesuré)", -1, CMenuItem::kDisabled);
    menu->addEntry(styles, "Styles");
    styles->forget();
    auto* artists = new COptionMenu();
    artists->setNbItemsPerColumn(30);
    for (const auto& a : tree.artists) artistMenu(artists, a);
    if (tree.artists.empty()) artists->addEntry("(rien de mesuré)", -1, CMenuItem::kDisabled);
    menu->addEntry(artists, "Artistes de A à Z");
    artists->forget();
    parent->addView(menu);
    // La face et la carte suivent le choix (fait ici, par AUTO ou relu avec la session).
    syncs_.push_back([this, menu] {
        auto* oc = omnis();
        const std::string now = oc ? std::to_string(static_cast<int>(oc->referenceKind())) + oc->referenceName() : std::string();
        if (now != card_->key) {
            card_->key = now;
            card_->data = oc ? oc->referenceCard() : OmnisController::ReferenceCard{};
            menu->invalid();
            if (pageScreens_[1]) pageScreens_[1]->markDirty();
            if (cardScreen_) cardScreen_->markDirty();
        }
    });
}

void OmnisEditor::askAi() {
    if (!askField_) return;
    const std::string text = askField_->getText().getString();
    if (text.empty()) return;
    answer_ = omnis()->ask(text);
}

// -----------------------------------------------------------------------------
//  Le rack
// -----------------------------------------------------------------------------
std::array<int, 7> OmnisEditor::order() const {
    const auto o = params::chainOrder(static_cast<int>(std::lround(plain(id(ParamId::ChainOrder)))));
    std::array<int, 7> out{};
    for (int i = 0; i < 7; ++i) out[static_cast<std::size_t>(i)] = static_cast<int>(o[static_cast<std::size_t>(i)]);
    return out;
}

void OmnisEditor::buildRack(CViewContainer* root) {
    rack_ = new Panel(CRect(kWorkL, kRackT, kWorkR, kRackB));
    root->addView(rack_);
    const double slotW = (rack_->getViewSize().getWidth() - 16.0 - 6.0 * 6.0) / 7.0, slotH = rack_->getViewSize().getHeight() - 16.0;
    OmnisController* c = omnis();
    for (int m = 0; m < 7; ++m) {
        const auto module = static_cast<params::Module>(m);
        const std::uint32_t onId = id(params::moduleOnId(module));
        RackSlot::Callbacks cb;
        cb.isOn = [c, onId] { return c->getParamNormalized(onId) >= 0.5; };
        cb.toggle = [this, onId] { setNorm(onId, norm(onId) >= 0.5 ? 0.0 : 1.0); };
        cb.isSelected = [this, m] { return page_ == m; };
        cb.select = [this, m] { selectPage(m); };
        cb.dragMove = [this, m](double x) { dragMove(m, x); };
        cb.drop = [this, m](double x) { drop(m, x); };
        Model macro;
        switch (module) {
            case params::Module::Sub:
                cb.status = [this] { return "SUB " + fmt("%+.1f", plain(id(ParamId::SubGain))) + " · M<" + fmt("%.0f", plain(id(ParamId::MonoFreq))); };
                cb.level = [this] { return std::fabs(plain(id(ParamId::SubGain))) / 12.0; };
                macro = model(id(ParamId::SubGain));
                break;
            case params::Module::Match:
                cb.status = [this] {
                    int n = 0;
                    for (int k = 0; k < params::kMatchBands; ++k)
                        if (std::fabs(plain(id(params::matchMidId(k)))) > 0.05 || std::fabs(plain(id(params::matchSideId(k)))) > 0.05) ++n;
                    return "TILT " + fmt("%+.1f", plain(id(ParamId::MatchTilt))) + " · " + std::to_string(n) + (n > 1 ? " NŒUDS" : " NŒUD");
                };
                cb.level = [this] {
                    double m2 = std::fabs(plain(id(ParamId::MatchTilt))) / 6.0;
                    for (int k = 0; k < params::kMatchBands; ++k)
                        m2 = std::max({m2, std::fabs(plain(id(params::matchMidId(k)))) / 12.0, std::fabs(plain(id(params::matchSideId(k)))) / 12.0});
                    return m2;
                };
                macro = model(id(ParamId::MatchTilt));
                break;
            case params::Module::Inflator: {
                cb.status = [this] { return "808 " + fmt("%.0f%%", plain(id(ParamId::InflLowEffect))) + " · MIX " + fmt("%.0f%%", plain(id(ParamId::InflHighEffect))); };
                cb.level = [this] { return std::max(plain(id(ParamId::InflLowEffect)), plain(id(ParamId::InflHighEffect))) / 100.0; };
                // Les deux bandes ensemble, en gardant leur rapport.
                macro.get = [this] { return std::max(norm(id(ParamId::InflLowEffect)), norm(id(ParamId::InflHighEffect))); };
                macro.set = [this, c](double v) {
                    const double lo = norm(id(ParamId::InflLowEffect)), hi = norm(id(ParamId::InflHighEffect)), mx = std::max(lo, hi);
                    c->performGesture(id(ParamId::InflLowEffect), mx < 0.005 ? v : lo * v / mx);
                    c->performGesture(id(ParamId::InflHighEffect), mx < 0.005 ? v : hi * v / mx);
                };
                macro.begin = [c] {
                    c->beginGesture(id(ParamId::InflLowEffect));
                    c->beginGesture(id(ParamId::InflHighEffect));
                };
                macro.end = [c] {
                    c->endGesture(id(ParamId::InflLowEffect));
                    c->endGesture(id(ParamId::InflHighEffect));
                };
                macro.text = [this] { return fmt("%.0f %%", 100.0 * std::max(norm(id(ParamId::InflLowEffect)), norm(id(ParamId::InflHighEffect)))); };
                break;
            }
            case params::Module::Imager: {
                const auto w = [](int b) { return id(static_cast<ParamId>(810 + b)); };
                cb.status = [this, w] { return "WIDTH " + fmt("%.0f", plain(w(2))) + " · " + fmt("%.0f%%", plain(w(3))); };
                cb.level = [this, w] {
                    double m2 = 0.0;
                    for (int b = 0; b < 4; ++b) m2 = std::max(m2, std::fabs(plain(w(b)) - 100.0) / 100.0);
                    return m2;
                };
                // Élargit le médium-aigu et l'aigu, le bas-médium à moitié, le bas jamais.
                macro = model(w(2));
                macro.set = [this, c, w](double v) {
                    const double pct = v * 200.0;
                    c->performGesture(w(2), v);
                    c->performGesture(w(3), v);
                    c->performGesture(w(1), clamp01((100.0 + (pct - 100.0) / 2.0) / 200.0));
                };
                macro.begin = [c, w] {
                    for (int b = 1; b < 4; ++b) c->beginGesture(w(b));
                };
                macro.end = [c, w] {
                    for (int b = 1; b < 4; ++b) c->endGesture(w(b));
                };
                break;
            }
            case params::Module::Multiband: {
                cb.status = [this] { return "GR " + fmt("%.1f dB", std::min({an_->meters.multibandGrDb[0], an_->meters.multibandGrDb[1], an_->meters.multibandGrDb[2], an_->meters.multibandGrDb[3]})); };
                cb.level = [this] {
                    float g = 0.0f;
                    for (float v : an_->meters.multibandGrDb) g = std::min(g, v);
                    return -g / 12.0;
                };
                // « Glue » : les quatre bandes ensemble, de neutre à seuil −24 dB, ratio 4:1 (3:1 en bas).
                macro.get = [this] { return clamp01((plain(id(params::mbId(2, params::MbControl::Ratio))) - 1.0) / 3.0); };
                macro.set = [this, c](double t) {
                    for (int b = 0; b < 4; ++b) {
                        const double ratio = b < 2 ? 1.0 + 2.0 * t : 1.0 + 3.0 * t, thr = -24.0 * t;
                        const auto put = [&](params::MbControl k, double v) {
                            const auto pid = id(params::mbId(b, k));
                            c->performGesture(pid, params::toNormalized(*params::findParameter(pid), v));
                        };
                        put(params::MbControl::Ratio, ratio);
                        put(params::MbControl::Threshold, thr);
                        put(params::MbControl::Makeup, -0.5 * thr * (1.0 - 1.0 / ratio));
                    }
                };
                macro.begin = [c] {
                    for (int b = 0; b < 4; ++b)
                        for (auto k : {params::MbControl::Ratio, params::MbControl::Threshold, params::MbControl::Makeup}) c->beginGesture(id(params::mbId(b, k)));
                };
                macro.end = [c] {
                    for (int b = 0; b < 4; ++b)
                        for (auto k : {params::MbControl::Ratio, params::MbControl::Threshold, params::MbControl::Makeup}) c->endGesture(id(params::mbId(b, k)));
                };
                macro.text = [this] { return fmt("%.0f %%", 100.0 * clamp01((plain(id(params::mbId(2, params::MbControl::Ratio))) - 1.0) / 3.0)); };
                break;
            }
            case params::Module::Clipper:
                cb.status = [this] { return "CLIP " + fmt("%.1f dB", an_->meters.clipperGrDb); };
                cb.level = [this] { return -an_->meters.clipperGrDb / 12.0; };
                macro = model(id(ParamId::ClipDrive));
                break;
            case params::Module::Limiter:
                cb.status = [this] { return "GR " + fmt("%.1f dB", an_->meters.limiterGrDb); };
                cb.level = [this] { return -an_->meters.limiterGrDb / 12.0; };
                macro = model(id(ParamId::LimGain));
                break;
        }
        auto* slot = new RackSlot(CRect(0, 0, slotW, slotH), std::to_string(m + 1), kModuleTitles[static_cast<std::size_t>(m)], cb);
        const double kh = LedKnob::heightFor(LedKnob::Size::Big);
        static const std::array<const char*, 7> kMacroNames{"SUB", "TONE", "INFLATE", "WIDTH", "GLUE", "DRIVE", "LOUD"};
        auto* knob = new LedKnob(CRect(4, 30, slotW - 4, 30 + kh), kMacroNames[static_cast<std::size_t>(m)], macro, LedKnob::Size::Big);
        slot->addView(knob);
        live_.push_back(knob);
        rack_->addView(slot);
        live_.push_back(slot);
        slots_[static_cast<std::size_t>(m)] = slot;
    }
    stemBanner_ = new Screen(CRect(80, 82, rack_->getViewSize().getWidth() - 80, 116), [this](CDrawContext* dc, const CRect& r) {
        text(dc, "Instance en rôle STEM : la chaîne de la piste agit (page PISTE). Le rack sert au master.", CRect(r.left + 10, r.top, r.right - 10, r.bottom),
             color::kAccent, mono(10.5), kCenterText);
    });
    stemBanner_->setMouseEnabled(false);
    rack_->addView(stemBanner_);
    layoutRack();
}

void OmnisEditor::layoutRack() {
    if (!rack_) return;
    const auto o = order();
    const double slotW = (rack_->getViewSize().getWidth() - 16.0 - 6.0 * 6.0) / 7.0, slotH = rack_->getViewSize().getHeight() - 16.0;
    const bool stem = role() == static_cast<int>(params::LinkRole::Stem);
    for (int pos = 0; pos < 7; ++pos) {
        RackSlot* s = slots_[static_cast<std::size_t>(o[static_cast<std::size_t>(pos)])];
        if (!s) continue;
        const double x = 8.0 + pos * (slotW + 6.0);
        CRect r(x, 8, x + slotW, 8 + slotH);
        if (s->getViewSize() != r) {
            s->setViewSize(r);
            s->setMouseableArea(r);
        }
        s->setNumber(std::to_string(pos + 1));
        s->setDimmed(stem);
        s->setAlphaValue(stem ? 0.35f : 1.0f);
        s->setDropMark(0);
    }
    if (stemBanner_) stemBanner_->setVisible(stem);
    rack_->invalid();
}

void OmnisEditor::dragMove(int module, double frameX) {
    if (!rack_) return;
    dragFrom_ = module;
    CPoint origin(0, 0);
    rack_->localToFrame(origin);
    const double slotW = (rack_->getViewSize().getWidth() - 16.0 - 6.0 * 6.0) / 7.0;
    const double local = frameX - origin.x - 8.0;
    dropAt_ = std::clamp(static_cast<int>(std::floor((local + 3.0) / (slotW + 6.0) + 0.5)), 0, 7); // une place entre deux modules
    const auto o = order();
    for (int pos = 0; pos < 7; ++pos) {
        RackSlot* s = slots_[static_cast<std::size_t>(o[static_cast<std::size_t>(pos)])];
        if (s) s->setDropMark(pos == dropAt_ ? -1 : (pos == 6 && dropAt_ == 7 ? 1 : 0));
    }
}

void OmnisEditor::drop(int module, double frameX) {
    if (frameX < 0.0 || dragFrom_ != module || dropAt_ < 0) {
        dragFrom_ = dropAt_ = -1;
        layoutRack();
        return;
    }
    auto o = order();
    const int from = static_cast<int>(std::find(o.begin(), o.end(), module) - o.begin());
    int to = dropAt_;
    if (to > from) --to; // la place libérée par le module
    std::vector<int> v(o.begin(), o.end());
    v.erase(v.begin() + from);
    v.insert(v.begin() + std::clamp(to, 0, 6), module);
    std::array<params::Module, 7> next{};
    for (int i = 0; i < 7; ++i) next[static_cast<std::size_t>(i)] = static_cast<params::Module>(v[static_cast<std::size_t>(i)]);
    setPlain(id(ParamId::ChainOrder), params::chainOrderRank(next));
    if (to != from) answer_ = std::string(kModuleTitles[static_cast<std::size_t>(module)]) + " déplacé : la chaîne passe au son sec le temps de se recaler.";
    dragFrom_ = dropAt_ = -1;
    layoutRack();
}

// -----------------------------------------------------------------------------
//  Le tiroir : tous les réglages du module | son écran
// -----------------------------------------------------------------------------
CViewContainer* OmnisEditor::page(CViewContainer* drawer, int index, const char* number, const char* title, const char* help) {
    const CRect area(0, 0, kDrawerSplit - kWorkL, drawer->getViewSize().getHeight());
    auto* p = new CViewContainer(area);
    p->setTransparency(true);
    p->addView(new Caption(CRect(12, 10, 30, 28), number, color::kAccent, mono(11)));
    const double helpX = std::max(170.0, 40.0 + 10.0 * std::string(title).size());
    p->addView(new Caption(CRect(30, 8, helpX, 30), title, color::kText, sans(13, true)));
    p->addView(new Caption(CRect(helpX, 10, area.right - 10, 28), help, color::kText3, mono(9)));
    drawer->addView(p);
    pages_[static_cast<std::size_t>(index)] = p;
    const double sx = kDrawerSplit - kWorkL + 10.0;
    auto* scr = new Screen(CRect(sx + 6, 12, drawer->getViewSize().getWidth() - 12, drawer->getViewSize().getHeight() - 12),
                           [this, index](CDrawContext* dc, const CRect& r) { paintModuleScreen(index, dc, r); }, true);
    drawer->addView(scr);
    live_.push_back(scr);
    pageScreens_[static_cast<std::size_t>(index)] = scr;
    return p;
}

void OmnisEditor::knobs(CViewContainer* parent, const CRect& area, const char* title, const std::vector<std::uint32_t>& ids, int size) {
    // Groupe : un cadre, un nom, les boutons côte à côte.
    struct GroupBox : public CView {
        GroupBox(const CRect& r, std::string t) : CView(r), t_(std::move(t)) { setMouseEnabled(false); }
        void draw(CDrawContext* c) override {
            const CRect r = getViewSize();
            c->setFillColor(CColor(16, 17, 19));
            c->drawRect(r, kDrawFilled);
            c->setFrameColor(CColor(0, 0, 0, 255));
            c->drawRect(CRect(r).inset(0.5, 0.5), kDrawStroked);
            c->setFrameColor(CColor(255, 255, 255, 12));
            c->drawLine(CPoint(r.left + 1, r.top + 0.5), CPoint(r.right - 1, r.top + 0.5));
            engraved(c, t_, CRect(r.left + 10, r.top + 4, r.right - 6, r.top + 16));
            setDirty(false);
        }
        std::string t_;
    };
    parent->addView(new GroupBox(area, title));
    const auto sz = size == 0 ? LedKnob::Size::XSmall : (size == 2 ? LedKnob::Size::Mid : LedKnob::Size::Small);
    const double kh = LedKnob::heightFor(sz);
    const int n = static_cast<int>(ids.size());
    const double w = (area.getWidth() - 8.0) / std::max(1, n);
    for (int i = 0; i < n; ++i) {
        const auto* spec = params::findParameter(ids[static_cast<std::size_t>(i)]);
        const std::string name = spec ? std::string(spec->shortName) : std::string();
        // Noms lisibles : le nom d'affichage sans le préfixe du module.
        std::string label = spec ? std::string(spec->name) : name;
        for (const char* prefix : {"Inflate ", "Limiter ", "Clip ", "MB Sub ", "MB Low ", "MB Mid ", "MB High ", "Imager ", "Match ", "Stem "})
            if (label.rfind(prefix, 0) == 0) label = label.substr(std::string(prefix).size());
        for (auto& ch : label) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        const double x = area.left + 4 + i * w;
        auto* k = new LedKnob(CRect(x, area.top + 18, x + w, area.top + 18 + kh), label, model(ids[static_cast<std::size_t>(i)]), sz);
        parent->addView(k);
        live_.push_back(k);
    }
}

void OmnisEditor::buildDrawer(CViewContainer* root) {
    auto* drawer = new Panel(CRect(kWorkL, kDrawerT, kWorkR, kDrawerB));
    root->addView(drawer);
    drawer->addView(new Groove(CRect(kDrawerSplit - kWorkL, 12, kDrawerSplit - kWorkL + 10, drawer->getViewSize().getHeight() - 12)));
    const double W = kDrawerSplit - kWorkL; // largeur des réglages
    const auto seg = [&](CViewContainer* p, const CRect& r, const char* name, ParamId pid) {
        const auto* spec = params::findParameter(pid);
        std::vector<std::string> labels;
        for (const auto& l : spec->choices) labels.emplace_back(l);
        p->addView(new Caption(CRect(r.left, r.top - 14, r.right, r.top - 2), name, color::kText3, mono(9)));
        auto* s = new SegControl(r, labels, [this, pid] { return static_cast<int>(std::lround(plain(id(pid)) - params::findParameter(pid)->minValue)); },
                                 [this, pid](int i) { setPlain(id(pid), params::findParameter(pid)->minValue + i); });
        p->addView(s);
        live_.push_back(s);
    };
    const double row1 = 40, row2 = 150;
    using P = ParamId;
    {
        auto* p = page(drawer, 0, "1", "SUB", "Coupe l'infra-grave, met le bas en mono, dose le sub.");
        knobs(p, CRect(12, row1, 132, row1 + 104), "COUPE-BAS", {id(P::LowCutFreq)});
        knobs(p, CRect(140, row1, 360, row1 + 104), "MONO", {id(P::MonoFreq), id(P::MonoAmount)});
        knobs(p, CRect(12, row2, 360, row2 + 104), "SUB", {id(P::SubGain), id(P::SubFreq), id(P::SubSynth)});
        seg(p, CRect(380, row1 + 30, W - 14, row1 + 54), "PENTE MONO", P::MonoSlope);
        seg(p, CRect(380, row2 + 30, W - 14, row2 + 54), "PHASE DU SUB", P::SubPhaseMode);
    }
    {
        auto* p = page(drawer, 1, "2", "MATCH EQ", "Glisse les nœuds sur l'écran · Maj : fin · double-clic : 0 dB.");
        knobs(p, CRect(12, row1, 250, row1 + 104), "COURBE", {id(P::MatchAmount), id(P::MatchTilt)});
        p->addView(new Caption(CRect(270, row1 + 2, W - 14, row1 + 14), "NŒUDS ÉDITÉS", color::kText3, mono(9)));
        auto* side = new SegControl(CRect(270, row1 + 18, W - 14, row1 + 42), {"MID", "SIDE"}, [this] { return matchSide_; }, [this](int i) {
            matchSide_ = i;
            if (pageScreens_[1]) pageScreens_[1]->markDirty();
        });
        p->addView(side);
        live_.push_back(side);
        auto* flat = new HwButton(CRect(270, row1 + 54, 400, row1 + 80), "À PLAT", [] { return false; }, [this] {
            for (int k = 0; k < params::kMatchBands; ++k) setPlain(id(matchSide_ == 0 ? params::matchMidId(k) : params::matchSideId(k)), 0.0);
        }, false);
        p->addView(flat);
        live_.push_back(flat);
        // La référence visée : pochette, photo (avec son crédit), tempo, tonalité, sonie.
        cardScreen_ = new Screen(CRect(12, row2, W - 14, row2 + 72), [this](CDrawContext* dc, const CRect& r) { paintReferenceCard(dc, r); }, false);
        cardScreen_->setMouseEnabled(false);
        p->addView(cardScreen_);
        live_.push_back(cardScreen_);
        p->addView(new Caption(CRect(12, row2 + 80, W - 14, row2 + 94), "21 nœuds en Mid et en Side, d'une demi-octave (20 Hz – 20 kHz).", color::kText3, mono(9)));
        p->addView(new Caption(CRect(12, row2 + 96, W - 14, row2 + 110), "La barre IA et les références les écrivent ; ici tu les retouches.", color::kText3, mono(9)));
    }
    {
        auto* p = page(drawer, 2, "3", "INFLATOR", "Densité et chaleur, la 808 et le mix à part.");
        knobs(p, CRect(12, row1, W - 14, row1 + 104), "DOSE", {id(P::InflLowEffect), id(P::InflHighEffect), id(P::InflSplit)});
        knobs(p, CRect(12, row2, 420, row2 + 104), "COULEUR", {id(P::InflDrive), id(P::InflCurve), id(P::InflWarmth), id(P::InflOutput)});
        seg(p, CRect(430, row2 + 30, W - 14, row2 + 54), "SURÉCH.", P::InflOversampling);
    }
    {
        auto* p = page(drawer, 3, "4", "IMAGER", "Largeur stéréo par bande. 100 % = inchangé.");
        knobs(p, CRect(12, row1, W - 14, row1 + 104), "LARGEUR", {id(static_cast<P>(810)), id(static_cast<P>(811)), id(static_cast<P>(812)), id(static_cast<P>(813))});
        knobs(p, CRect(12, row2, 400, row2 + 104), "SÉPARATIONS", {id(P::ImgSplitLow), id(P::ImgSplitMid), id(P::ImgSplitHigh)});
    }
    {
        auto* p = page(drawer, 4, "5", "MULTIBAND", "Tient la 808 dans le temps. Fréquences : glisse les traits de l'écran.");
        static const std::array<const char*, 4> kBands{"SUB", "BAS", "MÉDIUM", "AIGU"};
        const double cw = (W - 24.0 - 3 * 8.0) / 4.0;
        for (int b = 0; b < 4; ++b) {
            const double x = 12 + b * (cw + 8);
            knobs(p, CRect(x, row1, x + cw, row1 + 92), kBands[static_cast<std::size_t>(b)],
                  {id(params::mbId(b, params::MbControl::Threshold)), id(params::mbId(b, params::MbControl::Ratio))}, 0);
            knobs(p, CRect(x, row1 + 98, x + cw, row1 + 190), "TEMPS · GAIN",
                  {id(params::mbId(b, params::MbControl::Attack)), id(params::mbId(b, params::MbControl::Release)), id(params::mbId(b, params::MbControl::Makeup))}, 0);
        }
    }
    {
        auto* p = page(drawer, 5, "6", "CLIPPER", "Écrête les pics avant le limiteur ; la 808 à part.");
        knobs(p, CRect(12, row1, 330, row1 + 104), "ÉCRÊTAGE", {id(P::ClipDrive), id(P::ClipCeiling), id(P::ClipKnee)});
        knobs(p, CRect(12, row2, 330, row2 + 104), "808", {id(P::ClipSplit), id(P::ClipSubDrive), id(P::ClipSubRoom)});
        seg(p, CRect(342, row1 + 30, W - 14, row1 + 54), "COURBE", P::ClipMode);
        seg(p, CRect(342, row2 + 30, W - 14, row2 + 54), "SURÉCH.", P::ClipOversampling);
    }
    {
        auto* p = page(drawer, 6, "7", "LIMITER", "Plafond final, true peak. Punch : plus de relief.");
        knobs(p, CRect(12, row1, W - 14, row1 + 104), "VOLUME", {id(P::LimGain), id(P::LimCeiling), id(P::LimRelease), id(P::LimAutoRelease)});
        knobs(p, CRect(12, row2, W - 14, row2 + 104), "RELIEF · STÉRÉO", {id(P::LimPunch), id(P::LimUpward), id(P::LimUnlinkTransient), id(P::LimUnlinkSustain)});
    }
    {
        auto* p = page(drawer, kStemPage, "S", "PISTE", "Instance en rôle Stem : la chaîne de l'élément, sans latence.");
        seg(p, CRect(12, row1 + 16, 220, row1 + 40), "MODE", P::StemMode);
        p->addView(new Caption(CRect(232, row1 + 2, 400, row1 + 14), "ÉLÉMENT", color::kText3, mono(9)));
        auto* el = new COptionMenu(CRect(232, row1 + 16, W - 14, row1 + 40), listen([this](CControl* ctl) {
            setPlain(id(ParamId::LinkStemKind), std::lround(ctl->getValue()));
        }), 2);
        el->setFont(mono(10.5));
        el->setFontColor(color::kText);
        el->setBackColor(color::kScreen);
        el->setFrameColor(CColor(60, 62, 66));
        for (const auto& l : params::findParameter(ParamId::LinkStemKind)->choices) el->addEntry(UTF8String(std::string(l)));
        el->setCurrent(static_cast<int32_t>(std::lround(plain(id(ParamId::LinkStemKind)))));
        p->addView(el);
        syncs_.push_back([this, el] {
            const auto v = static_cast<int32_t>(std::lround(plain(id(ParamId::LinkStemKind))));
            if (el->getCurrentIndex() != v) el->setCurrent(v);
        });
        auto* heard = new Screen(CRect(12, row1 + 50, W - 14, row1 + 74), [this](CDrawContext* dc, const CRect& r) {
            engraved(dc, "RECONNU À L'ÉCOUTE", CRect(r.left + 8, r.top, r.left + 160, r.bottom));
            text(dc, valueText(id(ParamId::MeterStemElement)), CRect(r.left + 160, r.top, r.right - 8, r.bottom), color::kGood, mono(11, true));
        }, true);
        heard->setMouseEnabled(false);
        p->addView(heard);
        live_.push_back(heard);
        knobs(p, CRect(12, row2 - 18, W - 14, row2 + 86), "DOSES DU MODE MANUEL (TRIM : LES DEUX)",
              {id(P::StemHpf), id(P::StemComp), id(P::StemSat), id(P::StemClean), id(P::StemWidth), id(P::StemTrim)});
    }
    {
        auto* p = page(drawer, kConductorPage, "M", "CHEF D'ORCHESTRE", "Le master écoute les stems de son groupe et propose.");
        auto* text1 = new Screen(CRect(12, row1, W - 14, row1 + 110), [this](CDrawContext* dc, const CRect& r) {
            const auto& pending = omnis()->pendingProposals();
            std::string s;
            if (role() != static_cast<int>(params::LinkRole::Master)) s = "Règle cette instance en Master (même groupe que les stems) pour recevoir les propositions.";
            else if (pending.empty()) s = "Aucune proposition : aucun stem n'en gêne un autre pour l'instant. Lance la lecture, OMNIS écoute.";
            else {
                proposalIndex_ = std::clamp(proposalIndex_, 0, static_cast<int>(pending.size()) - 1);
                s = pending[static_cast<std::size_t>(proposalIndex_)].text;
                engraved(dc, "PROPOSITION " + std::to_string(proposalIndex_ + 1) + " / " + std::to_string(pending.size()),
                         CRect(r.left + 10, r.top + 6, r.right - 10, r.top + 18));
            }
            // Retour à la ligne à la main (le texte tient en quelques lignes).
            std::vector<std::string> lines;
            std::string line;
            for (std::size_t i = 0; i < s.size();) {
                const std::size_t sp = s.find(' ', i);
                const std::string word = s.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
                if (line.size() + word.size() > 70 && !line.empty()) {
                    lines.push_back(line);
                    line.clear();
                }
                line += (line.empty() ? "" : " ") + word;
                if (sp == std::string::npos) break;
                i = sp + 1;
            }
            if (!line.empty()) lines.push_back(line);
            for (std::size_t i = 0; i < lines.size() && i < 5; ++i)
                text(dc, lines[i], CRect(r.left + 10, r.top + 24 + 16 * i, r.right - 10, r.top + 40 + 16 * i), color::kText, mono(10.5));
        }, true);
        text1->setMouseEnabled(false);
        p->addView(text1);
        live_.push_back(text1);
        const auto act = [this](int answer) {
            const auto& pending = omnis()->pendingProposals();
            if (pending.empty()) return;
            const auto& pr = pending[static_cast<std::size_t>(std::clamp(proposalIndex_, 0, static_cast<int>(pending.size()) - 1))];
            omnis()->answerProposal(pr.key, answer);
            omnis()->requestProposals();
        };
        const auto btn = [&](const CRect& r, const char* label, std::function<void()> f) {
            auto* b = new HwButton(r, label, [] { return false; }, std::move(f), false);
            b->setAccentText(true);
            p->addView(b);
            live_.push_back(b);
        };
        btn(CRect(12, row1 + 118, 100, row1 + 144), "OUI", [act] { act(0); });
        btn(CRect(106, row1 + 118, 194, row1 + 144), "NON", [act] { act(1); });
        btn(CRect(W - 112, row1 + 118, W - 66, row1 + 144), "<", [this] { --proposalIndex_; });
        btn(CRect(W - 60, row1 + 118, W - 14, row1 + 144), ">", [this] { ++proposalIndex_; });
        auto* applied = new Screen(CRect(12, row1 + 152, W - 130, row1 + 236), [this](CDrawContext* dc, const CRect& r) {
            engraved(dc, "RETOUCHES EN PLACE", CRect(r.left + 10, r.top + 6, r.right - 10, r.top + 18));
            const auto& a = omnis()->appliedCorrections();
            if (a.empty()) text(dc, "Aucune.", CRect(r.left + 10, r.top + 22, r.right - 10, r.top + 38), color::kText3, mono(10));
            for (std::size_t i = 0; i < a.size() && i < 3; ++i)
                text(dc, "• " + a[i].description, CRect(r.left + 10, r.top + 22 + 16 * i, r.right - 10, r.top + 38 + 16 * i), color::kText2, mono(10));
        }, true);
        applied->setMouseEnabled(false);
        p->addView(applied);
        live_.push_back(applied);
        btn(CRect(W - 122, row1 + 182, W - 14, row1 + 208), "ANNULER", [this] {
            if (!omnis()->appliedCorrections().empty()) omnis()->answerProposal(omnis()->appliedCorrections().back().key, 2);
            omnis()->requestProposals();
        });
    }
    // Mesures de l'écran du Match EQ : glisser les nœuds.
    if (pageScreens_[1])
        pageScreens_[1]->setMouse([this](const CPoint& p, int phase, bool fine) {
            const CRect r = pageScreens_[1]->getViewSize();
            return matchMouse(p, phase, fine, CRect(0, 0, r.getWidth(), r.getHeight()));
        });
    // Multibande : les trois fréquences de séparation se glissent sur l'écran.
    if (pageScreens_[4])
        pageScreens_[4]->setMouse([this](const CPoint& p, int phase, bool) {
            static int held = -1;
            const CRect r = pageScreens_[4]->getViewSize();
            const CRect plot(10, 26, r.getWidth() - 10, r.getHeight() - 20);
            const std::array<ParamId, 3> splits{ParamId::MbSplitLow, ParamId::MbSplitMid, ParamId::MbSplitHigh};
            if (phase == 0) {
                held = -1;
                double best = 10.0;
                for (int i = 0; i < 3; ++i) {
                    const double x = xOfHz(plain(id(splits[static_cast<std::size_t>(i)])), 20.0, 20000.0, plot);
                    if (std::fabs(x - p.x) < best) {
                        best = std::fabs(x - p.x);
                        held = i;
                    }
                }
                if (held >= 0) omnis()->beginGesture(id(splits[static_cast<std::size_t>(held)]));
                return held >= 0;
            }
            if (held < 0) return false;
            const auto pid = id(splits[static_cast<std::size_t>(held)]);
            const auto* spec = params::findParameter(pid);
            omnis()->performGesture(pid, params::toNormalized(*spec, params::clampPlain(*spec, hzOfX(p.x, 20.0, 20000.0, plot))));
            if (phase == 2) {
                omnis()->endGesture(pid);
                held = -1;
            }
            return true;
        });
}

bool OmnisEditor::matchMouse(const CPoint& p, int phase, bool fine, const CRect& r) {
    const CRect plot(10, 24, r.getWidth() - 10, r.getHeight() - 20);
    const auto nodeId = [this](int k) { return id(matchSide_ == 0 ? params::matchMidId(k) : params::matchSideId(k)); };
    const auto yOfDb = [&](double db) { return plot.top + (12.0 - db) / 24.0 * plot.getHeight(); };
    if (phase == 0 || phase == 3) {
        matchNode_ = -1;
        double best = 12.0;
        for (int k = 0; k < params::kMatchBands; ++k) {
            const double x = xOfHz(params::matchBandHz(k), 20.0, 20480.0, plot), y = yOfDb(plain(nodeId(k)));
            const double d = std::hypot(x - p.x, y - p.y);
            if (d < best) {
                best = d;
                matchNode_ = k;
            }
        }
        if (matchNode_ < 0) return false;
        if (phase == 3) {
            setPlain(nodeId(matchNode_), 0.0);
            matchNode_ = -1;
            return true;
        }
        matchStartY_ = p.y;
        matchStartDb_ = plain(nodeId(matchNode_));
        omnis()->beginGesture(nodeId(matchNode_));
        return true;
    }
    if (matchNode_ < 0) return false;
    const auto pid = nodeId(matchNode_);
    const double db = std::clamp(matchStartDb_ + (matchStartY_ - p.y) * 24.0 / plot.getHeight() * (fine ? 0.2 : 1.0), -12.0, 12.0);
    omnis()->performGesture(pid, params::toNormalized(*params::findParameter(pid), db));
    if (phase == 2) {
        omnis()->endGesture(pid);
        matchNode_ = -1;
    }
    return true;
}

void OmnisEditor::selectPage(int p) {
    page_ = std::clamp(p, 0, 8);
    for (int i = 0; i < 9; ++i) {
        if (pages_[static_cast<std::size_t>(i)]) pages_[static_cast<std::size_t>(i)]->setVisible(i == page_);
        if (pageScreens_[static_cast<std::size_t>(i)]) pageScreens_[static_cast<std::size_t>(i)]->setVisible(i == page_);
    }
    // Delta suit le module ouvert.
    if (page_ <= 6 && plain(id(ParamId::DeltaListen)) >= 0.5 && std::lround(plain(id(ParamId::DeltaListen))) != page_ + 1)
        setPlain(id(ParamId::DeltaListen), page_ + 1);
    if (rack_) rack_->invalid();
}

// -----------------------------------------------------------------------------
//  Écrans des modules
// -----------------------------------------------------------------------------
void OmnisEditor::paintModuleScreen(int module, CDrawContext* c, const CRect& r) {
    c->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const CRect plot(r.left + 10, r.top + 24, r.right - 10, r.bottom - 20);
    const auto caption = [&](const std::string& s) { text(c, s, CRect(r.left + 12, r.top + 5, r.right - 12, r.top + 20), color::kText2, mono(9.5)); };
    const auto hint = [&](const std::string& s) { text(c, s, CRect(r.left + 12, r.bottom - 16, r.right - 12, r.bottom - 3), color::kText3, mono(8.5), kRightText); };
    const auto curve = [&](double lo, double hi, double dbRange, const std::function<double(double)>& dbAt, const CColor& col, bool fill) {
        auto path = VSTGUI::owned(c->createGraphicsPath());
        if (!path) return;
        const double mid = plot.top + plot.getHeight() * 0.5;
        for (int i = 0; i <= 160; ++i) {
            const double x = plot.left + plot.getWidth() * i / 160.0;
            const double y = std::clamp(mid - dbAt(hzOfX(x, lo, hi, plot)) / dbRange * plot.getHeight() * 0.5, plot.top, plot.bottom);
            if (i == 0) path->beginSubpath(CPoint(x, y));
            else path->addLine(CPoint(x, y));
        }
        if (fill) {
            auto area = VSTGUI::owned(c->createGraphicsPath());
            area->addPath(*path);
            area->addLine(CPoint(plot.right, mid));
            area->addLine(CPoint(plot.left, mid));
            area->closeSubpath();
            c->setFillColor(withAlpha(col, 0.12));
            c->drawGraphicsPath(area, CDrawContext::kPathFilled);
        }
        c->setLineWidth(1.6);
        c->setFrameColor(col);
        c->drawGraphicsPath(path, CDrawContext::kPathStroked);
        c->setLineWidth(1.0);
    };
    const auto zeroLine = [&] {
        c->setFrameColor(CColor(255, 255, 255, 30));
        const double y = std::round(plot.top + plot.getHeight() * 0.5) + 0.5;
        c->drawLine(CPoint(plot.left, y), CPoint(plot.right, y));
    };
    const auto transfer = [&](const std::function<double(double)>& f, double range) {
        // Courbe de transfert : entrée (−range … +range) -> sortie, diagonale en repère.
        const CRect sq(plot.left + (plot.getWidth() - plot.getHeight()) / 2, plot.top, plot.left + (plot.getWidth() + plot.getHeight()) / 2, plot.bottom);
        c->setFrameColor(color::kGrid);
        c->drawLine(CPoint(sq.left, sq.bottom), CPoint(sq.right, sq.top));
        c->drawLine(CPoint(sq.left, (sq.top + sq.bottom) / 2), CPoint(sq.right, (sq.top + sq.bottom) / 2));
        c->drawLine(CPoint((sq.left + sq.right) / 2, sq.top), CPoint((sq.left + sq.right) / 2, sq.bottom));
        auto path = VSTGUI::owned(c->createGraphicsPath());
        for (int i = 0; i <= 200; ++i) {
            const double x = -range + 2.0 * range * i / 200.0;
            const double y = std::clamp(f(x), -range, range);
            const CPoint pt(sq.left + (x + range) / (2 * range) * sq.getWidth(), sq.bottom - (y + range) / (2 * range) * sq.getHeight());
            if (i == 0) path->beginSubpath(pt);
            else path->addLine(pt);
        }
        c->setLineWidth(1.8);
        c->setFrameColor(color::kAccent);
        c->drawGraphicsPath(path, CDrawContext::kPathStroked);
        c->setLineWidth(1.0);
    };
    const auto history = [&](const std::array<float, Analysis::kHistory>& h, double rangeDb, const CColor& col) {
        auto path = VSTGUI::owned(c->createGraphicsPath());
        for (int i = 0; i < Analysis::kHistory; ++i) {
            const float v = h[static_cast<std::size_t>((an_->historyPos + i) % Analysis::kHistory)];
            const CPoint pt(plot.left + plot.getWidth() * i / (Analysis::kHistory - 1), plot.top + std::clamp(-v / rangeDb, 0.0, 1.0) * plot.getHeight());
            if (i == 0) path->beginSubpath(pt);
            else path->addLine(pt);
        }
        auto area = VSTGUI::owned(c->createGraphicsPath());
        area->addPath(*path);
        area->addLine(CPoint(plot.right, plot.top));
        area->addLine(CPoint(plot.left, plot.top));
        area->closeSubpath();
        c->setFillColor(withAlpha(col, 0.18));
        c->drawGraphicsPath(area, CDrawContext::kPathFilled);
        c->setLineWidth(1.5);
        c->setFrameColor(col);
        c->drawGraphicsPath(path, CDrawContext::kPathStroked);
        c->setLineWidth(1.0);
        for (int db = 3; db < rangeDb; db += 3) {
            const double y = plot.top + db / rangeDb * plot.getHeight();
            c->setFrameColor(color::kGrid);
            c->drawLine(CPoint(plot.left, y), CPoint(plot.right, y));
            text(c, fmt("-%.0f", db), CRect(plot.left + 2, y - 11, plot.left + 30, y - 1), color::kText3, mono(8));
        }
    };

    switch (module) {
        case 0: { // Sub : coupe-bas, sub, zone mono
            caption("RÉPONSE DU BAS · mono sous " + fmt("%.0f Hz", plain(id(ParamId::MonoFreq))));
            const double lo = 10.0, hi = 1000.0;
            gridLines(c, plot, lo, hi, true);
            const double monoX = xOfHz(plain(id(ParamId::MonoFreq)), lo, hi, plot);
            c->setFillColor(withAlpha(color::kCool, 0.08 + 0.12 * plain(id(ParamId::MonoAmount)) / 100.0));
            c->drawRect(CRect(plot.left, plot.top, monoX, plot.bottom), kDrawFilled);
            engraved(c, "MONO", CRect(plot.left + 4, plot.top + 2, monoX - 2, plot.top + 14), kLeftText, color::kCool);
            zeroLine();
            const double fc = plain(id(ParamId::LowCutFreq)), sf = plain(id(ParamId::SubFreq)), sg = plain(id(ParamId::SubGain));
            curve(lo, hi, 18.0, [&](double f) {
                const double w = f / fc, hp = 10.0 * std::log10(std::pow(w, 4) / (1.0 + std::pow(w, 4)));
                const double bell = sg / (1.0 + std::pow(std::log2(f / sf) / 0.9, 2.0));
                return hp + bell;
            }, color::kAccent, true);
            hint("COUPE-BAS " + fmt("%.0f Hz", fc) + " · SUB " + fmt("%+.1f dB", sg) + " À " + fmt("%.0f Hz", sf));
            break;
        }
        case 1: { // Match EQ : nœuds éditables
            caption(std::string("COURBE M/S · ÉDITION ") + (matchSide_ == 0 ? "MID" : "SIDE") + " · TILT " + fmt("%+.1f dB", plain(id(ParamId::MatchTilt))));
            const double lo = 20.0, hi = 20480.0;
            gridLines(c, plot, lo, hi, true);
            // Le spectre de sortie en fond.
            {
                auto path = VSTGUI::owned(c->createGraphicsPath());
                for (int b = 0; b < Analysis::kBands; ++b) {
                    const double f = 20.0 * std::pow(1000.0, (b + 0.5) / Analysis::kBands);
                    const double y = std::clamp(plot.top + (-12.0 - an_->spectrum[static_cast<std::size_t>(b)]) / 60.0 * plot.getHeight(), plot.top, plot.bottom);
                    const CPoint pt(xOfHz(f, lo, hi, plot), y);
                    if (b == 0) path->beginSubpath(pt);
                    else path->addLine(pt);
                }
                c->setFrameColor(CColor(255, 255, 255, 40));
                c->drawGraphicsPath(path, CDrawContext::kPathStroked);
            }
            zeroLine();
            for (int db : {-6, 6}) {
                const double y = plot.top + (12.0 - db) / 24.0 * plot.getHeight();
                c->setFrameColor(color::kGrid);
                c->drawLine(CPoint(plot.left, y), CPoint(plot.right, y));
                text(c, fmt("%+.0f", db), CRect(plot.left + 2, y - 11, plot.left + 30, y - 1), color::kText3, mono(8));
            }
            const double amount = plain(id(ParamId::MatchAmount)) / 100.0, tilt = plain(id(ParamId::MatchTilt));
            for (int layer = 1; layer >= 0; --layer) {
                const bool edited = layer == matchSide_;
                const auto nodeDb = [&](int k) { return plain(id(layer == 0 ? params::matchMidId(k) : params::matchSideId(k))); };
                const auto at = [&](double f) {
                    const double pos = std::clamp(2.0 * std::log2(f / 20.0), 0.0, params::kMatchBands - 1.0);
                    const int k = std::min(static_cast<int>(pos), params::kMatchBands - 2);
                    const double t = pos - k;
                    const double node = nodeDb(k) * (1 - t) + nodeDb(k + 1) * t;
                    const double tl = tilt * std::clamp(std::log2(f / 640.0) / std::log2(20000.0 / 640.0), -1.0, 1.0);
                    return amount * (node + tl);
                };
                curve(lo, hi, 12.0, at, edited ? color::kAccent : withAlpha(color::kCool, 0.55), edited);
                if (!edited) continue;
                for (int k = 0; k < params::kMatchBands; ++k) {
                    const double x = xOfHz(params::matchBandHz(k), lo, hi, plot), y = plot.top + (12.0 - nodeDb(k)) / 24.0 * plot.getHeight();
                    const bool held = k == matchNode_;
                    fillCircle(c, CPoint(x, y), held ? 4.5 : 3.0, held || std::fabs(nodeDb(k)) > 0.05 ? color::kAccent : CColor(120, 120, 124));
                }
            }
            hint(matchNode_ >= 0 ? hzText(params::matchBandHz(matchNode_)) + " Hz : " + valueText(id(matchSide_ == 0 ? params::matchMidId(matchNode_) : params::matchSideId(matchNode_)))
                                 : "AMBRE : ÉDITÉ · BLEU : L'AUTRE CANAL");
            break;
        }
        case 2: { // Inflator : courbe de transfert
            caption("COURBE · 808 " + fmt("%.0f %%", plain(id(ParamId::InflLowEffect))) + " · MIX " + fmt("%.0f %%", plain(id(ParamId::InflHighEffect))));
            const double effect = std::max(plain(id(ParamId::InflLowEffect)), plain(id(ParamId::InflHighEffect))) / 100.0;
            const double drive = std::pow(10.0, plain(id(ParamId::InflDrive)) / 20.0), m = InflatorCurve::exponentFor(plain(id(ParamId::InflCurve)));
            const double out = std::pow(10.0, plain(id(ParamId::InflOutput)) / 20.0);
            transfer([&](double x) { return out * (x + effect * (InflatorCurve::shape(drive * x, m) / drive - x)); }, 1.2);
            hint("DRIVE " + valueText(id(ParamId::InflDrive)) + " · COURBE " + valueText(id(ParamId::InflCurve)));
            break;
        }
        case 3: { // Imager : largeur par bande
            caption("LARGEUR PAR BANDE · CORRÉLATION " + valueText(id(ParamId::MeterCorrelation)));
            const double lo = 20.0, hi = 20000.0;
            gridLines(c, plot, lo, hi, true);
            const std::array<double, 5> edges{lo, plain(id(ParamId::ImgSplitLow)), plain(id(ParamId::ImgSplitMid)), plain(id(ParamId::ImgSplitHigh)), hi};
            for (int b = 0; b < 4; ++b) {
                const double x0 = xOfHz(edges[static_cast<std::size_t>(b)], lo, hi, plot), x1 = xOfHz(edges[static_cast<std::size_t>(b) + 1], lo, hi, plot);
                const double w = plain(id(static_cast<ParamId>(810 + b)));
                const double y100 = plot.bottom - plot.getHeight() * 0.5, y = plot.bottom - plot.getHeight() * std::clamp(w / 200.0, 0.0, 1.0);
                c->setFillColor(withAlpha(w > 100.5 ? color::kAccent : (w < 99.5 ? color::kCool : color::kText3), 0.22));
                c->drawRect(CRect(x0 + 2, std::min(y, y100), x1 - 2, std::max(y, y100)), kDrawFilled);
                c->setFrameColor(w > 100.5 ? color::kAccent : (w < 99.5 ? color::kCool : color::kText2));
                c->drawLine(CPoint(x0 + 2, y), CPoint(x1 - 2, y));
                text(c, fmt("%.0f%%", w), CRect(x0, plot.top + 2, x1, plot.top + 14), color::kText2, mono(9), kCenterText);
                if (b > 0) {
                    c->setFrameColor(CColor(255, 255, 255, 60));
                    c->drawLine(CPoint(x0, plot.top), CPoint(x0, plot.bottom));
                }
            }
            c->setFrameColor(CColor(255, 255, 255, 30));
            c->drawLine(CPoint(plot.left, plot.bottom - plot.getHeight() * 0.5), CPoint(plot.right, plot.bottom - plot.getHeight() * 0.5));
            hint("100 % = INCHANGÉ · 0 % = MONO");
            break;
        }
        case 4: { // Multibande : réduction par bande, séparations à glisser
            caption("RÉDUCTION PAR BANDE (dB)");
            const double lo = 20.0, hi = 20000.0;
            gridLines(c, plot, lo, hi, true);
            const std::array<double, 5> edges{lo, plain(id(ParamId::MbSplitLow)), plain(id(ParamId::MbSplitMid)), plain(id(ParamId::MbSplitHigh)), hi};
            for (int b = 0; b < 4; ++b) {
                const double x0 = xOfHz(edges[static_cast<std::size_t>(b)], lo, hi, plot), x1 = xOfHz(edges[static_cast<std::size_t>(b) + 1], lo, hi, plot);
                const double gr = -an_->meters.multibandGrDb[b];
                const double h = std::clamp(gr / 12.0, 0.0, 1.0) * (plot.getHeight() - 16);
                c->setFillColor(withAlpha(color::kAccent, 0.5));
                c->drawRect(CRect(x0 + 6, plot.top + 16, x1 - 6, plot.top + 16 + h), kDrawFilled);
                text(c, fmt("%.1f", -gr), CRect(x0, plot.top + 2, x1, plot.top + 14), color::kText2, mono(9), kCenterText);
                if (b > 0) {
                    c->setLineWidth(2.0);
                    c->setFrameColor(color::kAccent35);
                    c->drawLine(CPoint(x0, plot.top), CPoint(x0, plot.bottom));
                    c->setLineWidth(1.0);
                    text(c, hzText(edges[static_cast<std::size_t>(b)]), CRect(x0 - 24, plot.bottom - 28, x0 + 24, plot.bottom - 16), color::kAccent, mono(8.5), kCenterText);
                }
            }
            hint("GLISSE LES TRAITS : FRÉQUENCES DE SÉPARATION");
            break;
        }
        case 5: { // Clipper : courbe + réduction dans le temps
            caption("COURBE · " + valueText(id(ParamId::ClipMode)) + " · RÉDUCTION " + fmt("%.1f dB", an_->meters.clipperGrDb));
            const double drive = std::pow(10.0, plain(id(ParamId::ClipDrive)) / 20.0), ceil = std::pow(10.0, plain(id(ParamId::ClipCeiling)) / 20.0);
            const int mode = static_cast<int>(std::lround(plain(id(ParamId::ClipMode))));
            const double knee = plain(id(ParamId::ClipKnee)) / 100.0;
            transfer([&](double x) {
                const double u = drive * x / ceil;
                const double y = mode == 0 ? dsp::shapes::hard(u) : (mode == 1 ? dsp::shapes::softKnee(u, knee) : dsp::shapes::diode(u));
                return y * ceil;
            }, 1.2);
            break;
        }
        case 6: { // Limiteur : réduction de gain dans le temps
            caption("RÉDUCTION DE GAIN · " + fmt("%.1f dB", an_->meters.limiterGrDb) + " · PLAFOND " + valueText(id(ParamId::LimCeiling)));
            history(an_->limHistory, 12.0, color::kAccent);
            hint("PUNCH " + valueText(id(ParamId::LimPunch)) + " · UPWARD " + valueText(id(ParamId::LimUpward)));
            break;
        }
        case kStemPage: {
            caption("CHAÎNE DE LA PISTE");
            const std::array<const char*, 6> stages{"TRIM", "PASSE-HAUT", "EQ DYN", "COMP", "SAT", "M/S"};
            const double w = (plot.getWidth() - 5 * 8) / 6.0;
            for (int i = 0; i < 6; ++i) {
                const CRect b(plot.left + i * (w + 8), plot.top + 30, plot.left + i * (w + 8) + w, plot.top + 70);
                inset(c, b, CColor(16, 17, 19));
                text(c, stages[static_cast<std::size_t>(i)], b, color::kText2, mono(9), kCenterText);
                if (i < 5) {
                    c->setFrameColor(color::kAccent35);
                    c->drawLine(CPoint(b.right + 1, b.top + 20), CPoint(b.right + 7, b.top + 20));
                }
            }
            text(c, "Élément reconnu : " + valueText(id(ParamId::MeterStemElement)), CRect(plot.left, plot.top + 86, plot.right, plot.top + 102), color::kGood, mono(10));
            text(c, "Mode : " + valueText(id(ParamId::StemMode)) + " · aucune latence ajoutée", CRect(plot.left, plot.top + 104, plot.right, plot.top + 120), color::kText2, mono(10));
            // Bus de drums : chaque élément nommé, avec ce qu'OMNIS en fait.
            const auto& drums = omnis()->drumElements();
            if (!drums.empty()) {
                engraved(c, "DANS LE BUS", CRect(plot.left, plot.top + 128, plot.right, plot.top + 140));
                for (std::size_t i = 0; i < drums.size() && i < 4; ++i)
                    text(c, "• " + drums[i], CRect(plot.left, plot.top + 142 + 16 * i, plot.right, plot.top + 158 + 16 * i), color::kText, mono(9.5));
            }
            hint("LE MASTER DU MÊME GROUPE ENTEND CETTE PISTE");
            break;
        }
        case kConductorPage: {
            caption("STEMS LIÉS : " + valueText(id(ParamId::MeterLinkStems)) + " · PROPOSITIONS : " + valueText(id(ParamId::MeterProposals)));
            text(c, "Chaque proposition attend ton accord :", CRect(plot.left, plot.top + 10, plot.right, plot.top + 26), color::kText2, mono(10));
            text(c, "rien n'est appliqué sans un OUI.", CRect(plot.left, plot.top + 28, plot.right, plot.top + 44), color::kText2, mono(10));
            text(c, "Refusée, elle n'est plus reproposée ;", CRect(plot.left, plot.top + 56, plot.right, plot.top + 72), color::kText3, mono(10));
            text(c, "acceptée, ANNULER la retire.", CRect(plot.left, plot.top + 74, plot.right, plot.top + 90), color::kText3, mono(10));
            break;
        }
        default: break;
    }
}

// -----------------------------------------------------------------------------
//  Écrans du bas : analyseur et goniomètre
// -----------------------------------------------------------------------------
void OmnisEditor::buildScreens(CViewContainer* root) {
    auto* panel = new Panel(CRect(kWorkL, kScreensT, kWorkR, kScreensB));
    root->addView(panel);
    const double split = (kWorkR - kWorkL) * 2.0 / 3.0;
    spectrum_ = new Screen(CRect(10, 10, split - 6, panel->getViewSize().getHeight() - 10), [this](CDrawContext* c, const CRect& r) { paintSpectrum(c, r); }, true);
    spectrum_->setMouseEnabled(false);
    panel->addView(spectrum_);
    live_.push_back(spectrum_);
    scope_ = new Screen(CRect(split + 6, 10, panel->getViewSize().getWidth() - 10, panel->getViewSize().getHeight() - 10),
                        [this](CDrawContext* c, const CRect& r) { paintScope(c, r); }, true);
    scope_->setMouseEnabled(false);
    panel->addView(scope_);
    live_.push_back(scope_);
}

void OmnisEditor::paintSpectrum(CDrawContext* c, const CRect& r) {
    c->setDrawMode(kAntiAliasing | kNonIntegralMode);
    engraved(c, "ANALYSEUR · SORTIE", CRect(r.left + 10, r.top + 4, r.left + 200, r.top + 16));
    const CRect plot(r.left + 8, r.top + 18, r.right - 8, r.bottom - 14);
    gridLines(c, plot, 20.0, 20000.0, true);
    auto line = VSTGUI::owned(c->createGraphicsPath());
    auto area = VSTGUI::owned(c->createGraphicsPath());
    for (int b = 0; b < Analysis::kBands; ++b) {
        const double f = 20.0 * std::pow(1000.0, (b + 0.5) / Analysis::kBands);
        const double y = std::clamp(plot.top + (-6.0 - an_->spectrum[static_cast<std::size_t>(b)]) / 66.0 * plot.getHeight(), plot.top, plot.bottom);
        const CPoint pt(xOfHz(f, 20.0, 20000.0, plot), y);
        if (b == 0) {
            line->beginSubpath(pt);
            area->beginSubpath(CPoint(pt.x, plot.bottom));
        }
        line->addLine(pt);
        area->addLine(pt);
    }
    area->addLine(CPoint(plot.right, plot.bottom));
    area->closeSubpath();
    c->setFillColor(withAlpha(color::kAccent, 0.10));
    c->drawGraphicsPath(area, CDrawContext::kPathFilled);
    c->setLineWidth(1.4);
    c->setFrameColor(color::kAccent);
    c->drawGraphicsPath(line, CDrawContext::kPathStroked);
    c->setLineWidth(1.0);
}

void OmnisEditor::paintScope(CDrawContext* c, const CRect& r) {
    engraved(c, "GONIOMÈTRE", CRect(r.left + 10, r.top + 4, r.left + 120, r.top + 16));
    text(c, "CORR " + valueText(id(ParamId::MeterCorrelation)), CRect(r.right - 110, r.top + 4, r.right - 8, r.top + 16), color::kText2, mono(9), kRightText);
    const CPoint center((r.left + r.right) / 2, (r.top + r.bottom) / 2 + 6);
    const double radius = std::min(r.getWidth(), r.getHeight()) / 2 - 16;
    c->setFrameColor(color::kGrid);
    c->drawLine(CPoint(center.x - radius, center.y), CPoint(center.x + radius, center.y));
    c->drawLine(CPoint(center.x, center.y - radius), CPoint(center.x, center.y + radius));
    c->drawLine(CPoint(center.x - radius * 0.7, center.y - radius * 0.7), CPoint(center.x + radius * 0.7, center.y + radius * 0.7));
    c->drawLine(CPoint(center.x - radius * 0.7, center.y + radius * 0.7), CPoint(center.x + radius * 0.7, center.y - radius * 0.7));
    // Échelle automatique douce : les points occupent le cadre.
    double peak = 1e-4;
    for (int i = 0; i < 512; ++i) peak = std::max({peak, static_cast<double>(std::fabs(an_->sample(i, 0))), static_cast<double>(std::fabs(an_->sample(i, 1)))});
    const double scale = radius / std::max(peak * 1.2, 0.05);
    c->setFillColor(withAlpha(color::kAccent, 0.55));
    for (int i = 0; i < 512; i += 1) {
        const double l = an_->sample(i, 0), rr = an_->sample(i, 1);
        const double x = (rr - l) * 0.7071 * scale, y = (l + rr) * 0.7071 * scale;
        const double px = center.x + std::clamp(x, -radius, radius), py = center.y - std::clamp(y, -radius, radius);
        c->drawRect(CRect(px - 0.7, py - 0.7, px + 0.7, py + 0.7), kDrawFilled);
    }
}

// -----------------------------------------------------------------------------
//  Colonne Master
// -----------------------------------------------------------------------------
void OmnisEditor::buildMaster(CViewContainer* root) {
    auto* col = new Panel(CRect(kMasterL, kRibbon1, kMasterR, kScreensB));
    root->addView(col);
    const double W = col->getViewSize().getWidth();
    col->addView(new Caption(CRect(12, 8, 120, 22), "MASTER", color::kText2, mono(10, true)));
    const double kh = LedKnob::heightFor(LedKnob::Size::Mid);
    auto* in = new LedKnob(CRect(12, 28, W / 2 - 2, 28 + kh), "ENTRÉE", model(id(ParamId::InputGain)), LedKnob::Size::Mid);
    auto* out = new LedKnob(CRect(W / 2 + 2, 28, W - 12, 28 + kh), "SORTIE", model(id(ParamId::OutputGain)), LedKnob::Size::Mid);
    col->addView(in);
    col->addView(out);
    live_.push_back(in);
    live_.push_back(out);
    meter_ = new Screen(CRect(12, 128, W - 12, 368), [this](CDrawContext* c, const CRect& r) { paintMeter(c, r); }, true);
    meter_->setMouseEnabled(false);
    col->addView(meter_);
    live_.push_back(meter_);
    readouts_ = new Screen(CRect(12, 376, W - 12, 540), [this](CDrawContext* c, const CRect& r) { paintReadouts(c, r); }, true);
    readouts_->setMouseEnabled(false);
    col->addView(readouts_);
    live_.push_back(readouts_);

    // Instances liées.
    col->addView(new Caption(CRect(12, 550, W - 12, 562), "INSTANCES LIÉES", color::kText3, mono(9)));
    const auto menu = [&](const CRect& r, ParamId pid, const char* name) {
        col->addView(new Caption(CRect(r.left, r.top + 4, r.left + 60, r.bottom), name, color::kText3, mono(9)));
        auto* m = new COptionMenu(CRect(r.left + 62, r.top, r.right, r.bottom), listen([this, pid](CControl* ctl) {
            const auto* spec = params::findParameter(pid);
            setPlain(id(pid), spec->minValue + std::lround(ctl->getValue()));
            layoutRack();
            if (pid == ParamId::LinkRole) selectPage(role() == static_cast<int>(params::LinkRole::Stem) ? kStemPage : (role() == static_cast<int>(params::LinkRole::Master) ? kConductorPage : 6));
        }), static_cast<int32_t>(pid));
        m->setFont(mono(10.5));
        m->setFontColor(color::kText);
        m->setBackColor(color::kScreen);
        m->setFrameColor(CColor(60, 62, 66));
        const auto* spec = params::findParameter(pid);
        for (const auto& l : spec->choices) m->addEntry(UTF8String(std::string(l)));
        m->setCurrent(static_cast<int32_t>(std::lround(plain(id(pid)) - spec->minValue)));
        col->addView(m);
        syncs_.push_back([this, m, pid, spec] {
            const auto v = static_cast<int32_t>(std::lround(plain(id(pid)) - spec->minValue));
            if (m->getCurrentIndex() != v) m->setCurrent(v);
        });
    };
    menu(CRect(12, 568, W - 12, 590), ParamId::LinkRole, "RÔLE");
    menu(CRect(12, 596, W - 12, 618), ParamId::LinkGroup, "GROUPE");
    auto* links = new Screen(CRect(12, 626, W - 12, 650), [this](CDrawContext* c, const CRect& r) {
        const int rl = role();
        const std::string s = rl == static_cast<int>(params::LinkRole::Master) ? "Stems liés : " + valueText(id(ParamId::MeterLinkStems))
                              : rl == static_cast<int>(params::LinkRole::Stem) ? "Entendu : " + valueText(id(ParamId::MeterStemElement))
                                                                                 : "Seul : la chaîne du master";
        text(c, s, CRect(r.left + 8, r.top, r.right - 8, r.bottom), color::kText2, mono(10));
    }, true);
    links->setMouseEnabled(false);
    col->addView(links);
    live_.push_back(links);
    auto* chef = new HwButton(CRect(12, 658, W - 12, 684), "CHEF D'ORCHESTRE", [this] { return page_ == kConductorPage; }, [this] { selectPage(kConductorPage); });
    auto* piste = new HwButton(CRect(12, 690, W - 12, 716), "PISTE (STEM)", [this] { return page_ == kStemPage; }, [this] { selectPage(kStemPage); });
    col->addView(chef);
    col->addView(piste);
    live_.push_back(chef);
    live_.push_back(piste);
}

void OmnisEditor::paintMeter(CDrawContext* c, const CRect& r) {
    c->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const auto& m = an_->meters;
    const CRect plot(r.left + 30, r.top + 22, r.right - 10, r.bottom - 12);
    const double lo = -48.0;
    const auto yOf = [&](double db) { return plot.bottom - std::clamp((db - lo) / -lo, 0.0, 1.0) * plot.getHeight(); };
    engraved(c, "SORTIE", CRect(r.left + 8, r.top + 5, r.left + 100, r.top + 17));
    for (int db = 0; db >= -48; db -= 6) {
        const double y = std::round(yOf(db)) + 0.5;
        c->setFrameColor(color::kGrid);
        c->drawLine(CPoint(plot.left, y), CPoint(plot.right, y));
        text(c, fmt("%.0f", db), CRect(r.left + 2, y - 6, plot.left - 4, y + 6), color::kText3, mono(8), kRightText);
    }
    // Bargraphes : crête L, R, sonie court terme, réduction du limiteur.
    const double bw = (plot.getWidth() - 3 * 10) / 4.0;
    const auto bar = [&](int i, double db, double hold, const CColor& col, const char* name) {
        const double x = plot.left + i * (bw + 10);
        const CRect frameRect(x, plot.top, x + bw, plot.bottom);
        c->setFillColor(CColor(14, 14, 15));
        c->drawRect(frameRect, kDrawFilled);
        const int cells = 32;
        const double ch = plot.getHeight() / cells;
        for (int k = 0; k < cells; ++k) {
            const double cellDb = lo + (k + 0.5) * (-lo) / cells;
            const CRect cell(x + 1, plot.bottom - (k + 1) * ch + 1, x + bw - 1, plot.bottom - k * ch - 0.5);
            const bool lit = cellDb <= db;
            const CColor on = cellDb > -1.0 ? color::kClip : (cellDb > -6.0 ? color::kAccentHot : col);
            c->setFillColor(lit ? on : CColor(255, 255, 255, 10));
            c->drawRect(cell, kDrawFilled);
        }
        if (hold > lo) {
            c->setFillColor(hold > -0.1 ? color::kClip : color::kText);
            c->drawRect(CRect(x, yOf(hold) - 1, x + bw, yOf(hold) + 1), kDrawFilled);
        }
        text(c, name, CRect(x - 4, plot.top - 13, x + bw + 4, plot.top - 2), color::kText3, mono(8), kCenterText);
    };
    const double st = plain(id(ParamId::MeterShortTerm));
    bar(0, m.outPeakDb[0], an_->peakHold[0], color::kAccent, "L");
    bar(1, m.outPeakDb[1], an_->peakHold[1], color::kAccent, "R");
    bar(2, st, -200.0, color::kCool, "LUFS");
    // Réduction : descend du haut.
    {
        const double x = plot.left + 3 * (bw + 10);
        c->setFillColor(CColor(14, 14, 15));
        c->drawRect(CRect(x, plot.top, x + bw, plot.bottom), kDrawFilled);
        const double gr = std::min(0.0, static_cast<double>(m.limiterGrDb) + m.clipperGrDb);
        const double h = std::clamp(-gr / 12.0, 0.0, 1.0) * plot.getHeight();
        c->setFillColor(color::kAccent);
        c->drawRect(CRect(x + 1, plot.top, x + bw - 1, plot.top + h), kDrawFilled);
        text(c, "GR", CRect(x - 4, plot.top - 13, x + bw + 4, plot.top - 2), color::kText3, mono(8), kCenterText);
    }
}

void OmnisEditor::paintReadouts(CDrawContext* c, const CRect& r) {
    const auto& m = an_->meters;
    const double tp = plain(id(ParamId::MeterTruePeak)), st = plain(id(ParamId::MeterShortTerm)), il = plain(id(ParamId::MeterIntegrated));
    const double corr = plain(id(ParamId::MeterCorrelation)), lra = plain(id(ParamId::MeterLoudnessRange));
    const bool hasSignal = il > -70.0;
    // Verdicts en mots (nos seuils, d'après nos mesures de masters rap / trap).
    const auto loudWord = [](double l) -> std::pair<std::string, CColor> {
        if (l <= -70.0) return {"SILENCE", color::kText3};
        if (l > -5.0) return {"TRÈS FORT", color::kClip};
        if (l > -8.0) return {"FORT", color::kAccent};
        if (l > -12.0) return {"BON", color::kGood};
        if (l > -16.0) return {"DOUX", color::kCool};
        return {"CALME", color::kText2};
    };
    const double plr = hasSignal ? tp - il : 0.0;
    struct Cell {
        std::string name, value, word;
        CColor wordColor;
    };
    const auto lw = loudWord(st), li = loudWord(il);
    const std::array<Cell, 8> cells{{
        {"LUFS-S", st <= -79.0 ? "-inf" : fmt("%.1f", st), lw.first, lw.second},
        {"LUFS-I", il <= -79.0 ? "-inf" : fmt("%.1f", il), li.first, li.second},
        {"TRUE PEAK", tp <= -79.0 ? "-inf" : fmt("%.1f", tp), tp > 0.0 ? "SATURE" : (tp > -1.0 ? "LIMITE" : "OK"), tp > 0.0 ? color::kClip : (tp > -1.0 ? color::kAccent : color::kGood)},
        {"CRÊTE MAX", m.samplePeakMaxDb <= -119.0 ? "-inf" : fmt("%.1f", m.samplePeakMaxDb), "", color::kText2},
        {"PLR", hasSignal ? fmt("%.1f", plr) : "—", !hasSignal ? "" : (plr >= 11.0 ? "AÉRÉ" : (plr >= 8.0 ? "PUNCHY" : (plr >= 6.0 ? "DENSE" : "ÉCRASÉ"))),
         plr >= 8.0 ? color::kGood : (plr >= 6.0 ? color::kAccent : color::kClip)},
        {"LRA", fmt("%.1f", lra), "", color::kText2},
        {"CORR", fmt("%.2f", corr), !hasSignal ? "" : (corr < -0.15 ? "PHASE !" : (corr > 0.97 ? "MONO" : "OK")), corr < -0.15 ? color::kClip : (corr > 0.97 ? color::kCool : color::kGood)},
        {"GR", fmt("%.1f", std::min(0.0, static_cast<double>(m.limiterGrDb) + m.clipperGrDb)), "", color::kText2},
    }};
    const double cw = r.getWidth() / 2.0, chh = r.getHeight() / 4.0;
    for (int i = 0; i < 8; ++i) {
        const CRect cell(r.left + (i % 2) * cw, r.top + (i / 2) * chh, r.left + (i % 2 + 1) * cw, r.top + (i / 2 + 1) * chh);
        c->setFrameColor(CColor(0, 0, 0, 255));
        c->drawRect(cell, kDrawStroked);
        const Cell& k = cells[static_cast<std::size_t>(i)];
        engraved(c, k.name, CRect(cell.left + 8, cell.top + 5, cell.right - 4, cell.top + 16));
        text(c, k.value, CRect(cell.left + 8, cell.top + 17, cell.right - 4, cell.bottom - 4), i == 0 ? color::kAccent : color::kText, mono(14));
        if (!k.word.empty()) text(c, k.word, CRect(cell.left + 4, cell.top + 5, cell.right - 7, cell.top + 16), k.wordColor, mono(8.5, true), kRightText);
    }
}

// -----------------------------------------------------------------------------
//  Minuterie
// -----------------------------------------------------------------------------
void OmnisEditor::pumpFeed() {
    if (!feed_) {
        feed_ = omnis()->feed();
        if (feed_) feed_->wanted.store(true);
    }
    if (!feed_) return;
    an_->sampleRate = feed_->sampleRate.load();
    const std::size_t got = feed_->scope.pop(an_->pop.data(), an_->pop.size());
    if (got >= 2) an_->push(an_->pop.data(), got / 2);
    if (feed_->meters.update()) an_->meters = feed_->meters.readBuffer();
    an_->analyse();
    for (int ch = 0; ch < 2; ++ch) {
        auto& hold = an_->peakHold[static_cast<std::size_t>(ch)];
        auto& t = an_->holdTicks[static_cast<std::size_t>(ch)];
        if (an_->meters.outPeakDb[ch] >= hold) {
            hold = an_->meters.outPeakDb[ch];
            t = 45;
        } else if (--t <= 0) {
            hold = std::max(an_->meters.outPeakDb[ch], hold - 0.5f);
        }
    }
    an_->limHistory[static_cast<std::size_t>(an_->historyPos)] = an_->meters.limiterGrDb;
    an_->clipHistory[static_cast<std::size_t>(an_->historyPos)] = an_->meters.clipperGrDb;
    an_->historyPos = (an_->historyPos + 1) % Analysis::kHistory;
}

void OmnisEditor::tick() {
    pumpFeed();
    const std::string news = omnis()->pollAutomation();
    if (!news.empty()) answer_ = news;
    if (const double pr = omnis()->autoProgress(); pr >= 0.0)
        answer_ = pr <= 0.0 ? std::string("AUTO : joue le passage le plus fort (refrain, drop)…") : "AUTO : écoute " + fmt("%.0f %%", 100.0 * pr) + "…";
    if (++ticks_ % 15 == 0 && (role() != static_cast<int>(params::LinkRole::Alone) || page_ == kConductorPage)) omnis()->requestProposals();
    const int o = static_cast<int>(std::lround(plain(id(ParamId::ChainOrder)))), rl = role();
    if ((o != lastOrder_ || rl != lastRole_) && dragFrom_ < 0) {
        lastOrder_ = o;
        lastRole_ = rl;
        layoutRack();
    }
    for (auto& sync : syncs_) sync();
    for (auto* l : live_) l->tick();
}

} // namespace omnis::vst3
