// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/gui/Widgets.h — Les pièces de la console, dessinées en code :
//    - LedKnob : bouton rotatif à couronne de LED (allumées du repos à la
//      valeur), valeur affichée dessous ; glisser verticalement (Maj : fin),
//      molette, double-clic : valeur par défaut ;
//    - HwButton : bouton matériel, avec voyant ou non ;
//    - SegControl : sélecteur à segments (Stéréo / Mid / Side…) ;
//    - Screen : un écran noir dessiné par une fonction (courbes, mesures),
//      qui peut recevoir la souris ;
//    - RackSlot : un module du rack au format 500 (numéro, nom, voyant, gros
//      bouton, état), qu'on sélectionne d'un clic et qu'on déplace en le
//      glissant par son en-tête.
//  Chaque pièce lit son état par des fonctions (Model) : l'éditeur les relie
//  aux paramètres du contrôleur, et tick() ne redessine que ce qui a changé.
// =============================================================================
#pragma once

#include "Look.h"

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/events.h"

#include <functional>
#include <string>
#include <vector>

namespace omnis::vst3::gui {

/// Ce qui se redessine tout seul quand son état change (appelé par la minuterie).
class Live {
public:
    virtual ~Live() = default;
    virtual void tick() = 0;
};

/// Une valeur normalisée [0, 1] vue par une commande.
struct Model {
    std::function<double()> get;
    std::function<void(double)> set;
    std::function<void()> begin, end;
    std::function<std::string()> text; ///< valeur affichée (unités comprises)
    double origin = 0.0;               ///< d'où les LED s'allument (0 : butée gauche ; 0,5 : bipolaire)
    double def = 0.0;                  ///< valeur par défaut (double-clic)
    int steps = 0;                     ///< > 0 : valeurs discrètes
};

class LedKnob : public VSTGUI::CView, public Live {
public:
    enum class Size { XSmall, Small, Mid, Big };
    LedKnob(const CRect& r, std::string label, Model model, Size size = Size::Small);

    void draw(CDrawContext* c) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent& e) override;
    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& e) override;
    void tick() override;

    [[nodiscard]] static CCoord heightFor(Size size);
    [[nodiscard]] const std::string& label() const noexcept { return label_; }

private:
    [[nodiscard]] CCoord diameter() const;
    void setNorm(double v);

    std::string label_;
    Model model_;
    Size size_;
    bool dragging_ = false;
    CCoord startY_ = 0.0;
    double startValue_ = 0.0, shownValue_ = -1.0;
    std::string shownText_;
};

class HwButton : public VSTGUI::CView, public Live {
public:
    HwButton(const CRect& r, std::string label, std::function<bool()> isOn, std::function<void()> onClick, bool withLed = true);
    void draw(CDrawContext* c) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void tick() override;
    /// Clignote tant que allumé (Gain Match : ce n'est pas le vrai niveau).
    void setBlink(bool blink) { blink_ = blink; }
    void setLabel(std::string label) {
        label_ = std::move(label);
        invalid();
    }
    void setAccentText(bool accent) { accentText_ = accent; }

private:
    std::string label_;
    std::function<bool()> isOn_;
    std::function<void()> onClick_;
    bool withLed_, blink_ = false, accentText_ = false;
    int shownState_ = -1, phase_ = 0;
};

class SegControl : public VSTGUI::CView, public Live {
public:
    SegControl(const CRect& r, std::vector<std::string> labels, std::function<int()> get, std::function<void(int)> set);
    void draw(CDrawContext* c) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void tick() override;

private:
    std::vector<std::string> labels_;
    std::function<int()> get_;
    std::function<void(int)> set_;
    int shown_ = -2;
};

class Screen : public VSTGUI::CView, public Live {
public:
    using Painter = std::function<void(CDrawContext*, const CRect&)>;
    /// phase : 0 appui, 1 glissé, 2 relâché, 3 double-clic. true : l'écran prend la souris.
    using Mouse = std::function<bool(const CPoint& local, int phase, bool fine)>;
    Screen(const CRect& r, Painter painter, bool alwaysRedraw = false);
    void draw(CDrawContext* c) override;
    void setMouse(Mouse m) { mouse_ = std::move(m); }
    /// Redessiner à la prochaine minuterie (mesures qui bougent).
    void markDirty() { dirty_ = true; }
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
    void tick() override;

private:
    Painter painter_;
    Mouse mouse_;
    bool always_, dirty_ = true, tracking_ = false;
};

/// Un module du rack.
class RackSlot : public VSTGUI::CViewContainer, public Live {
public:
    struct Callbacks {
        std::function<bool()> isOn;
        std::function<void()> toggle;
        std::function<bool()> isSelected;
        std::function<void()> select;
        std::function<std::string()> status;
        std::function<double()> level; ///< 0 … 1 : remplissage de la barre d'état
        std::function<void(CCoord frameX)> dragMove;
        std::function<void(CCoord frameX)> drop;
    };
    RackSlot(const CRect& r, std::string number, std::string title, Callbacks cb);
    void drawBackgroundRect(CDrawContext* c, const CRect& updateRect) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
    void tick() override;
    void setNumber(std::string n) {
        number_ = std::move(n);
        invalid();
    }
    void setDimmed(bool d) {
        if (d != dimmed_) {
            dimmed_ = d;
            invalid();
        }
    }
    void setDropMark(int side) {
        if (side != dropMark_) {
            dropMark_ = side;
            invalid();
        }
    }
    [[nodiscard]] CRect ledRect() const;

private:
    std::string number_, title_;
    Callbacks cb_;
    bool dragging_ = false, pressed_ = false, dimmed_ = false;
    int dropMark_ = 0; ///< −1 : marque à gauche, +1 : à droite
    CCoord pressX_ = 0.0;
    std::string shownStatus_;
    int shownFlags_ = -1;
    double shownLevel_ = -1.0;
};

} // namespace omnis::vst3::gui
