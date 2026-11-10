// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisEditor.cpp
// =============================================================================
#include "OmnisEditor.h"

#include "OmnisController.h"
#include "UiFeed.h"
#include "gui/Look.h"
#include "ai/ReferenceLibrary.h"
#include "ai/TextMatch.h"
#include "core/I18n.h"
#include "gfx/AvatarCache.h"
#include "gui/OpenUrl.h"
#include "gui/Widgets.h"

#include "dsp/Fft.h"
#include "dsp/Waveshaper.h"
#include "core/UserDataDir.h"
#include "params/Parameters.h"
#include "stages/InflatorStage.h"

#include "public.sdk/source/vst/utility/stringconvert.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/dragging.h"
#include "vstgui/lib/idatapackage.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cgradient.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
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

// Entrées du menu RÉF qui ne sont pas des références (actions).
const std::string kImportAction = "\x01import";
const std::string kOnlineAction = "\x01musicbrainz";

constexpr double kPad = 10.0;
// Colonne de travail | colonne Master.
// La mise en page de la maquette « OMNIS Rack », à ses mesures (1200 × 1000, affichée à kZoom) :
// en-tête ; à gauche, 01 STYLE + AUTO, la barre IA, 02 LA CHAÎNE, 03 RÉGLAGE ; à droite, la sortie.
constexpr double kTopH = 72.0;                                   // .top : 16 px de marge, segments de 36 px
constexpr double kWorkL = 20.0, kWorkR = 910.0;                  // .left : 20 px de marge
constexpr double kMasterL = 930.0, kMasterR = 1200.0;            // .right : 270 px
constexpr double kStyleT = 92.0, kIaT = 162.0, kIaB = 256.0;     // 01 / STYLE ; la barre IA
constexpr double kChainHeadT = 278.0, kRackT = 306.0, kRackB = 680.0; // 02 / LA CHAÎNE ; .stage (bibliothèque sur deux rangs)
constexpr double kSetHeadT = 700.0, kDrawerT = 726.0, kDrawerB = 1024.0; // 03 / RÉGLAGE ; .detail
constexpr double kChainX = 14.0, kChainY = 40.0, kChainH = 226.0; // la bande des cartes dans la scène
constexpr double kCardTop = 26.0, kCardW = 148.0, kCardGap = 16.0, kIoW = 34.0;
constexpr double kDrawerSplit = 569.0; // réglages (549 px) | écran du module
const std::array<const char*, 8> kModuleTitles{"Sub", "EQ", "Saturation", "Imager", "Multibande", "Clipper", "Limiteur", "Comp"};
const std::array<const char*, 8> kModuleCodes{"SUB", "EQ", "SAT", "IMG", "MB", "CLIP", "LIM", "COMP"};
const std::array<const char*, 8> kModuleMacros{"SUB", "TON", "CHALEUR", "LARGEUR", "TENUE", "DRIVE", "VOLUME", "COLLE"};
/// Ce que dit chaque caractère (les phrases de la maquette).
std::string characterHelp(const std::string& ch) {
    static const std::map<std::string, std::string> kHelp{
        {"Propre", "Phase linéaire : précis, rien ne bouge dans le temps."},
        {"Analogique", "Cloches larges, aucun pré-écho devant le kick et la 808."},
        {"Opto", "Doux : relâche vite après un coup, lentement quand ça compresse longtemps."},
        {"FET", "Rapide et mordant : fait claquer les drums."},
        {"VCA", "La colle propre du bus, la plus neutre."},
        {"Vari-mu", "Très arrondi, la compression monte peu à peu."},
        {"Inflate", "Densité : plus plein, la crête reste à sa place."},
        {"Bande", "Tasse les crêtes en douceur, colle."},
        {"Lampe", "Harmoniques paires : rond et chaud."},
        {"Transistor", "Propre puis mordant quand on pousse."},
        {"Doux", "Arrondit les crêtes, le plus discret."},
        {"Dur", "Coupe net : plus fort, plus agressif."},
        {"Diode", "Asymétrique : un grain analogique."},
        {"Transparent", "Ne prend que les crêtes qui dépassent : la vibe reste."},
        {"Punch", "Laisse passer les attaques, tient le reste."},
        {"Synth", "Ajoute une octave en dessous, pour une 808 trop maigre."},
        {"Sub propre", "Dose le sub, coupe l'infra-grave inutile."},
    };
    const auto it = kHelp.find(ch);
    return it == kHelp.end() ? std::string() : it->second;
}

/// Une section de la maquette : sans fond (STYLE), `.ia` (#060606, liseré 22 %), `.stage` (#050505,
/// liseré 12 %) ou `.detail` (#060606, liseré 22 %).
class Panel : public CViewContainer {
public:
    enum class Look { Bare, Ia, Stage, Detail };
    explicit Panel(const CRect& r, Look look = Look::Bare) : CViewContainer(r), look_(look) { setTransparency(look == Look::Bare); }
    void drawBackgroundRect(CDrawContext* c, const CRect&) override {
        if (look_ == Look::Bare) return;
        c->setDrawMode(kAntiAliasing | kNonIntegralMode);
        const CRect r(0, 0, getViewSize().getWidth(), getViewSize().getHeight());
        c->setFillColor(look_ == Look::Stage ? CColor(5, 5, 5) : CColor(6, 6, 6));
        c->drawRect(r, kDrawFilled);
        c->setLineWidth(1.0);
        c->setFrameColor(look_ == Look::Stage ? color::kLine : color::kLine2);
        c->drawRect(CRect(r).inset(0.5, 0.5), kDrawStroked);
    }

private:
    Look look_;
};

/// Le fond de la maquette : noir, grille de 48 px, halo ambre en haut à droite, cadre du plug-in,
/// trait sous l'en-tête et colonne de sortie.
class Face : public CViewContainer {
public:
    explicit Face(const CRect& r) : CViewContainer(r) { setTransparency(false); }
    void drawBackgroundRect(CDrawContext* c, const CRect&) override {
        c->setDrawMode(kAntiAliasing | kNonIntegralMode);
        const CRect r(0, 0, getViewSize().getWidth(), getViewSize().getHeight());
        c->setFillColor(CColor(0, 0, 0));
        c->drawRect(r, kDrawFilled);
        // Le halo : radial-gradient(… at 85 % −10 %, rgba(255,170,0,.16), transparent 62 %).
        if (auto path = VSTGUI::owned(c->createGraphicsPath())) {
            path->addRect(r);
            if (auto g = VSTGUI::owned(CGradient::create(0.0, 1.0, CColor(255, 170, 0, 34), CColor(255, 170, 0, 0)))) {
                g->addColorStop(0.45, CColor(255, 170, 0, 14));
                c->fillRadialGradient(path, *g, CPoint(r.getWidth() * 0.85, -r.getHeight() * 0.10), r.getWidth() * 0.62);
            }
        }
        // La grille du site : 48 px, 2,6 %.
        c->setLineWidth(1.0);
        c->setFrameColor(CColor(255, 255, 255, 7));
        for (CCoord x = 0.5; x < r.right; x += 48) c->drawLine(CPoint(x, 0), CPoint(x, r.bottom));
        for (CCoord y = 0.5; y < r.bottom; y += 48) c->drawLine(CPoint(0, y), CPoint(r.right, y));
        // La colonne de sortie : un fond noir à 35 %, un trait à gauche.
        c->setFillColor(CColor(0, 0, 0, 89));
        c->drawRect(CRect(kMasterL, kTopH, r.right, r.bottom), kDrawFilled);
        c->setFrameColor(color::kLine);
        c->drawLine(CPoint(kMasterL + 0.5, kTopH), CPoint(kMasterL + 0.5, r.bottom));
        c->drawLine(CPoint(0, kTopH - 0.5), CPoint(r.right, kTopH - 0.5));
        // Le cadre du plug-in (--line-2).
        c->setFrameColor(color::kLine2);
        c->drawRect(CRect(r).inset(0.5, 0.5), kDrawStroked);
    }
};

/// Une zone dessinée sans fond (en-têtes, puces de style, bibliothèque, niveaux) ; la souris
/// arrive dans les mêmes coordonnées que le dessin.
class Painted : public CView, public Live {
public:
    using Painter = std::function<void(CDrawContext*, const CRect&)>;
    using Click = std::function<void(const CPoint&)>;
    Painted(const CRect& r, Painter p, Click click = {}) : CView(r), painter_(std::move(p)), click_(std::move(click)) {
        if (!click_) setMouseEnabled(false);
    }
    void draw(CDrawContext* c) override {
        c->setDrawMode(kAntiAliasing | kNonIntegralMode);
        if (painter_) painter_(c, getViewSize());
        setDirty(false);
    }
    void onMouseDownEvent(MouseDownEvent& e) override {
        if (!click_ || !e.buttonState.isLeft()) return;
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents(true);
        click_(e.mousePosition);
        invalid();
    }
    void tick() override { invalid(); }

private:
    Painter painter_;
    Click click_;
};

/// `.eyebrow` : le petit carré ambre, puis les capitales espacées (.72rem, .22em).
void eyebrow(CDrawContext* c, const std::string& s, double x, double y) {
    eyebrowMark(c, x, y + 7);
    spaced(c, upperText(s), CRect(x + 20, y, x + 600, y + 14), color::kText2, mono(11.5), 0.22);
}

/// `.shead` : « 02 / » (.74rem, .2em, gris), le titre (1rem, gras, .1em), la phrase (.82rem, sans).
void sectionHead(CDrawContext* c, const CRect& r, const std::string& num, const std::string& title, const std::string& help) {
    const double nw = spacedWidth(c, num, mono(11.8), 0.2);
    spaced(c, num, CRect(r.left, r.top + 2, r.left + nw + 4, r.bottom), color::kText2, mono(11.8), 0.2);
    const double tw = spacedWidth(c, upperText(title), mono(16, true), 0.1);
    spaced(c, upperText(title), CRect(r.left + nw + 16, r.top - 1, r.left + nw + 20 + tw, r.bottom), color::kText, mono(16, true), 0.1);
    if (!help.empty()) text(c, help, CRect(r.left + nw + tw + 36, r.top + 1, r.right, r.bottom), color::kText2, sans(13.1));
}

/// La bande des cartes (`.chain`) : le trait pointillé du signal, ses petits carrés qui avancent
/// tant qu'on écoute OMNIS, « ENTRÉE » à la verticale ; la molette la fait défiler.
class ChainView : public CViewContainer {
public:
    struct Hooks {
        std::function<double()> scroll;          ///< décalage actuel
        std::function<void(double)> scrollBy;   ///< molette
        std::function<bool()> flowing;          ///< APRÈS : le signal passe par OMNIS
    };
    ChainView(const CRect& r, Hooks h) : CViewContainer(r), hooks_(std::move(h)) { setTransparency(true); }
    void drawBackgroundRect(CDrawContext* c, const CRect&) override {
        c->setDrawMode(kAntiAliasing | kNonIntegralMode);
        const CRect r(0, 0, getViewSize().getWidth(), getViewSize().getHeight());
        const double y = std::round(kCardTop + 115.0) + 0.5; // à hauteur des potards (h / 2 + 22 dans la maquette)
        c->setLineWidth(1.0);
        c->setFrameColor(CColor(255, 255, 255, 77));
        for (double x = 0; x < r.right; x += 8) c->drawLine(CPoint(x, y), CPoint(std::min(x + 4, r.right), y));
        if (hooks_.flowing && hooks_.flowing()) {
            const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            for (int k = 0; k < 8; ++k) {
                const double x = std::fmod(t * 80.0 + k * r.getWidth() / 8.0, r.getWidth());
                const double sz = k % 4 == 0 ? 5.0 : 3.0;
                c->setFillColor(k % 4 == 0 ? CColor(255, 255, 255) : color::kAccent);
                c->drawRect(CRect(x - sz / 2, y - sz / 2, x + sz / 2, y + sz / 2), kDrawFilled);
            }
        }
        // `.io` : « ENTRÉE », écrit de bas en haut, devant la première carte.
        const double sx = -(hooks_.scroll ? hooks_.scroll() : 0.0);
        {
            CDrawContext::Transform t(*c, CGraphicsTransform().rotate(-90.0, CPoint(sx + kIoW / 2, y)));
            spaced(c, "ENTRÉE", CRect(sx + kIoW / 2 - 40, y - 7, sx + kIoW / 2 + 40, y + 7), color::kText2, mono(9.5), 0.2, kCenterText);
        }
    }
    void onMouseWheelEvent(MouseWheelEvent& e) override {
        if (!hooks_.scrollBy) return;
        const double d = std::fabs(e.deltaX) > std::fabs(e.deltaY) ? e.deltaX : e.deltaY;
        hooks_.scrollBy(-d * 40.0);
        e.consumed = true;
    }

private:
    Hooks hooks_;
};

/// Gorge usinée entre les réglages et l'écran du tiroir.
class Groove : public CView {
public:
    explicit Groove(const CRect& r) : CView(r) {}
    void draw(CDrawContext* c) override {
        const CRect r = getViewSize();
        c->setLineWidth(1.0);
        c->setFrameColor(color::kLine);
        c->drawLine(CPoint(std::round(r.getCenter().x) + 0.5, r.top + 32), CPoint(std::round(r.getCenter().x) + 0.5, r.bottom));
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
    /// Un clic ouvre la fenêtre RÉF (tous les genres) plutôt que le menu déroulant.
    void setOpener(std::function<void()> open) { open_ = std::move(open); }
    CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override {
        if (open_ && buttons.isLeftButton()) {
            open_();
            return kMouseEventHandled;
        }
        return COptionMenu::onMouseDown(where, buttons);
    }
    void draw(CDrawContext* c) override {
        c->setDrawMode(kAntiAliasing | kNonIntegralMode);
        const CRect r = getViewSize();
        // Une puce de la maquette : « RÉF » en gris, puis la référence visée (ambre) ou « aucune ».
        const std::string name = label_();
        c->setFillColor(CColor(0, 0, 0, 0));
        c->drawRect(r, kDrawFilled);
        c->setLineWidth(1.0);
        c->setFrameColor(name.empty() ? color::kLine2 : color::kAccent35);
        c->drawRect(CRect(r).inset(0.5, 0.5), kDrawStroked);
        spaced(c, "RÉF", CRect(r.left + 11, r.top, r.left + 50, r.bottom), color::kText2, mono(11.5), 0.1);
        std::string shown = name.empty() ? std::string("aucune") : upperText(name);
        const CRect area(r.left + 50, r.top, r.right - 8, r.bottom);
        while (shown.size() > 1 && spacedWidth(c, shown, mono(11.5), 0.1) > area.getWidth()) {
            unsigned char last = 0;
            do {
                last = static_cast<unsigned char>(shown.back());
                shown.pop_back();
            } while (!shown.empty() && (last & 0xC0) == 0x80);
            if (shown.size() > 3 && shown.substr(shown.size() - 3) != "…") shown += "…";
        }
        spaced(c, shown, area, name.empty() ? color::kText3 : color::kAccent, mono(11.5), 0.1);
        setDirty(false);
    }
private:
    std::function<void()> open_;
    std::function<std::string()> label_;
    Thumb thumb_;
};

/// Le logo : Maj + Alt + 5 clics rapides basculent le mode promo (sans menu
/// visible : rien ne trahit la commande à l'écran pendant une vidéo).
class LogoSecret : public CView {
public:
    LogoSecret(const CRect& r, std::function<void()> toggle) : CView(r), toggle_(std::move(toggle)) {}
    void draw(CDrawContext*) override { setDirty(false); }
    void onMouseDownEvent(MouseDownEvent& e) override {
        if (!e.buttonState.isLeft() || !e.modifiers.has(ModifierKey::Shift) || !e.modifiers.has(ModifierKey::Alt)) return;
        e.consumed = true;
        const auto now = std::chrono::steady_clock::now();
        if (clicks_ == 0 || now - first_ > std::chrono::milliseconds(2500)) clicks_ = 0, first_ = now;
        if (++clicks_ >= 5) {
            clicks_ = 0;
            if (toggle_) toggle_();
        }
    }

private:
    std::function<void()> toggle_;
    std::chrono::steady_clock::time_point first_{};
    int clicks_ = 0;
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


/// Un fichier audio glissé sur les rubans du haut (AUTO, barre IA) : AUTO dessus ; avec
/// Maj, il rejoint les références. Seuls les sons connus sont acceptés.
class AudioFileDrop final : public DropTargetAdapter, public NonAtomicReferenceCounted {
public:
    using Callback = std::function<void(const std::string& path, bool shift)>;
    explicit AudioFileDrop(Callback cb) : cb_(std::move(cb)) {}
    DragOperation onDragEnter(DragEventData data) override { return firstAudio(data.drag).empty() ? DragOperation::None : DragOperation::Copy; }
    DragOperation onDragMove(DragEventData data) override { return onDragEnter(data); }
    bool onDrop(DragEventData data) override {
        const std::string path = firstAudio(data.drag);
        if (path.empty() || !cb_) return false;
        cb_(path, data.modifiers.has(ModifierKey::Shift));
        return true;
    }

private:
    static std::string firstAudio(IDataPackage* drag) {
        if (!drag) return {};
        for (uint32_t i = 0; i < drag->getCount(); ++i) {
            const void* buffer = nullptr;
            IDataPackage::Type type = IDataPackage::kError;
            const uint32_t size = drag->getData(i, buffer, type);
            if (type != IDataPackage::kFilePath || !buffer || size == 0) continue;
            std::string path(static_cast<const char*>(buffer), size);
            while (!path.empty() && path.back() == '\0') path.pop_back();
            std::string ext = path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.') + 1);
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext == "wav" || ext == "mp3" || ext == "flac" || ext == "aif" || ext == "aiff") return path;
        }
        return {};
    }
    Callback cb_;
};

} // namespace

// -----------------------------------------------------------------------------
//  Tutoriel : un calque qui éclaire une zone à la fois
// -----------------------------------------------------------------------------
namespace {

struct TutorialStep {
    CRect focus; ///< zone éclairée (coordonnées du cadre)
    const char* title;
    std::array<const char*, 4> lines;
    int since = 1; ///< version du guide qui a ajouté l'étape (montrée après une mise à jour)
};

/// Version du guide : quand elle monte, les étapes nouvelles se montrent une fois
/// à la personne qui avait déjà vu le guide.
constexpr int kTutorialVersion = 2;

const std::array<TutorialStep, 12> kTutorial{{
    {CRect(0, 0, 0, 0), "BIENVENUE DANS OMNIS",
     {"OMNIS masterise ta prod : mets-le en dernier sur le master.", "Clique AUTO, joue le morceau en entier, puis ajuste",
      "avec les boutons ou en mots dans la barre IA.", "Suis le guide (2 minutes)."}},
    {CRect(20, 278, 910, 680), "LA CHAÎNE : TES MODULES",
     {"Chaque carte : un module, son gros bouton et son caractère.", "Clique une carte pour tout régler en dessous ; × la retire de la chaîne.",
      "Glisse une carte pour changer l'ordre ; la bibliothèque (+) la remet.", "La barre du bas montre ce qu'il fait en ce moment."}},
    {CRect(20, 700, 910, 1024), "LE RÉGLAGE : TOUT RÉGLER",
     {"En bas, tous les réglages du module ouvert ; à droite, son écran.", "Glisser vers le haut / le bas · Maj : réglage fin · double-clic : défaut.",
      "Match EQ : glisse les nœuds sur l'écran. Multiband : glisse les traits.", "Rien n'est caché : tu choisis précisément ce que tu changes."}},
    {CRect(20, 700, 910, 1024), "EQ DYNAMIQUE",
     {"Match EQ > DYN : 4 bandes qui n'agissent que quand un son sort du lot", "(sifflante, note de 808 qui gonfle, hi-hat qui pique) ; le reste du temps, rien.",
      "Plage < 0 : retient · > 0 : fait ressortir · seuil : dB au-dessus de l'habitude.", "Écris « ça siffle » dans la barre IA : OMNIS en allume une."}, 2},
    {CRect(20, 162, 910, 256), "LA BARRE IA",
     {"Écris ce que tu veux, en français, en anglais ou en argot :", "« plus de punch », « garde ma 808 », « genre rage », « moins boueux »…",
      "OMNIS écoute ta prod, comprend, règle les modules et te dit ce qu'il a fait.", "RÉF : choisis un style, un artiste ou un titre de ta base ; AUTO le vise."}},
    {CRect(20, 92, 805, 140), "TON STYLE ET TES RÉFÉRENCES",
     {"01 / STYLE : clique un style mesuré, AUTO le vise ensuite.", "TOUS LES GENRES… (ou RÉF) ouvre tous les genres, chacun illustré.",
      "+ AJOUTER UN FICHIER (« Artiste - Titre ») : OMNIS le mesure, sans garder", "le son ; MusicBrainz, sa seule connexion, peut le confirmer."}, 2},
    {CRect(815, 96, 910, 140), "AUTO",
     {"Clique AUTO, puis joue le morceau du début à la fin.", "OMNIS l'écoute en entier, règle la chaîne vers ton style,",
      "essaie plusieurs réglages de volume et garde le plus propre.", "La cible : la RÉF choisie, sinon le style écrit (« rap us », « rage »)."}},
    {CRect(606, 16, 936, 56), "ÉCOUTER HONNÊTEMENT",
     {"AVANT / APRÈS : le son d'origine ou celui d'OMNIS ; A / B : deux réglages.", "MATCH : écoute au même volume que l'original (ça clignote : pense à l'enlever).",
      "DELTA : n'entends que ce que le module ouvert change.", "STÉRÉO / MID / SIDE : vérifie le centre et les côtés."}},
    {CRect(690, 310, 906, 344), "LATENCE",
     {"Qualité max : ~710 ms de latence, normale en mastering (le DAW la compense).", "Un clic : mode TRAVAIL (~90 ms) pour jouer en direct, qualité réduite.",
      "Il clignote en rouge tant qu'il est actif :", "remets la qualité max avant d'exporter."}, 2},
    {CRect(940, 88, 1190, 650), "LE NIVEAU ET LES MESURES",
     {"NIVEAU : la sonie visée, de Streaming -14 à Très fort -7, ou selon le style.", "Puis L / R / LUFS / GR, et chaque mesure avec son verdict en mots.",
      "TRUE PEAK doit rester OK (sous -1 dBTP) pour le streaming.", "PLR = écart crête / sonie : plus il est grand, plus ça respire."}},
    {CRect(940, 654, 1190, 1000), "ÉCOUTES ET EXPORT",
     {"ÉCOUTE : entends ton master sur écouteurs, téléphone, TikTok, en voiture…", "Elle reste en rouge : remets-la sur Off avant d'exporter.",
      "DIAGNOSTIC DU MIX : ce que le master ne rattrape pas, à corriger dans ton projet.",
      "COMPOSITION (en haut) : avec OMNIS sur tes bus (RÔLE Bus), ce qu'il y a dedans."}, 2},
    {CRect(1136, 14, 1186, 58), "C'EST PARTI",
     {"Le bouton ? (ou « tuto » dans la barre IA) relance ce guide.", "Astuce : en mastering, plusieurs petites touches valent mieux", "qu'un gros réglage.", "Bon son !"}},
}};

/// Le guide déjà vu : un petit fichier dans le dossier de la personne (jamais dans
/// le VST3, que le système protège en écriture), avec la version du guide.
std::filesystem::path tutorialFlag() { return userDataDir() / "tutoriel_vu.txt"; }

/// Étape où ouvrir le guide : 0 la première fois, la première étape nouvelle après
/// une mise à jour du guide, -1 s'il est à jour.
int tutorialStartStep() {
    std::error_code ec;
    if (!std::filesystem::exists(tutorialFlag(), ec)) return 0;
    std::ifstream in(tutorialFlag());
    std::string word;
    int seen = 0;
    in >> word >> seen;
    if (seen <= 0) seen = 1; // « vu » seul (avant le 05/11) : version 1
    for (std::size_t i = 0; i < kTutorial.size(); ++i)
        if (kTutorial[i].since > seen) return static_cast<int>(i);
    return -1;
}

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
        double x = std::clamp(focus.left, 20.0, 910.0 - w);
        if (focus.left >= 930.0) x = 910.0 - w;
        double y = focus.bottom + 16;
        if (y + h > all.bottom - 10) y = std::max(10.0, focus.top - h - 16);
        if (focus.left >= 930.0) y = std::clamp(focus.top, 20.0, all.bottom - h - 20);
        return CRect(x, y, x + w, y + h);
    }
    OmnisEditor& editor_;
    CRect card_, next_, skip_, back_;
};

#include "RefBrowser.inc"

void OmnisEditor::openReferenceBrowser(const std::string& family, const std::string& style) {
    if (auto* b = dynamic_cast<RefBrowser*>(refBrowser_)) b->open(family, style);
}

void OmnisEditor::closeReferenceBrowser() {
    if (auto* b = dynamic_cast<RefBrowser*>(refBrowser_)) b->close();
}

int OmnisEditor::tutorialSteps() noexcept { return static_cast<int>(kTutorial.size()); }

void OmnisEditor::startTutorial(int step) {
    tutorialStep_ = step;
    if (step < 0) {
        std::error_code ec;
        std::filesystem::create_directories(tutorialFlag().parent_path(), ec);
        std::ofstream(tutorialFlag()) << "vu " << kTutorialVersion << "\n";
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
    std::array<float, kHistory> limHistory{}, clipHistory{}, compHistory{};
    int historyPos = 0;
    UiMeters meters{};
    std::array<float, 2> peakHold{-120.0f, -120.0f};
    std::array<int, 2> holdTicks{};
    double sampleRate = 48000.0;
    std::uint32_t lastSequence = 0;
    std::map<std::string, SharedPointer<CBitmap>> pictures; ///< images/ : chargées une fois (nullptr : illisible)
    std::map<std::string, SharedPointer<CBitmap>> icons;    ///< icônes du menu RÉF
    std::map<std::string, gfx::avatars::Colors> colors;     ///< couleurs dominantes des avatars
    struct IconRef {
        CMenuItem* item;
        std::string path;
        analysis::SpectralSignature signature;
        std::string seed;
    };
    std::vector<IconRef> iconRefs; ///< pour redessiner les icônes quand le mode promo change
    int promoShown = -1;
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
    ViewRect r(0, 0, static_cast<int32>(std::lround(kWidth * kZoom)), static_cast<int32>(std::lround(kHeight * kZoom)));
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
    frame->setZoom(kZoom); // la maquette est dessinée à 1200 × 1000 ; à l'écran, 86 %
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
    an_->iconRefs.clear();
    slots_ = {};
    pages_ = {};
    pageScreens_ = {};
    spectrum_ = scope_ = meter_ = readouts_ = cardScreen_ = nullptr;
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
    an_->iconRefs.clear();
    auto* face = new Face(CRect(0, 0, kWidth, kHeight));
    root->addView(face);
    buildRibbons(face);
    buildRack(face);
    buildDrawer(face);
    buildMaster(face);
    omnis()->requestFeed();
    // Sur un bus, la console de mastering ne sert pas : la page COMPOSITION d'abord.
    if (omnis()->instanceRole() != params::InstanceRole::Master) page_ = kCompPage;
    selectPage(page_);
    // Le guide, par-dessus tout ; montré tout seul à la toute première ouverture.
    refBrowser_ = new RefBrowser(CRect(0, 0, kWidth, kHeight), *this);
    root->addView(refBrowser_);
    tutorial_ = new TutorialOverlay(CRect(0, 0, kWidth, kHeight), *this);
    root->addView(tutorial_);
    startTutorial(tutorialStartStep());
}

// -----------------------------------------------------------------------------
//  Rubans : transport et demande à l'IA
// -----------------------------------------------------------------------------
void OmnisEditor::buildRibbons(CViewContainer* root) {
    // Un son glissé sur l'en-tête, le style ou la barre IA : AUTO l'écoute en entier (Maj : référence).
    const auto onAudioFile = [this](const std::string& path, bool shift) {
        if (!omnis()) return;
        answer_ = shift ? omnis()->importReferenceFile(path) : omnis()->autoFromFile(path);
        if (frame) frame->invalid();
    };
    OmnisController* c = omnis();
    const auto add = [this](CViewContainer* p, HwButton* b) {
        p->addView(b);
        live_.push_back(b);
        return b;
    };
    const auto addSeg = [this](CViewContainer* p, SegControl* sc) {
        p->addView(sc);
        live_.push_back(sc);
        return sc;
    };

    // ---- .top : la marque à gauche ; à droite, les vues, les mémoires, avant / après, prêt à exporter.
    auto* top = new Panel(CRect(0, 0, kWidth, kTopH));
    root->addView(top);
    top->setDropTarget(makeOwned<AudioFileDrop>(onAudioFile));
    top->addView(new Painted(CRect(20, 16, 380, 56), [](CDrawContext* dc, const CRect& r) {
        // « abrasion » en verre : le contour blanc à 64 %, l'intérieur presque vide.
        const auto glassFont = mono(24, true);
        const CRect g(r.left, r.top + 4, r.left + 170, r.bottom - 4);
        for (const CPoint d : {CPoint(-1, 0), CPoint(1, 0), CPoint(0, -1), CPoint(0, 1)})
            spaced(dc, "abrasion", CRect(g).offset(d.x, d.y), CColor(255, 255, 255, 120), glassFont, 0.08);
        spaced(dc, "abrasion", g, CColor(8, 8, 8), glassFont, 0.08);
        spaced(dc, "abrasion", g, CColor(255, 255, 255, 22), glassFont, 0.08);
        const double gw = spacedWidth(dc, "abrasion", glassFont, 0.08);
        spaced(dc, "LABS", CRect(r.left + gw + 14, r.top + 11, r.left + gw + 70, r.bottom - 4), color::kText2, mono(10.6), 0.28);
        const double lw = spacedWidth(dc, "LABS", mono(10.6), 0.28);
        // « OMNIS » : blanc, halo léger.
        const CRect o(r.left + gw + lw + 30, r.top + 4, r.right, r.bottom - 4);
        spaced(dc, "OMNIS", CRect(o).offset(0, 0.5), CColor(143, 163, 255, 70), mono(24, true), 0.06);
        spaced(dc, "OMNIS", o, CColor(255, 255, 255), mono(24, true), 0.06);
    }));
    top->addView(new LogoSecret(CRect(20, 16, 380, 56), [this] {
        if (!omnis()) return;
        omnis()->setPromoMode(!omnis()->promoMode());
        answer_ = omnis()->promoMode() ? "Mode promo : monogrammes à la place des avatars." : "Mode promo désactivé : les avatars reviennent.";
        refreshPromo();
    }));
    addSeg(top, new SegControl(CRect(388, 18, 586, 54), {"MASTER", "COMPOSITION"}, [this] { return page_ == kCompPage ? 1 : 0; },
                               [this](int i) { selectPage(i == 1 ? static_cast<int>(kCompPage) : lastModule_); }));
    // A / B, et A>B (copie celui qu'on écoute dans l'autre).
    addSeg(top, new SegControl(CRect(606, 18, 774, 54), {"A", "B", c->abSlot() == 0 ? "A>B" : "B>A"}, [c] { return c->abSlot(); }, [this, c](int i) {
        if (i < 2) {
            c->selectAB(i);
            return;
        }
        c->copyABToOther();
        answer_ = c->abSlot() == 0 ? "A copié dans B." : "B copié dans A.";
    }));
    // AVANT / APRÈS : le son d'origine ou celui d'OMNIS.
    addSeg(top, new SegControl(CRect(794, 18, 936, 54), {"AVANT", "APRÈS"}, [this] { return plain(id(ParamId::Bypass)) >= 0.5 ? 0 : 1; },
                               [this](int i) { setPlain(id(ParamId::Bypass), i == 0 ? 1.0 : 0.0); }));
    // « prêt à exporter » (.tag) ; sinon la raison, en orange (.tag.hot). Un clic : le détail.
    auto* ready = new Painted(CRect(956, 25, 1132, 47), [this](CDrawContext* dc, const CRect& r) {
        const auto problems = omnis()->exportProblems();
        if (problems.empty()) {
            tagBox(dc, r, "PRÊT À EXPORTER", color::kAccent);
            return;
        }
        std::string why = i18n::tr(problems.front());
        if (const auto cut = why.find(':'); cut != std::string::npos) why = why.substr(0, cut > 0 && why[cut - 1] == ' ' ? cut - 1 : cut);
        tagBox(dc, r, upperText(why), color::kAccentHot, 9.4);
    }, [this](const CPoint&) { answer_ = omnis()->ask("prêt à exporter"); });
    top->addView(ready);
    live_.push_back(ready);
    addSeg(top, new SegControl(CRect(1142, 18, 1180, 54), {"?"}, [] { return -1; }, [this](int) { startTutorial(0); }));

    // ---- 01 / STYLE : les styles mesurés (.chip), tous les genres, la référence, AUTO (.btn.acc).
    auto* styleRow = new Panel(CRect(kWorkL, kStyleT, kWorkR, kIaT - 14));
    root->addView(styleRow);
    styleRow->setDropTarget(makeOwned<AudioFileDrop>(onAudioFile));
    struct Chip {
        std::string label, target; ///< target vide : ouvrir tous les genres
    };
    auto chips = std::make_shared<std::vector<Chip>>();
    {
        const std::vector<Chip> wanted{{"RAP FR", "Rap Français & New Wave"}, {"RAP US", "Rap & Hip-Hop International (US/UK)"},
                                       {"TRAP", "trap fr"},                 {"RAGE", "rage fr"},
                                       {"PLUGG", "plugg fr"},               {"MÉLODIQUE", "rap mélodique"}};
        const auto catalog = c ? c->genreCatalog() : std::vector<OmnisController::GenreCard>{};
        for (const auto& w : wanted)
            for (const auto& g : catalog)
                if (g.measured() && (g.style.empty() ? g.family : g.style) == w.target) {
                    chips->push_back(w);
                    break;
                }
        chips->push_back({"TOUS LES GENRES…", {}});
    }
    auto chipHits = std::make_shared<std::vector<std::pair<CRect, std::size_t>>>();
    auto* chipRow = new Painted(CRect(0, 0, 606, 50), [this, chips, chipHits](CDrawContext* dc, const CRect& r) {
        eyebrow(dc, "01 / style", r.left, r.top);
        auto* oc = omnis();
        const std::string aimed = oc && oc->referenceKind() == OmnisController::RefKind::Style ? oc->referenceName() : std::string();
        chipHits->clear();
        double x = r.left;
        for (std::size_t i = 0; i < chips->size(); ++i) {
            const Chip& ch = (*chips)[i];
            const double w = spacedWidth(dc, upperText(ch.label), mono(11.5), 0.1) + 22.4;
            const CRect b(x, r.top + 20, x + w, r.top + 48);
            if (b.right > r.right) break;
            const bool on = !ch.target.empty() && ch.target == aimed;
            dc->setFillColor(on ? color::kAccent : CColor(0, 0, 0, 0));
            dc->drawRect(b, kDrawFilled);
            dc->setLineWidth(1.0);
            dc->setFrameColor(on ? color::kAccent : color::kLine2);
            dc->drawRect(CRect(b).inset(0.5, 0.5), kDrawStroked);
            spaced(dc, upperText(ch.label), b, on ? CColor(0, 0, 0) : (ch.target.empty() ? color::kAccent : color::kChip), mono(11.5), 0.1, kCenterText);
            chipHits->push_back({b, i});
            x = b.right + 7.2;
        }
        refChipLeft_ = x;
    }, [this, chips, chipHits](const CPoint& p) {
        for (const auto& [b, i] : *chipHits)
            if (b.pointInside(p)) {
                const Chip& ch = (*chips)[i];
                if (ch.target.empty()) openReferenceBrowser();
                else if (omnis()) answer_ = omnis()->chooseReference(OmnisController::RefKind::Style, ch.target);
                return;
            }
    });
    styleRow->addView(chipRow);
    live_.push_back(chipRow);
    buildReferenceMenu(styleRow, CRect(612, 20, 785, 48)); // la référence visée ; un clic : tous les genres
    auto* autoButton = add(styleRow, new HwButton(CRect(795, 4, 890, 48), "AUTO", [] { return false; }, [this] { answer_ = omnis()->startAuto(); }, false));
    autoButton->setFilled(true);

    // ---- .ia : la demande (IA, le champ, APPLIQUER), puis la réponse d'OMNIS.
    auto* r2 = new Panel(CRect(kWorkL, kIaT, kWorkR, kIaB), Panel::Look::Ia);
    root->addView(r2);
    r2->setDropTarget(makeOwned<AudioFileDrop>(onAudioFile));
    r2->addView(new Painted(CRect(16, 8, 44, 40), [](CDrawContext* dc, const CRect& r) { spaced(dc, "IA", r, color::kAccent, mono(11.8, true), 0.2); }));
    auto* listener = listen([this](CControl*) { askAi(); });
    askField_ = new CTextEdit(CRect(54, 8, 772, 40), listener, 1, "");
    askField_->setFont(mono(14.4));
    askField_->setFontColor(CColor(255, 255, 255));
    askField_->setBackColor(CColor(6, 6, 6));
    askField_->setFrameColor(CColor(6, 6, 6));
    askField_->setTextInset(CPoint(0, 0));
    askField_->setHoriAlign(kLeftText);
    askField_->setPlaceholderString(i18n::tr("plus de colle · 808 plus chaude · plus large · moins fort").c_str());
    askField_->setTooltipText(i18n::tr("Glisse un son ici : AUTO l'écoute en entier (Maj : l'ajouter aux références)").c_str());
    r2->addView(askField_);
    auto* applyButton = add(r2, new HwButton(CRect(780, 6, 882, 42), "APPLIQUER", [] { return false; }, [this] { askAi(); }, false));
    applyButton->setWhite(true);
    auto* answer = new Painted(CRect(0, 48, kWorkR - kWorkL, kIaB - kIaT), [this](CDrawContext* dc, const CRect& r) {
        dc->setLineWidth(1.0);
        dc->setFrameColor(color::kLine);
        dc->drawLine(CPoint(r.left + 1, r.top + 0.5), CPoint(r.right - 1, r.top + 0.5));
        const CRect line(r.left + 16, r.top + 10, r.right - 16, r.top + 30);
        spaced(dc, "OMNIS", line, color::kAccent, mono(12.8), 0.14);
        const std::string& said = answer_.empty() ? std::string("choisis un style ou clique AUTO. Tu peux aussi glisser des modules dans la chaîne.") : answer_;
        text(dc, said, CRect(line.left + 66, line.top, line.right, line.bottom), color::kInk2, mono(12.8));
    });
    r2->addView(answer);
    live_.push_back(answer);
}

struct OmnisEditor::Card {
    std::string key = "\x01";
    OmnisController::ReferenceCard data;
};

CBitmap* OmnisEditor::picture(const std::string& path) {
    // Mode promo : aucune image du cache n'est même ouverte (seuls les monogrammes).
    if (path.empty() || (omnis() && omnis()->promoMode())) return nullptr;
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
    if (omnis() && omnis()->promoMode()) {
        paintMonogram(c, r, imagePath, sig, seed);
        return;
    }
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

namespace {

/// « La Fève - CINÉMA » -> « LF » ; « GRY » -> « GR » (lettres latines, en majuscules).
std::string initialsOf(const std::string& seed) {
    std::string name = seed.substr(0, seed.find(" - "));
    std::vector<std::string> words;
    std::string word;
    for (std::size_t i = 0; i <= name.size(); ++i) {
        const unsigned char ch = i < name.size() ? static_cast<unsigned char>(name[i]) : ' ';
        if (std::isalnum(ch) || ch >= 0x80) word += static_cast<char>(ch);
        else if (!word.empty()) words.push_back(word), word.clear();
    }
    const auto firstLetter = [](const std::string& w) {
        std::size_t n = 1;
        while (n < w.size() && (static_cast<unsigned char>(w[n]) & 0xC0) == 0x80) ++n; // un caractère UTF-8 entier
        std::string out = w.substr(0, n);
        if (out.size() == 1) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
        return out;
    };
    if (words.empty()) return "?";
    if (words.size() == 1) {
        std::string w = words.front();
        const std::string a = firstLetter(w);
        w.erase(0, a.size());
        return w.empty() ? a : a + firstLetter(w);
    }
    return firstLetter(words[0]) + firstLetter(words[1]);
}

CColor fromRgb(const std::array<std::uint8_t, 3>& v) { return CColor(v[0], v[1], v[2]); }
double lumOf(const CColor& col) { return (0.299 * col.red + 0.587 * col.green + 0.114 * col.blue) / 255.0; }

} // namespace

void OmnisEditor::paintMonogram(CDrawContext* c, const CRect& r, const std::string& imagePath, const analysis::SpectralSignature& sig,
                                const std::string& seed) {
    // Les deux couleurs dominantes de la vraie image (calculées quand l'avatar a été
    // fait), sinon une teinte tirée de la tonalité / du nom.
    gfx::avatars::Colors colors;
    if (!imagePath.empty()) {
        auto it = an_->colors.find(imagePath);
        if (it == an_->colors.end()) it = an_->colors.emplace(imagePath, gfx::avatars::readColors(std::filesystem::path(std::u8string(imagePath.begin(), imagePath.end())))).first;
        colors = it->second;
    }
    CColor a, b;
    if (colors.valid) {
        a = fromRgb(colors.rgb[0]);
        b = fromRgb(colors.rgb[1]);
    } else {
        std::uint32_t h = 2166136261u;
        for (unsigned char ch : seed) h = (h ^ ch) * 16777619u;
        const double hue = sig.music.keyRoot >= 0 ? std::fmod(sig.music.keyRoot * 210.0, 360.0) : static_cast<double>(h % 360);
        a.fromHSV(hue, 0.6, 0.35);
        b.fromHSV(std::fmod(hue + 30.0, 360.0), 0.7, 0.95);
    }
    // Fond : la plus sombre, assombrie ; trait et lettres : la plus claire, éclaircie
    // (lisible même compressé en vidéo).
    CColor dark = lumOf(a) <= lumOf(b) ? a : b, light = lumOf(a) <= lumOf(b) ? b : a;
    const auto scale = [](CColor col, double k) {
        return CColor(static_cast<uint8_t>(std::clamp(col.red * k, 0.0, 255.0)), static_cast<uint8_t>(std::clamp(col.green * k, 0.0, 255.0)),
                      static_cast<uint8_t>(std::clamp(col.blue * k, 0.0, 255.0)));
    };
    dark = scale(dark, 0.45);
    if (lumOf(light) < 0.65) light = scale(light, 0.65 / std::max(0.05, lumOf(light)));
    c->setFillColor(dark);
    c->drawRect(r, kDrawFilled);
    const double side = std::min(r.getWidth(), r.getHeight());
    const CPoint mid = r.getCenter();
    if (side >= 32) {
        // Couronne : les 21 bandes Mid de la fiche, en rayons autour des initiales.
        const double top = *std::max_element(sig.midDb.begin(), sig.midDb.end());
        const int n = static_cast<int>(sig.midDb.size());
        c->setFrameColor(withAlpha(light, 0.85));
        c->setLineWidth(std::max(1.5, side / 40.0));
        c->setLineStyle(kLineSolid);
        for (int k = 0; k < n; ++k) {
            const double v = std::clamp((sig.midDb[static_cast<std::size_t>(k)] - (top - 30.0)) / 30.0, 0.08, 1.0);
            const double ang = -std::numbers::pi / 2 + 2.0 * std::numbers::pi * k / n;
            const double r0 = side * 0.31, r1 = r0 + side * 0.15 * v;
            c->drawLine(CPoint(mid.x + r0 * std::cos(ang), mid.y + r0 * std::sin(ang)), CPoint(mid.x + r1 * std::cos(ang), mid.y + r1 * std::sin(ang)));
        }
    }
    std::string letters = initialsOf(seed);
    if (side < 32) letters = letters.substr(0, letters.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 1) == std::string::npos ? letters.size() : 1);
    text(c, letters, CRect(mid.x - side / 2, mid.y - side * 0.2, mid.x + side / 2, mid.y + side * 0.2), light, sans(std::max(9.0, side * (side < 32 ? 0.55 : 0.26)), true),
         kCenterText);
}

void OmnisEditor::pickReferenceFile() {
    if (!omnis()) return;
    auto* selector = CNewFileSelector::create(frame, CNewFileSelector::kSelectFile);
    if (!selector) {
        answer_ = "Pas de sélecteur de fichiers ici : utilise omnis_refdb reconnaitre puis scan.";
        return;
    }
    selector->setTitle(UTF8String(i18n::tr("Fichier de référence : « Artiste - Titre » (WAV, MP3, FLAC)")));
    selector->addFileExtension(CFileExtension("WAV", "wav"));
    selector->addFileExtension(CFileExtension("MP3", "mp3"));
    selector->addFileExtension(CFileExtension("FLAC", "flac"));
    selector->run([this, alive = std::weak_ptr<bool>(alive_)](CNewFileSelector* sel) {
        if (alive.expired() || sel->getNumSelectedFiles() == 0 || !omnis()) return;
        answer_ = omnis()->importReferenceFile(sel->getSelectedFile(0));
        if (frame) frame->invalid();
    });
    selector->forget();
}

void OmnisEditor::refreshPromo() {
    if (!omnis()) return;
    const int now = omnis()->promoMode() ? 1 : 0;
    if (now == an_->promoShown) return;
    an_->promoShown = now;
    an_->icons.clear();    // les icônes changent de nature (image <-> monogramme)
    an_->pictures.clear(); // promo : plus aucune image du cache gardée en mémoire
    for (const auto& ref : an_->iconRefs) {
        SharedPointer<CBitmap> b;
        if (auto ctx = COffscreenContext::create(CPoint(20, 20))) {
            ctx->beginDraw();
            paintCover(ctx, CRect(0, 0, 20, 20), ref.path, ref.signature, ref.seed);
            ctx->endDraw();
            b = ctx->getBitmap();
        }
        if (ref.item) ref.item->setIcon(b);
    }
    if (cardScreen_) cardScreen_->markDirty();
    if (frame) frame->invalid();
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
    engraved(c, "RÉFÉRENCE VISÉE", CRect(x, r.top + 6, r.right - 8, r.top + 18));
    text(c, d.name, CRect(x, r.top + 20, r.right - 8, r.top + 36), color::kAccent, mono(11, true));
    text(c, d.detail, CRect(x, r.top + 38, r.right - 8, r.top + 50), color::kText2, mono(9.5));
    // Une image ne se montre qu'avec sa source (auteur et licence, ou Deezer).
    const bool promo = omnis() && omnis()->promoMode();
    std::string credits = promo ? std::string() : d.credit; // mode promo : aucune image, rien à créditer
    if (!promo && !d.coverCredit.empty() && d.coverCredit != d.credit) credits += (credits.empty() ? "" : " · ") + d.coverCredit;
    if (!credits.empty()) text(c, credits, CRect(x, r.bottom - 17, r.right - 8, r.bottom - 5), color::kText3, mono(8.5));
    // La musique en ligne : un clic sur la carte l'ouvre dans le navigateur.
    if (!promo && !d.link.empty()) {
        std::string shown = d.link;
        for (const char* prefix : {"https://", "www."})
            if (shown.rfind(prefix, 0) == 0) shown.erase(0, std::strlen(prefix));
        text(c, "▶ " + shown, CRect(x, r.top + 52, r.right - 8, r.top + 64), color::kCool, mono(9));
    }
}

void OmnisEditor::buildReferenceMenu(CViewContainer* parent, const CRect& r) {
    using Kind = OmnisController::RefKind;
    auto* c = omnis();
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
        if (pick == list->second.end()) return;
        if (pick->second.name == kImportAction) {
            pickReferenceFile();
            return;
        }
        if (pick->second.name == kOnlineAction) {
            const bool on = !omnis()->onlineRecognition();
            omnis()->setOnlineRecognition(on);
            answer_ = on ? "Recherche MusicBrainz activée : une requête par fichier ajouté, rien d'autre ne va sur internet."
                         : "Recherche MusicBrainz désactivée : OMNIS reconnaît les fichiers hors ligne (étiquettes, nom « Artiste - Titre »).";
            if (refFill_) refFill_();
            return;
        }
        answer_ = omnis()->chooseReference(pick->second.kind, pick->second.name);
    }), [this] {
        auto* oc = omnis();
        return oc && oc->referenceKind() != Kind::None ? oc->referenceName() : std::string();
    }, [this](CDrawContext* dc, const CRect& rr) { paintCover(dc, rr, card_->data.cover, card_->data.signature, card_->data.name); });
    menu->setFont(mono(10.5));
    menu->setFontColor(color::kText);
    menu->setBackColor(color::kScreen);
    menu->setOpener([this] { openReferenceBrowser(); });
    const auto add = [picks](COptionMenu* to, const std::string& label, Kind kind, const std::string& name) {
        auto* item = to->addEntry(UTF8String(i18n::tr(label)));
        (*picks)[to][to->getNbEntries() - 1] = Pick{kind, name};
        return item;
    };
    // Petite icône de menu : la pochette (ou la photo), dessinée hors écran.
    const auto icon = [this](const std::string& path, const analysis::SpectralSignature& sig, const std::string& seed) -> CBitmap* {
        const std::string key = seed + "\x1f" + path + (omnis() && omnis()->promoMode() ? "\x1fpromo" : "");
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
    // Le contenu du menu, refait quand le filtre de famille change (même menu, mêmes liens).
    refFill_ = [this, menu, picks, add, icon] {
        auto* oc = omnis();
        const auto tree = oc ? oc->referenceTree(refFamily_, refStyle_) : OmnisController::ReferenceTree{};
        menu->removeAllEntry();
        picks->clear();
        an_->iconRefs.clear();
        const auto plural = [](std::size_t n) { return std::to_string(n) + (n > 1 ? " titres" : " titre"); };
        // Un artiste : tous ses titres, puis chacun (pochette en icône).
        const auto artistMenu = [this, add, icon, plural](COptionMenu* to, const OmnisController::ReferenceArtist& a) {
            auto* sub = new COptionMenu();
            sub->setNbItemsPerColumn(30);
            add(sub, "Tous les titres de " + a.name + " (" + plural(a.titles.size()) + ")", Kind::Artist, a.name);
            sub->addSeparator();
            for (const auto& t : a.titles)
                if (auto* item = add(sub, t.title, Kind::Title, t.name)) {
                    item->setIcon(icon(t.cover, t.signature, t.name));
                    an_->iconRefs.push_back({item, t.cover, t.signature, t.name});
                }
            auto* item = to->addEntry(sub, UTF8String(a.name + "  (" + std::to_string(a.titles.size()) + ")"));
            if (item && !a.titles.empty()) {
                item->setIcon(icon(a.photo, a.titles.front().signature, a.name));
                an_->iconRefs.push_back({item, a.photo, a.titles.front().signature, a.name});
            }
            sub->forget();
        };
        add(menu, "Aucune (OMNIS suit tes mots)", Kind::None, {});
        add(menu, "Ajouter un fichier de référence… (« Artiste - Titre »)", Kind::None, kImportAction);
        add(menu, oc && oc->onlineRecognition() ? "Recherche MusicBrainz : activée (cliquer pour couper)" : "Recherche MusicBrainz : coupée (cliquer pour activer)",
            Kind::None, kOnlineAction);
        if (!refFamily_.empty())
            menu->addEntry(UTF8String(i18n::tr("Filtre : ") + i18n::tr(refFamily_) + (refStyle_.empty() ? std::string() : " › " + i18n::tr(refStyle_))), -1, CMenuItem::kDisabled);
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
        if (tree.styles.empty()) styles->addEntry(UTF8String(i18n::tr("(rien de mesuré)")), -1, CMenuItem::kDisabled);
        menu->addEntry(styles, UTF8String(i18n::tr("Styles")));
        styles->forget();
        auto* artists = new COptionMenu();
        artists->setNbItemsPerColumn(30);
        for (const auto& a : tree.artists) artistMenu(artists, a);
        if (tree.artists.empty()) artists->addEntry(UTF8String(i18n::tr("(rien de mesuré)")), -1, CMenuItem::kDisabled);
        menu->addEntry(artists, UTF8String(i18n::tr("Artistes de A à Z")));
        artists->forget();
    };
    refFill_();
    parent->addView(menu);
    an_->promoShown = c && c->promoMode() ? 1 : 0; // icônes faites dans le mode courant
    // La face et la carte suivent le choix (fait ici, par AUTO ou relu avec la session),
    // et le mode promo (« /fbi » passe par la barre IA).
    syncs_.push_back([this, menu] {
        refreshPromo();
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
bool OmnisEditor::onAfter(int first, int later) const {
    bool seen = false;
    for (const int m : order()) {
        if (seen && m == later) return plain(id(params::moduleOnId(static_cast<params::Module>(later)))) >= 0.5;
        if (m == first) seen = true;
    }
    return false;
}

std::array<int, 8> OmnisEditor::order() const {
    static_assert(params::kModules == 8);
    const auto o = params::chainOrder(static_cast<int>(std::lround(plain(id(ParamId::ChainOrder)))));
    std::array<int, 8> out{};
    for (int i = 0; i < params::kModules; ++i) out[static_cast<std::size_t>(i)] = static_cast<int>(o[static_cast<std::size_t>(i)]);
    return out;
}

namespace {
/// Largeur d'un emplacement du rack : les modules côte à côte, 6 px entre eux.
} // namespace

void OmnisEditor::buildRack(CViewContainer* root) {
    OmnisController* c = omnis();
    // 02 / LA CHAÎNE (.shead), au-dessus de la scène.
    root->addView(new Painted(CRect(kWorkL, kChainHeadT, kWorkR, kChainHeadT + 20), [](CDrawContext* dc, const CRect& r) {
        sectionHead(dc, r, "02 /", "La chaîne", "Glisse pour réordonner · clic pour régler · × pour retirer");
    }));
    rack_ = new Panel(CRect(kWorkL, kRackT, kWorkR, kRackB), Panel::Look::Stage);
    root->addView(rack_);
    const double stageW = rack_->getViewSize().getWidth();
    // `.hud` : à gauche, la chaîne et l'entrée ; à droite, la latence (un clic : le mode travail) et la sortie.
    auto* hud = new Painted(CRect(10, 8, stageW - 10, 36), [this](CDrawContext* dc, const CRect& r) {
        const auto f = mono(9.5);
        const double in = std::max(an_->meters.inPeakDb[0], an_->meters.inPeakDb[1]);
        spaced(dc, "OBJ " + fmt("%03.0f", static_cast<double>(shownModules_.size())) + " · " + upperText(i18n::tr("chaîne")), CRect(r.left, r.top, r.right, r.top + 14),
               color::kText2, f, 0.14);
        spaced(dc, upperText(i18n::tr("entrée")) + " " + (in <= -119.0 ? std::string("—") : fmt("%.1f", in)) + " DBFS", CRect(r.left, r.top + 14, r.right, r.top + 28),
               color::kText2, f, 0.14);
        const bool low = plain(id(ParamId::LowLatency)) >= 0.5;
        const double ms = an_->meters.latencyMs;
        const std::string lat = (low ? upperText(i18n::tr("mode travail")) + " " : upperText(i18n::tr("latence")) + " ") + (ms > 0.0 ? fmt("%.0f", ms) + " MS" : "—");
        const bool lit = !low || (std::chrono::steady_clock::now().time_since_epoch() / std::chrono::milliseconds(600)) % 2 == 0;
        spaced(dc, lat, CRect(r.left, r.top, r.right, r.top + 14), low ? withAlpha(color::kAccentHot, lit ? 1.0 : 0.45) : color::kText2, f, 0.14, kRightText);
        const double st = plain(id(ParamId::MeterShortTerm));
        spaced(dc, upperText(i18n::tr("sortie")) + " " + (st <= -79.0 ? std::string("—") : fmt("%.1f", st)) + " LUFS", CRect(r.left, r.top + 14, r.right, r.top + 28),
               color::kText2, f, 0.14, kRightText);
    }, [this](const CPoint& p) {
        if (p.x < rack_->getViewSize().getWidth() - 220) return; // seule la latence (à droite) se clique
        const bool low = plain(id(ParamId::LowLatency)) >= 0.5;
        setPlain(id(ParamId::LowLatency), low ? 0.0 : 1.0);
        answer_ = low ? "QUALITÉ MAX : filtres complets, la meilleure qualité (latence ~0,7 s). C'est le réglage d'export."
                      : "MODE TRAVAIL (latence ~90 ms) : qualité RÉDUITE — grave moins précis, multibande et limiteur plus durs. "
                        "Pour régler à l'oreille seulement : REPASSE EN QUALITÉ MAX AVANT D'EXPORTER (clic sur la latence). "
                        "Un export hors ligne repasse de lui-même en qualité max quand FL le signale.";
    });
    hud->setTooltipText(i18n::tr("Latence : clic = mode travail (latence basse, qualité réduite) / qualité max (export)").c_str());
    rack_->addView(hud);
    live_.push_back(hud);
    ChainView::Hooks hooks;
    hooks.scroll = [this] { return chainScroll_; };
    hooks.scrollBy = [this](double d) {
        chainScroll_ += d;
        layoutRack();
    };
    hooks.flowing = [this] { return plain(id(ParamId::Bypass)) < 0.5; };
    chain_ = new ChainView(CRect(kChainX, kChainY, stageW - kChainX, kChainY + kChainH), hooks);
    rack_->addView(chain_);
    // Les modules éteints à la première ouverture partent dans la bibliothèque (le Comp, d'habitude).
    if (c && c->chainHidden() < 0) {
        int mask = 0;
        for (int m = 0; m < params::kModules; ++m)
            if (c->getParamNormalized(id(params::moduleOnId(static_cast<params::Module>(m)))) < 0.5) mask |= 1 << m;
        c->setChainHidden(mask);
    }
    for (int m = 0; m < params::kModules; ++m) {
        const auto module = static_cast<params::Module>(m);
        const std::uint32_t onId = id(params::moduleOnId(module));
        RackSlot::Callbacks cb;
        cb.isOn = [c, onId] { return c->getParamNormalized(onId) >= 0.5; };
        cb.toggle = [this, onId] { setNorm(onId, norm(onId) >= 0.5 ? 0.0 : 1.0); };
        cb.isSelected = [this, m] { return page_ == m; };
        cb.select = [this, m] { selectPage(m); };
        cb.code = kModuleCodes[static_cast<std::size_t>(m)];
        cb.remove = [this, m, onId] {
            if (auto* o = omnis()) o->setChainHidden(o->chainHidden() | (1 << m));
            setNorm(onId, 0.0);
            answer_ = std::string(i18n::tr(kModuleTitles[static_cast<std::size_t>(m)])) + i18n::tr(" retiré de la chaîne.");
            layoutRack();
            if (page_ == m && !shownModules_.empty()) selectPage(shownModules_.front());
        };
        cb.character = [this, m] { return moduleCharacter(m); };
        cb.nextCharacter = [this, m] {
            nextCharacter(m);
            selectPage(m);
            const std::string ch = moduleCharacter(m);
            answer_ = std::string(i18n::tr(kModuleTitles[static_cast<std::size_t>(m)])) + i18n::tr(" : caractère ") + i18n::tr(ch) + ". " + i18n::tr(characterHelp(ch));
        };
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
                    // EQ dynamique allumée : sa retenue la plus forte du moment.
                    int dynOn = 0;
                    double dynDb = 0.0;
                    for (int b = 0; b < params::kDynBands; ++b)
                        if (plain(id(params::dynId(b, params::DynControl::On))) >= 0.5) {
                            ++dynOn;
                            const double g = an_->meters.dynamicGainDb[b];
                            if (std::fabs(g) > std::fabs(dynDb)) dynDb = g;
                        }
                    if (dynOn > 0) return std::to_string(n) + (n > 1 ? " NŒUDS" : " NŒUD") + " · DYN " + fmt("%+.1f", std::fabs(dynDb) < 0.05 ? 0.0 : dynDb);
                    // Court (le rack a 8 modules) : la pente, les nœuds, « A » en Analogique.
                    return fmt("%+.1f", plain(id(ParamId::MatchTilt))) + " · " + std::to_string(n) + (n > 1 ? " NŒUDS" : " NŒUD") +
                           (plain(id(ParamId::MatchCharacter)) >= 0.5 ? " · A" : "");
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
                cb.status = [this] {
                    // Sans limiteur allumé derrière lui, le clipper tient lui-même le plafond.
                    return "CLIP " + fmt("%.1f dB", an_->meters.clipperGrDb) +
                           (onAfter(static_cast<int>(params::Module::Clipper), static_cast<int>(params::Module::Limiter)) ? "" : " · PLAFOND TENU");
                };
                cb.level = [this] { return -an_->meters.clipperGrDb / 12.0; };
                macro = model(id(ParamId::ClipDrive));
                break;
            case params::Module::Limiter:
                cb.status = [this] {
                    // Un module allumé après le limiteur peut repasser son plafond.
                    bool after = false;
                    for (int m = 0; m < params::kModules; ++m)
                        after = after || (m != static_cast<int>(params::Module::Limiter) &&
                                          onAfter(static_cast<int>(params::Module::Limiter), m));
                    return "GR " + fmt("%.1f dB", an_->meters.limiterGrDb) + (after ? " · PAS DERNIER" : "");
                };
                cb.level = [this] { return -an_->meters.limiterGrDb / 12.0; };
                macro = model(id(ParamId::LimGain));
                break;
            case params::Module::Comp: {
                cb.status = [this] {
                    const bool on = plain(id(ParamId::CompOn)) >= 0.5;
                    return "GR " + fmt("%.1f dB", on ? an_->meters.compGrDb : 0.0f) + " · " + i18n::tr(valueText(id(ParamId::CompCharacter)));
                };
                cb.level = [this] { return plain(id(ParamId::CompOn)) >= 0.5 ? -an_->meters.compGrDb / 12.0 : 0.0; };
                // « Squeeze » : de rien (seuil 0 dB) à fort (seuil −36 dB), compensation de la moitié.
                const auto thr = id(ParamId::CompThreshold), mk = id(ParamId::CompMakeup), ratio = id(ParamId::CompRatio);
                macro.get = [this, thr] { return clamp01(-plain(thr) / 36.0); };
                macro.set = [this, c, thr, mk, ratio](double t) {
                    const double threshold = -36.0 * t, r = std::max(1.0, plain(ratio));
                    c->performGesture(thr, params::toNormalized(*params::findParameter(thr), threshold));
                    c->performGesture(mk, params::toNormalized(*params::findParameter(mk), std::clamp(-0.25 * threshold * (1.0 - 1.0 / r), 0.0, 12.0)));
                };
                macro.begin = [c, thr, mk] {
                    c->beginGesture(thr);
                    c->beginGesture(mk);
                };
                macro.end = [c, thr, mk] {
                    c->endGesture(thr);
                    c->endGesture(mk);
                };
                macro.text = [this, thr] { return fmt("%.0f %%", 100.0 * clamp01(-plain(thr) / 36.0)); };
                break;
            }
        }
        auto* slot = new RackSlot(CRect(0, 0, kCardW + 2 * RackSlot::kMarginX, RackSlot::kCardH + RackSlot::kMarginTop + RackSlot::kMarginBottom),
                                  std::to_string(m + 1), kModuleTitles[static_cast<std::size_t>(m)], cb);
        const double kh = LedKnob::heightFor(LedKnob::Size::Big);
        const double kx = RackSlot::kMarginX, ky = RackSlot::kMarginTop + 62.0;
        auto* knob = new LedKnob(CRect(kx + 4, ky, kx + kCardW - 4, ky + kh), kModuleMacros[static_cast<std::size_t>(m)], macro, LedKnob::Size::Big);
        slot->addView(knob);
        live_.push_back(knob);
        slotKnobs_[static_cast<std::size_t>(m)] = knob;
        chain_->addView(slot);
        live_.push_back(slot);
        slots_[static_cast<std::size_t>(m)] = slot;
    }
    // `.lib` : tous les modules, comme la maquette ; un clic remet un module retiré, ou montre sa carte.
    auto libHits = std::make_shared<std::vector<std::pair<CRect, int>>>();
    static const std::array<int, 8> kCharacters{2, 2, 4, 0, 0, 3, 2, 4}; // Sub, EQ, Saturation, Imager, Multibande, Clipper, Limiteur, Comp
    auto* library = new Painted(CRect(0, kChainY + kChainH + 18, stageW, rack_->getViewSize().getHeight()),
                                [this, libHits](CDrawContext* dc, const CRect& r) {
        dc->setLineWidth(1.0);
        dc->setFrameColor(color::kLine2);
        for (double x = r.left + 1; x < r.right - 1; x += 6) dc->drawLine(CPoint(x, r.top + 0.5), CPoint(std::min(x + 3, r.right - 1), r.top + 0.5));
        libHits->clear();
        double x = r.left + 14, y = r.top + 14;
        const int hidden = omnis() ? omnis()->chainHidden() : 0;
        for (int m : order()) {
            const std::string name = upperText(i18n::tr(kModuleTitles[static_cast<std::size_t>(m)]));
            const int nc = kCharacters[static_cast<std::size_t>(m)];
            const std::string more = nc > 0 ? fmt("%.0f", static_cast<double>(nc)) + i18n::tr(nc > 1 ? " caractères" : " caractère") : std::string();
            const double wName = spacedWidth(dc, name, mono(11.2), 0.1);
            const double wMore = more.empty() ? 0.0 : spacedWidth(dc, more, mono(9.9), 0.06) + 7.2;
            const double w = 10.4 + 7.2 + 7.2 + wName + wMore + 10.4;
            if (x + w > r.right - 14) {
                x = r.left + 14;
                y += 34;
            }
            const CRect b(x, y, x + w, y + 27);
            const bool out = (hidden >> m) & 1;
            dc->setFillColor(CColor(0, 0, 0));
            dc->drawRect(b, kDrawFilled);
            dc->setFrameColor(out ? color::kLine2 : color::kLine);
            dc->drawRect(CRect(b).inset(0.5, 0.5), kDrawStroked);
            text(dc, "+", CRect(b.left + 10, b.top, b.left + 20, b.bottom), withAlpha(color::kAccent, out ? 1.0 : 0.5), mono(11.2));
            spaced(dc, name, CRect(b.left + 24.8, b.top, b.left + 26 + wName, b.bottom), out ? color::kChip : color::kText2, mono(11.2), 0.1);
            if (!more.empty()) spaced(dc, more, CRect(b.left + 32 + wName, b.top + 1, b.right - 6, b.bottom), color::kText3, mono(9.9), 0.06);
            libHits->push_back({b, m});
            x = b.right + 7.2;
        }
    }, [this, libHits](const CPoint& p) {
        for (const auto& [b, m] : *libHits)
            if (b.pointInside(p)) {
                auto* o = omnis();
                if (o && ((o->chainHidden() >> m) & 1)) {
                    o->setChainHidden(o->chainHidden() & ~(1 << m));
                    setNorm(id(params::moduleOnId(static_cast<params::Module>(m))), 1.0);
                    answer_ = std::string(i18n::tr(kModuleTitles[static_cast<std::size_t>(m)])) + i18n::tr(" remis dans la chaîne.");
                    layoutRack();
                }
                selectPage(m);
                return;
            }
    });
    library_ = library;
    rack_->addView(library);
    live_.push_back(library);
    layoutRack();
}

void OmnisEditor::layoutRack() {
    if (!rack_ || !chain_) return;
    const auto o = order();
    const int hidden = omnis() ? std::max(0, omnis()->chainHidden()) : 0;
    shownModules_.clear();
    for (int m : o)
        if (!((hidden >> m) & 1)) shownModules_.push_back(m);
    const double first = kIoW + 16.0;
    chainContentW_ = first + static_cast<double>(shownModules_.size()) * (kCardW + kCardGap) - kCardGap + 12.0;
    const double viewW = chain_->getViewSize().getWidth();
    chainScroll_ = std::clamp(chainScroll_, 0.0, std::max(0.0, chainContentW_ - viewW));
    slotW_ = kCardW;
    for (auto* s : slots_)
        if (s) s->setVisible(false);
    for (std::size_t pos = 0; pos < shownModules_.size(); ++pos) {
        const int m = shownModules_[pos];
        RackSlot* s = slots_[static_cast<std::size_t>(m)];
        if (!s) continue;
        const double x = first + static_cast<double>(pos) * (kCardW + kCardGap) - chainScroll_;
        const CRect r(x - RackSlot::kMarginX, kCardTop - RackSlot::kMarginTop, x + kCardW + RackSlot::kMarginX, kCardTop + RackSlot::kCardH + RackSlot::kMarginBottom);
        if (s->getViewSize() != r) {
            s->setViewSize(r);
            s->setMouseableArea(r);
        }
        s->setNumber(std::to_string(pos + 1));
        s->setDropMark(0);
        s->setVisible(true);
    }
    lastMask_ = hidden;
    for (int m = 0; m < params::kModules; ++m)
        if (norm(id(params::moduleOnId(static_cast<params::Module>(m)))) >= 0.5) lastMask_ |= 1 << (m + 8);
    rack_->invalid();
}

void OmnisEditor::scrollChainTo(int module) {
    if (!chain_) return;
    const auto it = std::find(shownModules_.begin(), shownModules_.end(), module);
    if (it == shownModules_.end()) return;
    const double x = kIoW + 16.0 + static_cast<double>(it - shownModules_.begin()) * (kCardW + kCardGap);
    const double viewW = chain_->getViewSize().getWidth();
    if (x - 16 < chainScroll_) chainScroll_ = x - 16;
    else if (x + kCardW + 16 > chainScroll_ + viewW) chainScroll_ = x + kCardW + 16 - viewW;
    layoutRack();
}

std::string OmnisEditor::moduleCharacter(int m) const {
    using M = params::Module;
    switch (static_cast<M>(m)) {
        case M::Sub: return plain(id(ParamId::SubSynth)) > 0.5 ? "Synth" : "Propre";
        case M::Match: return plain(id(ParamId::MatchCharacter)) >= 0.5 ? "Analogique" : "Propre";
        case M::Inflator: {
            static const std::array<const char*, 4> k{"Inflate", "Bande", "Lampe", "Transistor"};
            return k[static_cast<std::size_t>(std::clamp<long>(std::lround(plain(id(ParamId::InflCharacter))), 0, 3))];
        }
        case M::Clipper: {
            static const std::array<const char*, 3> k{"Dur", "Doux", "Diode"}; // Hard, Soft Knee, Diode
            return k[static_cast<std::size_t>(std::clamp<long>(std::lround(plain(id(ParamId::ClipMode))), 0, 2))];
        }
        case M::Limiter: return plain(id(ParamId::LimPunch)) > 0.5 ? "Punch" : "Transparent";
        case M::Comp: {
            static const std::array<const char*, 4> k{"Opto", "FET", "VCA", "Vari-mu"};
            return k[static_cast<std::size_t>(std::clamp<long>(std::lround(plain(id(ParamId::CompCharacter))), 0, 3))];
        }
        default: return {};
    }
}

void OmnisEditor::nextCharacter(int m) {
    using M = params::Module;
    const auto cycle = [this](ParamId p, int n) { setPlain(id(p), static_cast<double>((std::lround(plain(id(p))) + 1) % n)); };
    switch (static_cast<M>(m)) {
        case M::Sub: setPlain(id(ParamId::SubSynth), plain(id(ParamId::SubSynth)) > 0.5 ? 0.0 : 50.0); break;
        case M::Match: cycle(ParamId::MatchCharacter, 2); break;
        case M::Inflator: cycle(ParamId::InflCharacter, 4); break;
        case M::Clipper: cycle(ParamId::ClipMode, 3); break;
        case M::Limiter: setPlain(id(ParamId::LimPunch), plain(id(ParamId::LimPunch)) > 0.5 ? 0.0 : 60.0); break;
        case M::Comp: cycle(ParamId::CompCharacter, 4); break;
        default: break;
    }
}

void OmnisEditor::dragMove(int module, double frameX) {
    if (!rack_) return;
    dragFrom_ = module;
    CPoint origin(0, 0);
    chain_->localToFrame(origin);
    const int n = static_cast<int>(shownModules_.size());
    const double local = frameX - origin.x + chainScroll_ - (kIoW + 16.0);
    dropAt_ = std::clamp(static_cast<int>(std::floor((local + kCardGap / 2) / (kCardW + kCardGap) + 0.5)), 0, n); // une place entre deux cartes
    for (int pos = 0; pos < n; ++pos) {
        RackSlot* s = slots_[static_cast<std::size_t>(shownModules_[static_cast<std::size_t>(pos)])];
        if (s) s->setDropMark(pos == dropAt_ ? -1 : (pos == n - 1 && dropAt_ == n ? 1 : 0));
    }
}

void OmnisEditor::drop(int module, double frameX) {
    if (frameX < 0.0 || dragFrom_ != module || dropAt_ < 0) {
        dragFrom_ = dropAt_ = -1;
        layoutRack();
        return;
    }
    // La place visée parmi les cartes ; les modules hors de la chaîne gardent la leur.
    const auto o = order();
    std::vector<int> v(o.begin(), o.end()), shown = shownModules_;
    const int from = static_cast<int>(std::find(shown.begin(), shown.end(), module) - shown.begin());
    int to = dropAt_;
    if (to > from) --to;
    shown.erase(std::remove(shown.begin(), shown.end(), module), shown.end());
    v.erase(std::remove(v.begin(), v.end(), module), v.end());
    if (to < static_cast<int>(shown.size())) v.insert(std::find(v.begin(), v.end(), shown[static_cast<std::size_t>(std::max(0, to))]), module);
    else if (!shown.empty()) v.insert(std::find(v.begin(), v.end(), shown.back()) + 1, module);
    else v.push_back(module);
    std::array<params::Module, params::kModules> next{};
    for (int i = 0; i < params::kModules; ++i) next[static_cast<std::size_t>(i)] = static_cast<params::Module>(v[static_cast<std::size_t>(i)]);
    setPlain(id(ParamId::ChainOrder), params::chainOrderRank(next));
    if (to != from) answer_ = std::string(kModuleTitles[static_cast<std::size_t>(module)]) + " déplacé : la chaîne passe au son sec le temps de se recaler.";
    dragFrom_ = dropAt_ = -1;
    layoutRack();
}

// -----------------------------------------------------------------------------
//  Le tiroir : tous les réglages du module | son écran
// -----------------------------------------------------------------------------
CViewContainer* OmnisEditor::page(CViewContainer* drawer, int index, const char* number, const char* title, const char* help) {
    (void)number;
    const CRect area(0, 0, kDrawerSplit - kWorkL, drawer->getViewSize().getHeight());
    auto* p = new CViewContainer(area);
    p->setTransparency(true);
    // `.dhead` : « COMP · VCA » (h3), puis la phrase du caractère (ou de la page).
    const std::string name = index < params::kModules ? std::string(kModuleTitles[static_cast<std::size_t>(index)]) : std::string(title);
    const std::string pageHelp = help;
    auto* head = new Painted(CRect(18, 8, area.right - 8, 32), [this, index, name, pageHelp](CDrawContext* dc, const CRect& r) {
        const std::string ch = index < params::kModules ? moduleCharacter(index) : std::string();
        const std::string h3 = upperText(i18n::tr(name)) + (ch.empty() ? std::string() : " · " + upperText(i18n::tr(ch)));
        const double w = spacedWidth(dc, h3, mono(16, true), 0.08);
        spaced(dc, h3, CRect(r.left, r.top, r.left + w + 4, r.bottom), color::kText, mono(16, true), 0.08);
        const std::string said = ch.empty() || characterHelp(ch).empty() ? pageHelp : characterHelp(ch);
        text(dc, said, CRect(r.left + w + 18, r.top + 1, r.right - 124, r.bottom), color::kInk2, sans(14)); // la place d'un choix en haut à droite (EQ / DYN)
    });
    p->addView(head);
    live_.push_back(head);
    drawer->addView(p);
    pages_[static_cast<std::size_t>(index)] = p;
    const double sx = kDrawerSplit - kWorkL + 10.0;
    auto* scr = new Screen(CRect(sx + 6, 44, drawer->getViewSize().getWidth() - 18, drawer->getViewSize().getHeight() - 18),
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
            c->setDrawMode(kAntiAliasing | kNonIntegralMode);
            const CRect r = getViewSize();
            c->setLineWidth(1.0);
            c->setFrameColor(color::kLine);
            c->drawRect(CRect(r).inset(0.5, 0.5), kDrawStroked);
            eyebrowMark(c, r.left + 10, r.top + 11);
            spaced(c, upperText(t_), CRect(r.left + 22, r.top + 5, r.right - 6, r.top + 17), color::kText3, mono(9.4), 0.22);
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
        for (const char* prefix : {"Inflate ", "Limiter ", "Comp ", "Clip ", "MB Sub ", "MB Low ", "MB Mid ", "MB High ", "Imager ", "Match ", "Stem ", "Dyn EQ 1 ",
                                   "Dyn EQ 2 ", "Dyn EQ 3 ", "Dyn EQ 4 ", "Dyn EQ "})
            if (label.rfind(prefix, 0) == 0) label = label.substr(std::string(prefix).size());
        for (auto& ch : label) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        const double x = area.left + 4 + i * w;
        auto* k = new LedKnob(CRect(x, area.top + 18, x + w, area.top + 18 + kh), label, model(ids[static_cast<std::size_t>(i)]), sz);
        parent->addView(k);
        live_.push_back(k);
    }
}

void OmnisEditor::buildDrawer(CViewContainer* root) {
    // 03 / RÉGLAGE (.shead), puis le cadre `.detail` et ses crochets.
    root->addView(new Painted(CRect(kWorkL, kSetHeadT, kWorkR, kSetHeadT + 20), [](CDrawContext* dc, const CRect& r) {
        sectionHead(dc, r, "03 /", "Réglage", {});
    }));
    auto* drawer = new Panel(CRect(kWorkL, kDrawerT, kWorkR, kDrawerB), Panel::Look::Detail);
    root->addView(drawer);
    root->addView(new Painted(CRect(kWorkL - 8, kDrawerT - 8, kWorkR + 8, kDrawerB + 8), [](CDrawContext* dc, const CRect& r) {
        brackets(dc, CRect(r).inset(8, 8), color::kAccent);
    }));
    // `.tag` : ALLUMÉ / ÉTEINT, à droite ; un clic allume ou éteint le module ouvert.
    {
        const double w = drawer->getViewSize().getWidth();
        auto* onTag = new Painted(CRect(w - 120, 12, w - 18, 34), [this](CDrawContext* dc, const CRect& r) {
            if (page_ >= params::kModules) return;
            const bool on = norm(id(params::moduleOnId(static_cast<params::Module>(page_)))) >= 0.5;
            tagBox(dc, r, on ? "ALLUMÉ" : "ÉTEINT", on ? color::kAccent : color::kAccentHot);
        }, [this](const CPoint&) {
            if (page_ >= params::kModules) return;
            const auto pid = id(params::moduleOnId(static_cast<params::Module>(page_)));
            setNorm(pid, norm(pid) >= 0.5 ? 0.0 : 1.0);
        });
        drawer->addView(onTag);
        live_.push_back(onTag);
    }
    drawer->addView(new Groove(CRect(kDrawerSplit - kWorkL, 12, kDrawerSplit - kWorkL + 10, drawer->getViewSize().getHeight() - 12)));
    const double W = kDrawerSplit - kWorkL; // largeur des réglages
    const auto seg = [&](CViewContainer* p, const CRect& r, const char* name, ParamId pid) {
        const auto* spec = params::findParameter(pid);
        std::vector<std::string> labels;
        for (const auto& l : spec->choices) labels.emplace_back(l);
        const std::string title = name;
        p->addView(new Painted(CRect(r.left, r.top - 16, r.right, r.top - 3), [title](CDrawContext* dc, const CRect& rr) {
            spaced(dc, upperText(title), rr, color::kText2, mono(9.9), 0.22);
        }));
        auto* s = new SegControl(r, labels, [this, pid] { return static_cast<int>(std::lround(plain(id(pid)) - params::findParameter(pid)->minValue)); },
                                 [this, pid](int i) { setPlain(id(pid), params::findParameter(pid)->minValue + i); });
        s->setChips(true);
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
        auto* pg = page(drawer, 1, "2", "MATCH EQ", "Glisse les nœuds · Maj : fin · 2 clics : 0 dB");
        const CRect inner(0, 0, pg->getViewSize().getWidth(), pg->getViewSize().getHeight());
        // Deux vues : la courbe à phase linéaire (EQ) et l'EQ dynamique (DYN).
        auto* view = new SegControl(CRect(W - 118, 8, W - 14, 30), {"EQ", "DYN"}, [this] { return matchView_; }, [this](int i) { showMatchView(i, dynBand_); });
        pg->addView(view);
        live_.push_back(view);
        auto* st = new CViewContainer(inner);
        st->setTransparency(true);
        pg->addView(st);
        matchStatic_ = st;
        knobs(st, CRect(12, row1, 250, row1 + 104), "COURBE", {id(P::MatchAmount), id(P::MatchTilt)});
        st->addView(new Caption(CRect(270, row1 + 2, W - 14, row1 + 14), "NŒUDS ÉDITÉS", color::kText3, mono(9)));
        auto* side = new SegControl(CRect(270, row1 + 18, W - 14, row1 + 42), {"MID", "SIDE"}, [this] { return matchSide_; }, [this](int i) {
            matchSide_ = i;
            if (pageScreens_[1]) pageScreens_[1]->markDirty();
        });
        st->addView(side);
        live_.push_back(side);
        auto* flat = new HwButton(CRect(270, row1 + 54, 350, row1 + 80), "À PLAT", [] { return false; }, [this] {
            for (int k = 0; k < params::kMatchBands; ++k) setPlain(id(matchSide_ == 0 ? params::matchMidId(k) : params::matchSideId(k)), 0.0);
        }, false);
        st->addView(flat);
        live_.push_back(flat);
        seg(st, CRect(360, row1 + 58, W - 14, row1 + 80), "CARACTÈRE", P::MatchCharacter);
        // La référence visée : pochette, photo (avec son crédit), tempo, tonalité, sonie.
        cardScreen_ = new Screen(CRect(12, row2 - 4, W - 14, row2 + 80), [this](CDrawContext* dc, const CRect& r) { paintReferenceCard(dc, r); }, false);
        cardScreen_->setMouse([this](const CPoint&, int phase, bool) {
            if (!card_ || card_->data.link.empty() || (omnis() && omnis()->promoMode())) return false; // promo : pas de lien
            if (phase == 0) gui::openUrl(card_->data.link);
            return true;
        });
        st->addView(cardScreen_);
        live_.push_back(cardScreen_);
        st->addView(new Caption(CRect(12, row2 + 86, W - 14, row2 + 100), "21 nœuds en Mid et en Side, d'une demi-octave (20 Hz – 20 kHz).", color::kText3, mono(9)));
        st->addView(new Caption(CRect(12, row2 + 102, W - 14, row2 + 116), "La barre IA et les références les écrivent ; ici tu les retouches.", color::kText3, mono(9)));
        // --- Vue DYN : 4 bandes qui n'agissent que quand elles sortent du lot ---
        auto* dy = new CViewContainer(inner);
        dy->setTransparency(true);
        pg->addView(dy);
        matchDynamic_ = dy;
        dy->addView(new Caption(CRect(12, row1 + 2, 250, row1 + 14), "BANDE DYNAMIQUE", color::kText3, mono(9)));
        auto* bands = new SegControl(CRect(12, row1 + 18, 250, row1 + 42), {"1", "2", "3", "4"}, [this] { return dynBand_; }, [this](int i) { showMatchView(1, i); });
        dy->addView(bands);
        live_.push_back(bands);
        knobs(dy, CRect(372, row1, W - 14, row1 + 104), "TEMPS (4 BANDES)", {id(P::MatchDynAttack), id(P::MatchDynRelease)});
        for (int b = 0; b < params::kDynBands; ++b) {
            auto* bv = new CViewContainer(inner);
            bv->setTransparency(true);
            dy->addView(bv);
            dynBandViews_[static_cast<std::size_t>(b)] = bv;
            const auto dyn = [b](params::DynControl c) { return params::dynId(b, c); };
            auto* on = new HwButton(CRect(258, row1 + 18, 364, row1 + 42), "ACTIVE", [this, dyn] { return plain(id(dyn(params::DynControl::On))) >= 0.5; },
                                    [this, dyn] { setPlain(id(dyn(params::DynControl::On)), plain(id(dyn(params::DynControl::On))) >= 0.5 ? 0.0 : 1.0); });
            bv->addView(on);
            live_.push_back(on);
            bv->addView(new Caption(CRect(12, row1 + 52, 364, row1 + 64), "CANAL", color::kText3, mono(9)));
            auto* ch = new SegControl(CRect(12, row1 + 68, 364, row1 + 92), {"STÉRÉO", "MID", "SIDE"},
                                      [this, dyn] { return static_cast<int>(std::lround(plain(id(dyn(params::DynControl::Channel))))); },
                                      [this, dyn](int i) { setPlain(id(dyn(params::DynControl::Channel)), i); });
            bv->addView(ch);
            live_.push_back(ch);
            knobs(bv, CRect(12, row2, 364, row2 + 104), b == 0 ? "BANDE 1" : b == 1 ? "BANDE 2" : b == 2 ? "BANDE 3" : "BANDE 4",
                  {id(dyn(params::DynControl::Freq)), id(dyn(params::DynControl::Q)), id(dyn(params::DynControl::Range)), id(dyn(params::DynControl::Threshold))});
            // Ce que la bande fait en ce moment.
            auto* now = new Screen(CRect(372, row2, W - 14, row2 + 104), [this, b, dyn](CDrawContext* dc, const CRect& r) {
                engraved(dc, "EN CE MOMENT", CRect(r.left + 10, r.top + 6, r.right - 8, r.top + 18));
                const bool active = plain(id(dyn(params::DynControl::On))) >= 0.5;
                const double g = an_->meters.dynamicGainDb[b];
                text(dc, active ? fmt("%+.1f dB", std::fabs(g) < 0.05 ? 0.0 : g) : std::string("—"), CRect(r.left + 8, r.top + 28, r.right - 8, r.top + 58),
                     std::fabs(g) >= 0.05 ? color::kAccent : color::kText2, mono(18, true), kCenterText);
                text(dc, active ? (g < -0.05 ? "RETENUE" : (g > 0.05 ? "RESSORT" : "AU REPOS")) : "BANDE ÉTEINTE", CRect(r.left + 8, r.top + 64, r.right - 8, r.top + 80),
                     color::kText3, mono(9), kCenterText);
                const double range = plain(id(dyn(params::DynControl::Range)));
                const double fill = active && std::fabs(range) > 0.05 ? std::clamp(g / range, 0.0, 1.0) : 0.0;
                const CRect bar(r.left + 12, r.bottom - 14, r.right - 12, r.bottom - 8);
                dc->setFillColor(CColor(0, 0, 0, 255));
                dc->drawRect(bar, kDrawFilled);
                if (fill > 0.005) {
                    dc->setFillColor(color::kAccent);
                    dc->drawRect(CRect(bar.left, bar.top, bar.left + fill * bar.getWidth(), bar.bottom), kDrawFilled);
                }
            }, true);
            now->setMouseEnabled(false);
            bv->addView(now);
            live_.push_back(now);
        }
        dy->addView(new Caption(CRect(12, row2 + 108, W - 14, row2 + 120), "Une bande n'agit que quand elle sort de son équilibre habituel dans le mix (seuil, en dB).", color::kText3, mono(9)));
        dy->addView(new Caption(CRect(12, row2 + 122, W - 14, row2 + 134), "Plage < 0 : retenue · > 0 : ressort. Glisse ses nœuds sur l'écran · double-clic : allumer.", color::kText3, mono(9)));
        showMatchView(0, 0);
    }
    {
        auto* p = page(drawer, 2, "3", "INFLATOR", "Densité et couleur, la 808 et le mix à part.");
        knobs(p, CRect(12, row1, 296, row1 + 104), "DOSE", {id(P::InflLowEffect), id(P::InflHighEffect), id(P::InflSplit)});
        seg(p, CRect(306, row1 + 30, W - 14, row1 + 54), "CARACTÈRE", P::InflCharacter);
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
        auto* p = page(drawer, static_cast<int>(params::Module::Comp), "C", "COMP", "Compresseur de bus : la colle. Le grave ne le fait pas pomper (SC HPF).");
        knobs(p, CRect(12, row1, W - 14, row1 + 104), "COMPRESSION", {id(P::CompThreshold), id(P::CompRatio), id(P::CompAttack), id(P::CompRelease)});
        knobs(p, CRect(12, row2, 296, row2 + 104), "SORTIE · DÉTECTEUR", {id(P::CompMakeup), id(P::CompMix), id(P::CompScHpf)});
        seg(p, CRect(306, row2 + 30, W - 14, row2 + 54), "CARACTÈRE", P::CompCharacter);
    }
    {
        // Composition : chaque élément des bus, son nom dans le DAW et ce qu'OMNIS devine.
        auto* p = page(drawer, kCompPage, "B", "COMPOSITION", "Tes bus et ce qu'OMNIS entend dedans. Rien à mettre sur les pistes.");
        auto* list = new Screen(CRect(12, row1 - 6, W - 14, row1 + 206), [this](CDrawContext* dc, const CRect& r) { paintComposition(dc, r); }, true);
        list->setMouseEnabled(false);
        p->addView(list);
        live_.push_back(list);
        seg(p, CRect(12, row1 + 222, 300, row1 + 246), "RÔLE DE CETTE INSTANCE", P::InstanceRole);
        // Après un changement du mix : tous les bus oublient et réécoutent le morceau.
        auto* again = new HwButton(CRect(316, row1 + 222, W - 14, row1 + 246), "RÉÉCOUTER", [] { return false; }, [this] { answer_ = omnis()->relistenBuses(); }, false);
        p->addView(again);
        live_.push_back(again);
    }
    {
        // Diagnostic du mix : ce qu'OMNIS entend dans la prod, à corriger dans le projet.
        auto* p = page(drawer, kDiagPage, "D", "DIAGNOSTIC DU MIX", "Ce que le master ne rattrape pas : à corriger dans ton projet FL.");
        auto* list = new Screen(CRect(12, row1, W - 14, row1 + 214), [this](CDrawContext* dc, const CRect& r) {
            // Une ligne par constat (le plus important d'abord), retours à la ligne à la main.
            std::vector<std::pair<std::string, CColor>> lines;
            std::size_t start = 0;
            while (start < diagnosis_.size()) {
                std::size_t end = diagnosis_.find('\n', start);
                if (end == std::string::npos) end = diagnosis_.size();
                const std::string item = diagnosis_.substr(start, end - start);
                start = end + 1;
                if (item.empty()) continue;
                const CColor col = item.rfind("[IMPORTANT]", 0) == 0 ? color::kClip : item.rfind("[CONSEIL]", 0) == 0 ? color::kAccent : color::kText2;
                std::string line;
                bool first = true;
                for (std::size_t i = 0; i < item.size();) {
                    const std::size_t sp = item.find(' ', i);
                    const std::string word = item.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
                    if (line.size() + word.size() > 74 && !line.empty()) {
                        lines.emplace_back(line, first ? col : color::kText);
                        line = "  ";
                        first = false;
                    }
                    line += (line.empty() || line == "  " ? "" : " ") + word;
                    if (sp == std::string::npos) break;
                    i = sp + 1;
                }
                if (!line.empty()) lines.emplace_back(line, first ? col : color::kText);
            }
            for (std::size_t i = 0; i < lines.size() && i < 13; ++i)
                text(dc, lines[i].first, CRect(r.left + 10, r.top + 8 + 16 * i, r.right - 10, r.top + 24 + 16 * i), lines[i].second, mono(10));
            if (lines.size() > 13) text(dc, "…", CRect(r.left + 10, r.bottom - 18, r.right - 10, r.bottom - 4), color::kText3, mono(10));
        }, true);
        list->setMouseEnabled(false);
        p->addView(list);
        live_.push_back(list);
        auto* again = new HwButton(CRect(12, row1 + 222, 190, row1 + 248), "RÉÉCOUTER", [] { return false; }, [this] {
            diagnosis_ = omnis()->mixDiagnosis();
        }, false);
        again->setAccentText(true);
        p->addView(again);
        live_.push_back(again);
    }
    // Mesures de l'écran du Match EQ : glisser les nœuds.
    if (pageScreens_[1])
        pageScreens_[1]->setMouse([this](const CPoint& p, int phase, bool fine) {
            const CRect r = pageScreens_[1]->getViewSize();
            return matchView_ == 1 ? dynMouse(p, phase, fine, CRect(0, 0, r.getWidth(), r.getHeight()))
                                   : matchMouse(p, phase, fine, CRect(0, 0, r.getWidth(), r.getHeight()));
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

void OmnisEditor::showMatchView(int view, int band) {
    matchView_ = std::clamp(view, 0, 1);
    dynBand_ = std::clamp(band, 0, params::kDynBands - 1);
    if (matchStatic_) matchStatic_->setVisible(matchView_ == 0);
    if (matchDynamic_) matchDynamic_->setVisible(matchView_ == 1);
    for (int b = 0; b < params::kDynBands; ++b)
        if (auto* v = dynBandViews_[static_cast<std::size_t>(b)]) v->setVisible(b == dynBand_);
    if (pages_[1]) pages_[1]->invalid();
    if (pageScreens_[1]) pageScreens_[1]->markDirty();
}

namespace {
// Écran de l'EQ dynamique : ±18 dB (la plage des bandes va de -18 à +12).
constexpr double kDynTopDb = 18.0, kDynBottomDb = -18.0;

/// Réponse d'une bande dynamique (y = x + (G − 1)·passe-bande), en dB.
double dynBandDb(double f, double hz, double q, double gainDb) {
    const double w = f / hz;
    const std::complex<double> bp = std::complex<double>(0.0, w / q) / std::complex<double>(1.0 - w * w, w / q);
    const double g = std::pow(10.0, gainDb / 20.0);
    return 20.0 * std::log10(std::max(std::abs(1.0 + (g - 1.0) * bp), 1e-6));
}
} // namespace

bool OmnisEditor::dynMouse(const CPoint& p, int phase, bool fine, const CRect& r) {
    const CRect plot(10, 24, r.getWidth() - 10, r.getHeight() - 20);
    const auto yOfDb = [&](double db) { return plot.top + (kDynTopDb - db) / (kDynTopDb - kDynBottomDb) * plot.getHeight(); };
    const auto dyn = [](int b, params::DynControl c) { return id(params::dynId(b, c)); };
    if (phase == 0 || phase == 3) {
        int picked = -1;
        double best = 14.0;
        for (int b = 0; b < params::kDynBands; ++b) {
            const double x = xOfHz(plain(dyn(b, params::DynControl::Freq)), 20.0, 20000.0, plot), y = yOfDb(plain(dyn(b, params::DynControl::Range)));
            const double d = std::hypot(x - p.x, y - p.y);
            if (d < best) {
                best = d;
                picked = b;
            }
        }
        if (picked < 0) return false;
        showMatchView(1, picked);
        if (phase == 3) { // double-clic : allumer / éteindre la bande
            setPlain(dyn(picked, params::DynControl::On), plain(dyn(picked, params::DynControl::On)) >= 0.5 ? 0.0 : 1.0);
            return true;
        }
        dynHeld_ = true;
        dynStartX_ = p.x;
        dynStartY_ = p.y;
        dynStartHz_ = plain(dyn(picked, params::DynControl::Freq));
        dynStartRange_ = plain(dyn(picked, params::DynControl::Range));
        omnis()->beginGesture(dyn(picked, params::DynControl::Freq));
        omnis()->beginGesture(dyn(picked, params::DynControl::Range));
        return true;
    }
    if (!dynHeld_) return false;
    const double scale = fine ? 0.2 : 1.0;
    // x : la fréquence (en octaves), y : la plage.
    const double octaves = (p.x - dynStartX_) / plot.getWidth() * std::log2(20000.0 / 20.0) * scale;
    const double hz = std::clamp(dynStartHz_ * std::pow(2.0, octaves), 20.0, 20000.0);
    const double range = std::clamp(dynStartRange_ + (dynStartY_ - p.y) * (kDynTopDb - kDynBottomDb) / plot.getHeight() * scale, -18.0, 12.0);
    const auto f = dyn(dynBand_, params::DynControl::Freq), g = dyn(dynBand_, params::DynControl::Range);
    omnis()->performGesture(f, params::toNormalized(*params::findParameter(f), hz));
    omnis()->performGesture(g, params::toNormalized(*params::findParameter(g), range));
    if (phase == 2) {
        omnis()->endGesture(f);
        omnis()->endGesture(g);
        dynHeld_ = false;
    }
    return true;
}

CColor OmnisEditor::familyColor(int family) {
    switch (static_cast<stems::Family>(family)) {
        case stems::Family::Drums: return color::kAccent;
        case stems::Family::Bass: return color::kAccentHot;
        case stems::Family::Instrument: return color::kCool;
        case stems::Family::Vocal: return color::kText;
        case stems::Family::Fx: return CColor(190, 130, 255);
        case stems::Family::Unknown:
        default: return color::kText3;
    }
}

void OmnisEditor::paintComposition(CDrawContext* dc, const CRect& r) {
    const auto& comp = omnis()->composition();
    struct Row {
        std::string left, right;
        CColor leftColor, rightColor;
        bool header = false;
    };
    std::vector<Row> rows;
    for (const auto& b : comp.buses) {
        Row h;
        h.left = i18n::tr("BUS ") + "« " + (b.name.empty() ? i18n::tr("sans nom") : b.name) + " »";
        h.right = i18n::tr(stems::familyLabel(b.family));
        h.leftColor = color::kText;
        h.rightColor = familyColor(static_cast<int>(b.family));
        h.header = true;
        rows.push_back(std::move(h));
        if (b.listening()) {
            rows.push_back({"  " + i18n::tr("lance la lecture…"), {}, color::kText3, color::kText3});
            continue;
        }
        if (const std::string l = stems::describeListened(b); !l.empty()) rows.push_back({"  " + l, {}, color::kText3, color::kText3});
        if (b.content.items.empty()) rows.push_back({"  " + i18n::tr("rien de net pour l'instant"), {}, color::kText3, color::kText3});
        // Ce qui est entendu : l'élément à gauche (couleur de son groupe), le détail à droite.
        for (const auto& item : b.content.items) {
            Row row;
            row.left = "  " + stems::describeItem(item);
            row.leftColor = familyColor(static_cast<int>(stems::familyOf(item.element)));
            row.right = fmt("%.0f %%", 100.0 * item.confidence);
            row.rightColor = color::kText3;
            rows.push_back(std::move(row));
        }
    }
    if (rows.empty()) {
        text(dc, i18n::tr("Aucun bus OMNIS pour l'instant."), CRect(r.left + 10, r.top + 10, r.right - 10, r.top + 26), color::kText2, mono(10));
        text(dc, i18n::tr("Pose OMNIS sur tes bus (DRUMS, MÉLO, EFX…), RÔLE : Bus,"), CRect(r.left + 10, r.top + 34, r.right - 10, r.top + 50), color::kText3, mono(10));
        text(dc, i18n::tr("puis lance la lecture. Rien à mettre sur les pistes."), CRect(r.left + 10, r.top + 50, r.right - 10, r.top + 66), color::kText3, mono(10));
        return;
    }
    const double mid = r.left + 0.62 * r.getWidth();
    constexpr double kRow = 13.0;
    const int maxRows = static_cast<int>((r.getHeight() - 8) / kRow);
    for (int i = 0; i < static_cast<int>(rows.size()) && i < maxRows; ++i) {
        const auto& row = rows[static_cast<std::size_t>(i)];
        const double y = r.top + 4 + kRow * i;
        if (i == maxRows - 1 && static_cast<int>(rows.size()) > maxRows) {
            text(dc, fmt(i18n::tr("… +%.0f : écris « composition » à l'IA").c_str(), static_cast<double>(rows.size() - static_cast<std::size_t>(i))),
                 CRect(r.left + 10, y, r.right - 10, y + kRow), color::kText3, mono(9));
            break;
        }
        if (row.header && i > 0) {
            dc->setFrameColor(color::kLine);
            dc->drawLine(CPoint(r.left + 8, y - 1.5), CPoint(r.right - 8, y - 1.5));
        }
        text(dc, row.left, CRect(r.left + 10, y, mid - 6, y + kRow), row.leftColor, mono(row.header ? 9.5 : 9, row.header));
        text(dc, row.right, CRect(mid + 4, y, r.right - 10, y + kRow), row.rightColor, mono(row.header ? 9.5 : 9, row.header), row.header ? kLeftText : kRightText);
    }
}

void OmnisEditor::selectPage(int p) {
    page_ = std::clamp(p, 0, static_cast<int>(kDiagPage));
    if (page_ < params::kModules) {
        lastModule_ = page_; // MASTER y revient après COMPOSITION
        scrollChainTo(page_);
    }
    if (page_ == kDiagPage) diagnosis_ = omnis()->mixDiagnosis(); // écouté maintenant
    for (int i = 0; i <= kDiagPage; ++i) {
        if (pages_[static_cast<std::size_t>(i)]) pages_[static_cast<std::size_t>(i)]->setVisible(i == page_);
        if (pageScreens_[static_cast<std::size_t>(i)]) pageScreens_[static_cast<std::size_t>(i)]->setVisible(i == page_);
    }
    // Delta suit le module ouvert.
    if (page_ < params::kModules && plain(id(ParamId::DeltaListen)) >= 0.5 && std::lround(plain(id(ParamId::DeltaListen))) != page_ + 1)
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
        case 1: { // Match EQ : nœuds éditables (vue EQ) ou bandes dynamiques (vue DYN)
            if (matchView_ == 1) {
                const auto dyn = [](int b, params::DynControl ctl) { return id(params::dynId(b, ctl)); };
                caption("EQ DYNAMIQUE · BANDE " + std::to_string(dynBand_ + 1) + " · " +
                        valueText(dyn(dynBand_, params::DynControl::Channel)));
                const double lo = 20.0, hi = 20000.0;
                gridLines(c, plot, lo, hi, true);
                zeroLine();
                for (int db : {-12, -6, 6, 12}) {
                    const double y = plot.top + (kDynTopDb - db) / (kDynTopDb - kDynBottomDb) * plot.getHeight();
                    c->setFrameColor(color::kGrid);
                    c->drawLine(CPoint(plot.left, y), CPoint(plot.right, y));
                    text(c, fmt("%+.0f", db), CRect(plot.left + 2, y - 11, plot.left + 30, y - 1), color::kText3, mono(8));
                }
                for (int b = 0; b < params::kDynBands; ++b) {
                    const bool on = plain(dyn(b, params::DynControl::On)) >= 0.5, sel = b == dynBand_;
                    const double hz = plain(dyn(b, params::DynControl::Freq)), q = plain(dyn(b, params::DynControl::Q));
                    const double range = plain(dyn(b, params::DynControl::Range)), now = an_->meters.dynamicGainDb[b];
                    // La plage (jusqu'où la bande peut aller), puis ce qu'elle fait maintenant.
                    if (on || sel)
                        curve(lo, hi, kDynTopDb, [&](double f) { return dynBandDb(f, hz, q, range); },
                              withAlpha(sel ? color::kAccent : color::kCool, on ? 0.45 : 0.2), false);
                    if (on && std::fabs(now) > 0.05) curve(lo, hi, kDynTopDb, [&](double f) { return dynBandDb(f, hz, q, now); }, color::kAccent, true);
                    const double x = xOfHz(hz, lo, hi, plot), y = plot.top + (kDynTopDb - range) / (kDynTopDb - kDynBottomDb) * plot.getHeight();
                    fillCircle(c, CPoint(x, y), sel ? 7.0 : 6.0, on ? (sel ? color::kAccent : color::kCool) : CColor(90, 90, 94));
                    text(c, std::to_string(b + 1), CRect(x - 6, y - 6, x + 6, y + 6), CColor(10, 10, 12), mono(8.5, true), kCenterText);
                }
                hint(hzText(plain(dyn(dynBand_, params::DynControl::Freq))) + " Hz · PLAGE " + valueText(dyn(dynBand_, params::DynControl::Range)) + " · SEUIL +" +
                     valueText(dyn(dynBand_, params::DynControl::Threshold)) + " · MAINTENANT " + fmt("%+.1f dB", an_->meters.dynamicGainDb[dynBand_]));
                break;
            }
            caption(std::string("COURBE M/S · ÉDITION ") + (matchSide_ == 0 ? "MID" : "SIDE") + " · TILT " + fmt("%+.1f dB", plain(id(ParamId::MatchTilt))) +
                    (plain(id(ParamId::MatchCharacter)) >= 0.5 ? " · ANALOGIQUE" : ""));
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
                // Analogique : la courbe entendue est lissée sur ±1/3 d'octave (cloches plus larges).
                const bool analog = plain(id(ParamId::MatchCharacter)) >= 0.5;
                const auto heard = [&](double f) {
                    if (!analog) return at(f);
                    double sum = 0.0;
                    for (int k = -4; k <= 4; ++k) sum += at(f * std::exp2(k / 12.0));
                    return sum / 9.0;
                };
                curve(lo, hi, 12.0, heard, edited ? color::kAccent : withAlpha(color::kCool, 0.55), edited);
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
            caption(i18n::tr(valueText(id(ParamId::InflCharacter))) + " · 808 " + fmt("%.0f %%", plain(id(ParamId::InflLowEffect))) + " · MIX " +
                    fmt("%.0f %%", plain(id(ParamId::InflHighEffect))));
            const int character = static_cast<int>(std::lround(plain(id(ParamId::InflCharacter))));
            const double effect = std::max(plain(id(ParamId::InflLowEffect)), plain(id(ParamId::InflHighEffect))) / 100.0;
            const double drive = std::pow(10.0, plain(id(ParamId::InflDrive)) / 20.0), m = InflatorCurve::exponentFor(plain(id(ParamId::InflCurve)));
            const double out = std::pow(10.0, plain(id(ParamId::InflOutput)) / 20.0);
            transfer([&](double x) { return out * (x + effect * (InflatorCurve::shape(character, drive * x, m) / drive - x)); }, 1.2);
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
        case static_cast<int>(params::Module::Comp): { // Compresseur : courbe statique + réduction dans le temps
            const bool on = plain(id(ParamId::CompOn)) >= 0.5;
            caption(i18n::tr(valueText(id(ParamId::CompCharacter))) + " · RÉDUCTION " + fmt("%.1f dB", on ? an_->meters.compGrDb : 0.0f) +
                    (on ? "" : " · MODULE ÉTEINT"));
            history(an_->compHistory, 12.0, color::kAccent);
            hint("SEUIL " + valueText(id(ParamId::CompThreshold)) + " · " + valueText(id(ParamId::CompRatio)) + " · MIX " + valueText(id(ParamId::CompMix)));
            break;
        }
        case 6: { // Limiteur : réduction de gain dans le temps
            caption("RÉDUCTION DE GAIN · " + fmt("%.1f dB", an_->meters.limiterGrDb) + " · PLAFOND " + valueText(id(ParamId::LimCeiling)));
            history(an_->limHistory, 12.0, color::kAccent);
            hint("PUNCH " + valueText(id(ParamId::LimPunch)) + " · UPWARD " + valueText(id(ParamId::LimUpward)));
            break;
        }
        case kCompPage: {
            caption("OMNIS SUR TES BUS");
            const bool bus = omnis()->instanceRole() == params::InstanceRole::Bus;
            text(c, i18n::tr(bus ? "CETTE INSTANCE : BUS" : "CETTE INSTANCE : MASTER") + (omnis()->trackName().empty() ? "" : " « " + omnis()->trackName() + " »"),
                 CRect(plot.left, plot.top + 4, plot.right, plot.top + 20), color::kAccent, mono(10, true));
            const char* lines[] = {"1. Sur chaque bus (DRUMS, 808, MÉLO, EFX…) :", "   OMNIS, RÔLE : Bus.", "2. Joue le morceau du début à la fin :",
                                   "   chaque bus l'écoute en entier.", "Rien à mettre sur les pistes : le son des", "bus ne change pas, le mixage non plus."};
            for (int i = 0; i < 6; ++i)
                text(c, i18n::tr(lines[i]), CRect(plot.left, plot.top + 30 + 15 * i, plot.right, plot.top + 44 + 15 * i), i >= 4 ? color::kText3 : color::kText2, mono(9.5));
            // Les groupes et leur couleur.
            double x = plot.left;
            for (int f = 1; f < stems::kFamilyCount; ++f) {
                const auto fam = static_cast<stems::Family>(f);
                const std::string label = i18n::tr(stems::familyLabel(fam));
                c->setFillColor(familyColor(f));
                c->drawRect(CRect(x, plot.bottom - 12, x + 8, plot.bottom - 4), kDrawFilled);
                text(c, label, CRect(x + 11, plot.bottom - 16, x + 90, plot.bottom), color::kText2, mono(8.5));
                x += 14.0 + 6.2 * static_cast<double>(label.size());
            }
            break;
        }
        case kDiagPage: {
            caption("CE QU'OMNIS ENTEND DANS TA PROD (AVANT SES MODULES)");
            text(c, "Comparé à ta référence choisie, sinon", CRect(plot.left, plot.top + 10, plot.right, plot.top + 26), color::kText2, mono(10));
            text(c, "à la moyenne des sons mesurés.", CRect(plot.left, plot.top + 28, plot.right, plot.top + 44), color::kText2, mono(10));
            text(c, "ROUGE : le master ne le rattrapera pas.", CRect(plot.left, plot.top + 62, plot.right, plot.top + 78), color::kClip, mono(10));
            text(c, "ORANGE : conseil de mix.", CRect(plot.left, plot.top + 80, plot.right, plot.top + 96), color::kAccent, mono(10));
            text(c, "GRIS : remarque (parfois voulu).", CRect(plot.left, plot.top + 98, plot.right, plot.top + 114), color::kText2, mono(10));
            text(c, "Écris aussi « diagnostic » dans la barre IA.", CRect(plot.left, plot.top + 132, plot.right, plot.top + 148), color::kText3, mono(10));
            break;
        }
        default: break;
    }
}

// -----------------------------------------------------------------------------
//  Écrans du bas : analyseur et goniomètre
// -----------------------------------------------------------------------------
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
    // `.right` : 18 px de marge ; le niveau, les barres, les mesures, puis l'écoute et la sortie.
    const double L = kMasterL + 18, R = kMasterR - 18;
    auto* col = new Panel(CRect(L, kStyleT, R, kHeight - 16));
    root->addView(col);
    const double W = col->getViewSize().getWidth();
    // NIVEAU : `.level` (le choix : liseré ambre, fond ambre à 12 %).
    auto levelHits = std::make_shared<std::vector<std::pair<CRect, int>>>();
    auto* level = new Painted(CRect(0, 0, W, 210), [this, levelHits](CDrawContext* dc, const CRect& r) {
        eyebrow(dc, "niveau", r.left, r.top);
        const auto* spec = params::findParameter(id(ParamId::LoudnessLevel));
        const int current = static_cast<int>(std::lround(plain(id(ParamId::LoudnessLevel))));
        levelHits->clear();
        static const std::array<int, 5> kRows{1, 2, 3, 4, 0};
        for (std::size_t k = 0; k < kRows.size(); ++k) {
            const int i = kRows[k];
            const CRect row(r.left, r.top + 24 + k * 38.0, r.right, r.top + 24 + k * 38.0 + 32);
            const bool on = i == current;
            dc->setFillColor(on ? color::kAccent15 : CColor(0, 0, 0, 0));
            dc->drawRect(row, kDrawFilled);
            dc->setLineWidth(1.0);
            dc->setFrameColor(on ? color::kAccent : color::kLine);
            dc->drawRect(CRect(row).inset(0.5, 0.5), kDrawStroked);
            std::string label = spec ? std::string(spec->choices[static_cast<std::size_t>(i)]) : std::string(), value = "AUTO";
            if (const auto sp = label.find_last_of(' '); i > 0 && sp != std::string::npos) {
                value = label.substr(sp + 1) + " LUFS";
                label = label.substr(0, sp);
            } else if (i == 0) {
                label = "Selon le style";
            }
            spaced(dc, upperText(i18n::tr(label)), CRect(row.left + 11.2, row.top, row.right - 80, row.bottom), color::kChip, mono(12.2), 0.08);
            spaced(dc, upperText(value), CRect(row.right - 100, row.top, row.right - 11.2, row.bottom), on ? color::kAccent : color::kText2, mono(11.2), 0.08, kRightText);
            levelHits->push_back({row, i});
        }
    }, [this, levelHits](const CPoint& p) {
        for (const auto& [row, i] : *levelHits)
            if (row.pointInside(p)) {
                setPlain(id(ParamId::LoudnessLevel), i);
                const auto* spec = params::findParameter(id(ParamId::LoudnessLevel));
                answer_ = i == 0 ? "NIVEAU : celui du style ou de la référence choisie."
                                 : "NIVEAU : " + i18n::tr(std::string(spec->choices[static_cast<std::size_t>(i)])) + " LUFS ; le volume y est mené pendant la lecture.";
                return;
            }
    });
    col->addView(level);
    live_.push_back(level);
    // `.bars` : L, R, GR.
    meter_ = new Screen(CRect(0, 226, W, 378), [this](CDrawContext* c, const CRect& r) { paintMeter(c, r); }, true);
    meter_->setMouseEnabled(false);
    col->addView(meter_);
    live_.push_back(meter_);
    // `.dots` : les mesures, avec leurs points de suite.
    readouts_ = new Screen(CRect(0, 394, W, 552), [this](CDrawContext* c, const CRect& r) { paintReadouts(c, r); }, true);
    readouts_->setMouseEnabled(false);
    col->addView(readouts_);
    live_.push_back(readouts_);
    // ÉCOUTE : stéréo / mid / side, MATCH (même volume que l'original), DELTA (ce que change le module).
    col->addView(new Painted(CRect(0, 568, W, 584), [](CDrawContext* dc, const CRect& r) { eyebrow(dc, "écoute", r.left, r.top); }));
    {
        auto* mon = new SegControl(CRect(0, 590, W, 626), {"STÉRÉO", "MID", "SIDE"}, [this] { return static_cast<int>(std::lround(plain(id(ParamId::Monitor)))); },
                                   [this](int i) { setPlain(id(ParamId::Monitor), i); });
        col->addView(mon);
        live_.push_back(mon);
        matchButton_ = new HwButton(CRect(0, 634, W / 2 - 4, 666), "MATCH", [this] { return plain(id(ParamId::GainMatch)) >= 0.5; },
                                    [this] { setPlain(id(ParamId::GainMatch), plain(id(ParamId::GainMatch)) >= 0.5 ? 0.0 : 1.0); }, false);
        matchButton_->setBlink(true);
        col->addView(matchButton_);
        live_.push_back(matchButton_);
        auto* delta = new HwButton(CRect(W / 2 + 4, 634, W, 666), "DELTA", [this] { return plain(id(ParamId::DeltaListen)) >= 0.5; }, [this] {
            const bool on = plain(id(ParamId::DeltaListen)) >= 0.5;
            setPlain(id(ParamId::DeltaListen), on || page_ >= params::kModules ? 0.0 : static_cast<double>(page_ + 1));
        }, false);
        col->addView(delta);
        live_.push_back(delta);
        // Écoute simulée (écouteurs, téléphone, voiture…) : en orange tant qu'elle est active.
        const auto pid = ParamId::ListenSim;
        auto* m = new COptionMenu(CRect(0, 674, W, 702), listen([this, pid](CControl* ctl) {
            const auto* spec = params::findParameter(id(pid));
            setPlain(id(pid), spec->minValue + std::lround(ctl->getValue()));
            const int v = static_cast<int>(std::lround(ctl->getValue()));
            answer_ = v == 0 ? "Écoute normale."
                             : "ÉCOUTE SIMULÉE « " + std::string(spec->choices[static_cast<std::size_t>(v)]) +
                                   " » : pour juger seulement (approximation d'OMNIS, pas une mesure du modèle). "
                                   "Rien n'est exporté avec, et elle repart à Off à la réouverture du projet.";
        }), static_cast<int32_t>(pid));
        m->setFont(mono(11.5));
        m->setFontColor(color::kChip);
        m->setBackColor(CColor(0, 0, 0));
        m->setFrameColor(color::kLine2);
        const auto* spec = params::findParameter(id(pid));
        for (const auto& l : spec->choices) m->addEntry(UTF8String(i18n::tr(l)));
        m->setCurrent(static_cast<int32_t>(std::lround(plain(id(pid)) - spec->minValue)));
        m->setTooltipText(i18n::tr("Écoute simulée : écouteurs, téléphone, TikTok, voiture…").c_str());
        col->addView(m);
        syncs_.push_back([this, m, pid, spec] {
            const auto v = static_cast<int32_t>(std::lround(plain(id(pid)) - spec->minValue));
            if (m->getCurrentIndex() != v) m->setCurrent(v);
            const CColor want = v != 0 ? color::kAccentHot : color::kChip;
            if (m->getFontColor() != want) {
                m->setFontColor(want);
                m->setFrameColor(v != 0 ? color::kAccentHot : color::kLine2);
                m->invalid();
            }
        });
    }
    // SORTIE : le gain d'entrée et de sortie, le diagnostic, l'export.
    col->addView(new Painted(CRect(0, 718, W, 734), [](CDrawContext* dc, const CRect& r) { eyebrow(dc, "gain", r.left, r.top); }));
    const double kh = LedKnob::heightFor(LedKnob::Size::XSmall);
    auto* in = new LedKnob(CRect(0, 742, W / 2 - 4, 742 + kh), "ENTRÉE", model(id(ParamId::InputGain)), LedKnob::Size::XSmall);
    auto* out = new LedKnob(CRect(W / 2 + 4, 742, W, 742 + kh), "SORTIE", model(id(ParamId::OutputGain)), LedKnob::Size::XSmall);
    col->addView(in);
    col->addView(out);
    live_.push_back(in);
    live_.push_back(out);
    auto* diag = new HwButton(CRect(0, 830, W, 862), "Diagnostic du mix", [this] { return page_ == kDiagPage; }, [this] { selectPage(kDiagPage); }, false);
    col->addView(diag);
    live_.push_back(diag);
    auto* exportCheck = new HwButton(CRect(0, 870, W, 902), "Vérifier l'export", [] { return false; }, [this] { answer_ = omnis()->ask("export"); }, false);
    col->addView(exportCheck);
    live_.push_back(exportCheck);
}

void OmnisEditor::paintMeter(CDrawContext* c, const CRect& r) {
    // `.bars` : trois colonnes (L, R, GR) dans un cadre #050505 ; rayures ambre de 3 px tous les 5 px ;
    // la réduction descend du haut, en orange.
    c->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const auto& m = an_->meters;
    c->setFillColor(CColor(0, 0, 0));
    c->drawRect(r, kDrawFilled);
    const CRect box(r.left, r.top, r.right, r.bottom - 20);
    c->setFillColor(CColor(5, 5, 5));
    c->drawRect(box, kDrawFilled);
    c->setLineWidth(1.0);
    c->setFrameColor(color::kLine);
    c->drawRect(CRect(box).inset(0.5, 0.5), kDrawStroked);
    const CRect in = CRect(box).inset(8, 8);
    const double gap = 8.0, w = (in.getWidth() - 2 * gap) / 3.0;
    const auto toLevel = [](double db) { return std::clamp((db + 48.0) / 48.0, 0.0, 1.0); };
    const double gr = std::min(0.0, static_cast<double>(m.limiterGrDb) + m.clipperGrDb);
    const std::array<double, 3> levels{toLevel(m.outPeakDb[0]), toLevel(m.outPeakDb[1]), std::clamp(-gr / 12.0, 0.0, 1.0)};
    for (int i = 0; i < 3; ++i) {
        const CRect col(in.left + i * (w + gap), in.top, in.left + i * (w + gap) + w, in.bottom);
        c->setFillColor(CColor(255, 255, 255, 13));
        c->drawRect(col, kDrawFilled);
        const double h = levels[static_cast<std::size_t>(i)] * col.getHeight();
        const bool down = i == 2;
        c->setFillColor(down ? color::kAccentHot : color::kAccent);
        for (double y = 0; y < h; y += 5.0) {
            const double top = down ? col.top + y : col.bottom - y - 3.0;
            c->drawRect(CRect(col.left, std::max(col.top, top), col.right, std::min(col.bottom, top + 3.0)), kDrawFilled);
        }
        static const std::array<const char*, 3> kNames{"L", "R", "GR"};
        spaced(c, kNames[static_cast<std::size_t>(i)], CRect(col.left, box.bottom + 6, col.right, box.bottom + 18), color::kText2, mono(9.5), 0.14, kCenterText);
    }
}

void OmnisEditor::paintReadouts(CDrawContext* c, const CRect& r) {
    // `.dots` : le nom (gris), des points de suite, la valeur (blanc, chiffres alignés).
    c->setDrawMode(kAntiAliasing | kNonIntegralMode);
    c->setFillColor(CColor(0, 0, 0));
    c->drawRect(r, kDrawFilled);
    const auto& m = an_->meters;
    const double tp = plain(id(ParamId::MeterTruePeak)), st = plain(id(ParamId::MeterShortTerm)), il = plain(id(ParamId::MeterIntegrated));
    const double corr = plain(id(ParamId::MeterCorrelation)), lra = plain(id(ParamId::MeterLoudnessRange));
    const bool hasSignal = il > -70.0;
    const auto v1 = [](double v, double floor) { return v <= floor ? std::string("–") : fmt("%.1f", v); };
    struct Row {
        const char* name;
        std::string value;
        bool hot;
    };
    const std::array<Row, 7> rows{{
        {"lufs court", v1(st, -79.0), false},
        {"lufs intégré", v1(il, -79.0), false},
        {"true peak", v1(tp, -79.0), tp > -1.0 && tp > -79.0},
        {"relief (lra)", hasSignal ? fmt("%.1f", lra) : "–", false},
        {"plr", hasSignal ? fmt("%.1f", tp - il) : "–", hasSignal && tp - il < 6.0},
        {"corrélation", hasSignal ? fmt("%.2f", corr) : "–", hasSignal && corr < -0.15},
        {"crête max", v1(m.samplePeakMaxDb, -119.0), m.samplePeakMaxDb > -0.1},
    }};
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const CRect row(r.left, r.top + i * 22.0, r.right, r.top + i * 22.0 + 18);
        const std::string name = upperText(i18n::tr(rows[i].name));
        const double nw = spacedWidth(c, name, mono(11.5), 0.08);
        spaced(c, name, CRect(row.left, row.top + 2, row.left + nw + 2, row.bottom), color::kText2, mono(11.5), 0.08);
        c->setFont(mono(14.4));
        const double vw = c->getStringWidth(UTF8String(rows[i].value));
        text(c, rows[i].value, CRect(row.right - vw - 2, row.top, row.right, row.bottom), rows[i].hot ? color::kAccentHot : color::kText, mono(14.4), kRightText);
        c->setFillColor(color::kLine2);
        for (double x = row.left + nw + 8; x < row.right - vw - 8; x += 3.0) c->drawRect(CRect(x, row.bottom - 5, x + 1, row.bottom - 4), kDrawFilled);
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
    an_->compHistory[static_cast<std::size_t>(an_->historyPos)] = plain(id(ParamId::CompOn)) >= 0.5 ? an_->meters.compGrDb : 0.0f;
    an_->historyPos = (an_->historyPos + 1) % Analysis::kHistory;
}

void OmnisEditor::tick() {
    pumpFeed();
    const std::string news = omnis()->pollAutomation();
    if (!news.empty()) answer_ = news;
    if (omnis()->takeLibraryChanged() && refFill_) refFill_(); // un fichier ajouté : le menu RÉF le montre
    if (omnis()->takeTutorialRequest()) startTutorial(0);      // « tuto » écrit dans la barre IA
    if (const auto status = omnis()->autoStatus(); !status.empty()) answer_ = status;
    ++ticks_;
    const int o = static_cast<int>(std::lround(plain(id(ParamId::ChainOrder))));
    int mask = omnis() ? std::max(0, omnis()->chainHidden()) : 0;
    for (int m = 0; m < params::kModules; ++m)
        if (norm(id(params::moduleOnId(static_cast<params::Module>(m)))) >= 0.5) mask |= 1 << (m + 8);
    if ((o != lastOrder_ || mask != lastMask_) && dragFrom_ < 0) {
        lastOrder_ = o;
        layoutRack();
    }
    for (int m = 0; m < params::kModules; ++m) // `.card.off` : une carte éteinte reste, grisée
        if (auto* sl = slots_[static_cast<std::size_t>(m)]) {
            const float a = norm(id(params::moduleOnId(static_cast<params::Module>(m)))) >= 0.5 ? 1.0f : 0.4f;
            if (sl->getAlphaValue() != a) sl->setAlphaValue(a);
        }
    if (chain_) chain_->invalid(); // le flux avance
    for (auto& sync : syncs_) sync();
    for (auto* l : live_) l->tick();
}

} // namespace omnis::vst3
