// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/gui/Widgets.cpp
// =============================================================================
#include "Widgets.h"

#include <numbers>

using namespace VSTGUI;

namespace omnis::vst3::gui {

namespace {

constexpr double kSweepDeg = 270.0, kStartDeg = -135.0;

CPoint polar(const CPoint& c, double radius, double degrees) {
    const double a = degrees * std::numbers::pi / 180.0;
    return CPoint(c.x + radius * std::sin(a), c.y - radius * std::cos(a));
}

CPoint localPoint(const CView* v, const CPoint& parentPoint) {
    return CPoint(parentPoint.x - v->getViewSize().left, parentPoint.y - v->getViewSize().top);
}

/// Fond « métal » d'un bouton matériel.
void hwFace(CDrawContext* c, const CRect& r, bool pressed) {
    c->setFillColor(pressed ? CColor(24, 25, 28) : CColor(36, 37, 41));
    c->drawRect(r, kDrawFilled);
    c->setFillColor(pressed ? CColor(22, 23, 26) : CColor(29, 30, 33));
    c->drawRect(CRect(r.left, r.top + r.getHeight() * 0.5, r.right, r.bottom), kDrawFilled);
    c->setLineWidth(1.0);
    c->setFrameColor(CColor(0, 0, 0, 255));
    c->drawRect(CRect(r).inset(0.5, 0.5), kDrawStroked);
    c->setFrameColor(CColor(255, 255, 255, pressed ? 6 : 18));
    c->drawLine(CPoint(r.left + 1.5, r.top + 1.5), CPoint(r.right - 1.5, r.top + 1.5));
}

} // namespace

// -----------------------------------------------------------------------------
//  LedKnob
// -----------------------------------------------------------------------------
LedKnob::LedKnob(const CRect& r, std::string label, Model model, Size size)
    : CView(r), label_(std::move(label)), model_(std::move(model)), size_(size) {
    setWantsFocus(false);
}

CCoord LedKnob::heightFor(Size size) {
    switch (size) {
        case Size::XSmall: return 34 + 41;
        case Size::Small: return 42 + 43;
        case Size::Mid: return 50 + 43;
        case Size::Big: return 70 + 45;
    }
    return 85;
}

CCoord LedKnob::diameter() const {
    switch (size_) {
        case Size::XSmall: return 34;
        case Size::Small: return 42;
        case Size::Mid: return 50;
        case Size::Big: return 70;
    }
    return 42;
}

void LedKnob::draw(CDrawContext* c) {
    c->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const CRect r = getViewSize();
    const double d = diameter();
    const double v = model_.get ? std::clamp(model_.get(), 0.0, 1.0) : 0.0;
    shownValue_ = v;
    shownText_ = model_.text ? model_.text() : std::string();
    const bool moved = std::fabs(v - model_.origin) > 0.004;

    // Nom au-dessus.
    text(c, label_, CRect(r.left, r.top, r.right, r.top + 12), moved ? color::kText2 : color::kText3, mono(size_ == Size::Big ? 10 : 9),
         kCenterText);
    const CPoint center(r.left + r.getWidth() * 0.5, r.top + 14 + d * 0.5 + 6);

    // Couronne de LED : allumées du repos à la valeur, plus chaudes vers les butées.
    const int count = size_ == Size::Big ? 31 : 21;
    const double crown = d * 0.5 + 5.0, dot = size_ == Size::Big ? 1.7 : (size_ == Size::XSmall ? 1.5 : 1.8);
    const double lo = std::min(v, model_.origin), hi = std::max(v, model_.origin);
    const double span = std::max({model_.origin, 1.0 - model_.origin, 1.0e-6});
    const double step = 1.0 / (count - 1);
    for (int i = 0; i < count; ++i) {
        const double t = i * step;
        const CPoint p = polar(center, crown, kStartDeg + kSweepDeg * t);
        const bool on = t >= lo - step / 2 && t <= hi + step / 2 && hi - lo > step / 4;
        const bool here = std::fabs(t - v) <= step / 2;
        if (on || here) {
            const double heat = std::pow(std::fabs(t - model_.origin) / span, 2.0);
            CColor lc = ledColor(heat);
            if (!on) lc = withAlpha(lc, 0.55);
            fillCircle(c, p, dot + 1.6, withAlpha(lc, 0.20));
            fillCircle(c, p, dot, lc);
        } else {
            fillCircle(c, p, dot, color::kDot);
        }
    }

    // Corps (moleté), chapeau, repère.
    const double body = d * 0.5 * 0.82;
    fillCircle(c, CPoint(center.x, center.y + 2), body + 1.5, CColor(0, 0, 0, 150)); // ombre portée
    fillCircle(c, center, body, color::kKnobBody);
    // Molette : stries rayonnantes.
    const int teeth = size_ == Size::Big ? 48 : 32;
    c->setLineWidth(1.0);
    c->setFrameColor(CColor(24, 25, 28));
    for (int i = 0; i < teeth; ++i) {
        const double a = 360.0 * i / teeth;
        c->drawLine(polar(center, body - 3.0, a), polar(center, body - 0.5, a));
    }
    strokeCircle(c, center, body, CColor(0, 0, 0, 255));
    const double cap = body * 0.78;
    fillCircle(c, center, cap, color::kKnobCap);
    // Reflet en haut du chapeau, ombre en bas.
    fillCircle(c, CPoint(center.x, center.y - cap * 0.18), cap * 0.80, CColor(62, 64, 70));
    fillCircle(c, CPoint(center.x, center.y + cap * 0.05), cap * 0.84, color::kKnobCap);
    strokeCircle(c, center, cap, CColor(255, 255, 255, 20));
    const double angle = kStartDeg + kSweepDeg * v;
    c->setLineWidth(size_ == Size::Big ? 2.5 : 2.0);
    c->setLineStyle(kLineSolid);
    c->setFrameColor(dragging_ ? color::kAccent : CColor(242, 242, 242));
    c->drawLine(polar(center, body * 0.30, angle), polar(center, body * 0.92, angle));
    c->setLineWidth(1.0);

    // Afficheur de la valeur.
    const double boxW = std::min<double>(r.getWidth() - 2, size_ == Size::Big ? 84 : 66);
    const CRect box(center.x - boxW / 2, center.y + d * 0.5 + 9, center.x + boxW / 2, center.y + d * 0.5 + 24);
    screen(c, box);
    text(c, shownText_, box, moved ? color::kAccent : color::kText2, mono(size_ == Size::Big ? 11 : 10), kCenterText);
    setDirty(false);
}

void LedKnob::setNorm(double v) {
    if (model_.steps > 0) v = std::round(v * model_.steps) / model_.steps;
    if (model_.set) model_.set(std::clamp(v, 0.0, 1.0));
    invalid();
}

void LedKnob::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft()) return;
    e.consumed = true;
    if (e.clickCount >= 2) {
        if (model_.begin) model_.begin();
        setNorm(model_.def);
        if (model_.end) model_.end();
        return;
    }
    dragging_ = true;
    startY_ = e.mousePosition.y;
    startValue_ = model_.get ? model_.get() : 0.0;
    if (model_.begin) model_.begin();
    invalid();
}

void LedKnob::onMouseMoveEvent(MouseMoveEvent& e) {
    if (!dragging_) return;
    e.consumed = true;
    const bool fine = e.modifiers.has(ModifierKey::Shift);
    const double pixels = (size_ == Size::Big ? 240.0 : 190.0) * (fine ? 5.0 : 1.0);
    setNorm(startValue_ + (startY_ - e.mousePosition.y) / pixels);
}

void LedKnob::onMouseUpEvent(MouseUpEvent& e) {
    if (!dragging_) return;
    e.consumed = true;
    dragging_ = false;
    if (model_.end) model_.end();
    invalid();
}

void LedKnob::onMouseCancelEvent(MouseCancelEvent& e) {
    if (dragging_ && model_.end) model_.end();
    dragging_ = false;
    e.consumed = true;
}

void LedKnob::onMouseWheelEvent(MouseWheelEvent& e) {
    const double delta = e.deltaY != 0.0 ? e.deltaY : e.deltaX;
    if (delta == 0.0) return;
    e.consumed = true;
    const double stepSize = model_.steps > 0 ? 1.0 / model_.steps : (e.modifiers.has(ModifierKey::Shift) ? 0.004 : 0.02);
    if (model_.begin) model_.begin();
    setNorm((model_.get ? model_.get() : 0.0) + (delta > 0 ? stepSize : -stepSize));
    if (model_.end) model_.end();
}

void LedKnob::tick() {
    const double v = model_.get ? model_.get() : 0.0;
    if (std::fabs(v - shownValue_) > 1.0e-6 || (model_.text && model_.text() != shownText_)) invalid();
}

// -----------------------------------------------------------------------------
//  HwButton
// -----------------------------------------------------------------------------
HwButton::HwButton(const CRect& r, std::string label, std::function<bool()> isOn, std::function<void()> onClick, bool withLed)
    : CView(r), label_(std::move(label)), isOn_(std::move(isOn)), onClick_(std::move(onClick)), withLed_(withLed) {}

void HwButton::draw(CDrawContext* c) {
    const CRect r = getViewSize();
    const bool on = isOn_ && isOn_();
    const bool lit = on && (!blink_ || phase_ < 5);
    hwFace(c, r, false);
    CCoord x = r.left + 10;
    if (withLed_) {
        led(c, CPoint(r.left + 11, r.top + r.getHeight() * 0.5), lit);
        x = r.left + 20;
    }
    const CColor col = on ? color::kAccent : (accentText_ ? color::kText : color::kText2);
    text(c, label_, CRect(x, r.top, r.right - (withLed_ ? 6 : 10), r.bottom), col, mono(10.5, false), withLed_ ? kLeftText : kCenterText);
    shownState_ = (on ? 1 : 0) | (lit ? 2 : 0);
    setDirty(false);
}

void HwButton::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft()) return;
    e.consumed = true;
    e.ignoreFollowUpMoveAndUpEvents(true);
    if (onClick_) onClick_();
    invalid();
}

void HwButton::tick() {
    phase_ = (phase_ + 1) % 10;
    const bool on = isOn_ && isOn_();
    const bool lit = on && (!blink_ || phase_ < 5);
    if (((on ? 1 : 0) | (lit ? 2 : 0)) != shownState_) invalid();
}

// -----------------------------------------------------------------------------
//  SegControl
// -----------------------------------------------------------------------------
SegControl::SegControl(const CRect& r, std::vector<std::string> labels, std::function<int()> get, std::function<void(int)> set)
    : CView(r), labels_(std::move(labels)), get_(std::move(get)), set_(std::move(set)) {}

void SegControl::draw(CDrawContext* c) {
    const CRect r = getViewSize();
    inset(c, r);
    const int n = static_cast<int>(labels_.size());
    const int current = get_ ? get_() : 0;
    const double w = (r.getWidth() - 4.0 - 2.0 * (n - 1)) / std::max(1, n);
    for (int i = 0; i < n; ++i) {
        const CRect s(r.left + 2 + i * (w + 2), r.top + 2, r.left + 2 + i * (w + 2) + w, r.bottom - 2);
        if (i == current) {
            c->setFillColor(CColor(34, 35, 39));
            c->drawRect(s, kDrawFilled);
            c->setFrameColor(CColor(255, 255, 255, 15));
            c->drawLine(CPoint(s.left, s.top + 0.5), CPoint(s.right, s.top + 0.5));
        }
        text(c, labels_[static_cast<std::size_t>(i)], s, i == current ? color::kAccent : color::kText3, mono(9.5), kCenterText);
    }
    shown_ = current;
    setDirty(false);
}

void SegControl::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft()) return;
    e.consumed = true;
    e.ignoreFollowUpMoveAndUpEvents(true);
    const CPoint p = localPoint(this, e.mousePosition);
    const int n = static_cast<int>(labels_.size());
    const int i = std::clamp(static_cast<int>(p.x / std::max<CCoord>(1.0, getViewSize().getWidth()) * n), 0, n - 1);
    if (set_) set_(i);
    invalid();
}

void SegControl::tick() {
    if ((get_ ? get_() : 0) != shown_) invalid();
}

// -----------------------------------------------------------------------------
//  Screen
// -----------------------------------------------------------------------------
Screen::Screen(const CRect& r, Painter painter, bool alwaysRedraw) : CView(r), painter_(std::move(painter)), always_(alwaysRedraw) {}

void Screen::draw(CDrawContext* c) {
    const CRect r = getViewSize();
    screen(c, r);
    if (painter_) {
        c->saveGlobalState();
        c->setClipRect(r);
        painter_(c, r);
        c->restoreGlobalState();
    }
    dirty_ = false;
    setDirty(false);
}

void Screen::onMouseDownEvent(MouseDownEvent& e) {
    if (!mouse_ || !e.buttonState.isLeft()) return;
    const CPoint p = localPoint(this, e.mousePosition);
    if (mouse_(p, e.clickCount >= 2 ? 3 : 0, e.modifiers.has(ModifierKey::Shift))) {
        e.consumed = true;
        tracking_ = e.clickCount < 2;
        if (!tracking_) e.ignoreFollowUpMoveAndUpEvents(true);
        invalid();
    }
}

void Screen::onMouseMoveEvent(MouseMoveEvent& e) {
    if (!tracking_ || !mouse_) return;
    e.consumed = true;
    mouse_(localPoint(this, e.mousePosition), 1, e.modifiers.has(ModifierKey::Shift));
    invalid();
}

void Screen::onMouseUpEvent(MouseUpEvent& e) {
    if (!tracking_ || !mouse_) return;
    e.consumed = true;
    tracking_ = false;
    mouse_(localPoint(this, e.mousePosition), 2, false);
    invalid();
}

void Screen::tick() {
    if (always_ || dirty_) invalid();
}

// -----------------------------------------------------------------------------
//  RackSlot
// -----------------------------------------------------------------------------
namespace {

/// Toute la surface du module, sous ses commandes : clic = sélection, voyant =
/// allumé / éteint, glisser = déplacer le module dans le rack.
class SlotGrip : public CView {
public:
    SlotGrip(const CRect& r, RackSlot& owner, RackSlot::Callbacks& cb) : CView(r), owner_(owner), cb_(cb) {}
    void draw(CDrawContext*) override { setDirty(false); }
    void onMouseDownEvent(MouseDownEvent& e) override {
        if (!e.buttonState.isLeft()) return;
        e.consumed = true;
        const CPoint p = localPoint(this, e.mousePosition);
        if (owner_.ledRect().pointInside(p)) {
            if (cb_.toggle) cb_.toggle();
            e.ignoreFollowUpMoveAndUpEvents(true);
            owner_.invalid();
            return;
        }
        pressed_ = true;
        dragging_ = false;
        pressX_ = frameX(e.mousePosition);
    }
    void onMouseMoveEvent(MouseMoveEvent& e) override {
        if (!pressed_) return;
        e.consumed = true;
        const CCoord x = frameX(e.mousePosition);
        if (!dragging_ && std::fabs(x - pressX_) > 6.0) dragging_ = true;
        if (dragging_ && cb_.dragMove) cb_.dragMove(x);
    }
    void onMouseUpEvent(MouseUpEvent& e) override {
        if (!pressed_) return;
        e.consumed = true;
        pressed_ = false;
        if (dragging_) {
            if (cb_.drop) cb_.drop(frameX(e.mousePosition));
        } else if (cb_.select) {
            cb_.select();
        }
        dragging_ = false;
        owner_.invalid();
    }
    void onMouseCancelEvent(MouseCancelEvent& e) override {
        pressed_ = dragging_ = false;
        if (cb_.drop) cb_.drop(-1.0);
        e.consumed = true;
    }

private:
    CCoord frameX(const CPoint& parentPoint) const {
        CPoint p = parentPoint;
        localToFrame(p);
        return p.x;
    }
    RackSlot& owner_;
    RackSlot::Callbacks& cb_;
    bool pressed_ = false, dragging_ = false;
    CCoord pressX_ = 0.0;
};

} // namespace

RackSlot::RackSlot(const CRect& r, std::string number, std::string title, Callbacks cb)
    : CViewContainer(r), number_(std::move(number)), title_(std::move(title)), cb_(std::move(cb)) {
    setTransparency(false);
    addView(new SlotGrip(CRect(0, 0, r.getWidth(), r.getHeight()), *this, cb_));
}

CRect RackSlot::ledRect() const {
    const CRect r = getViewSize();
    return CRect(r.getWidth() - 34, 10, r.getWidth() - 6, 30);
}

void RackSlot::drawBackgroundRect(CDrawContext* c, const CRect& /*updateRect*/) {
    const CRect r(0, 0, getViewSize().getWidth(), getViewSize().getHeight());
    const bool selected = cb_.isSelected && cb_.isSelected();
    const bool on = !cb_.isOn || cb_.isOn();
    // Face du module : métal brossé, plus clair en haut.
    c->setFillColor(CColor(32, 33, 37));
    c->drawRect(r, kDrawFilled);
    c->setFillColor(color::kMetal2);
    c->drawRect(CRect(r.left, r.top + r.getHeight() * 0.35, r.right, r.bottom), kDrawFilled);
    c->setFillColor(CColor(23, 24, 27));
    c->drawRect(CRect(r.left, r.top + r.getHeight() * 0.75, r.right, r.bottom), kDrawFilled);
    c->setLineWidth(1.0);
    c->setFrameColor(CColor(255, 255, 255, 4));
    for (CCoord x = r.left + 2; x < r.right; x += 3) c->drawLine(CPoint(x, r.top + 1), CPoint(x, r.bottom - 1));
    c->setFrameColor(selected ? color::kAccent35 : CColor(0, 0, 0, 255));
    c->drawRect(CRect(r).inset(0.5, 0.5), kDrawStroked);
    c->setFrameColor(CColor(255, 255, 255, 16));
    c->drawLine(CPoint(r.left + 1, r.top + 1.5), CPoint(r.right - 1, r.top + 1.5));
    if (selected) {
        c->setFrameColor(color::kAccent15);
        c->drawRect(CRect(r).inset(1.5, 1.5), kDrawStroked);
    }
    // Vis de fixation du format 500.
    c->setDrawMode(kAntiAliasing | kNonIntegralMode);
    for (const CCoord y : {r.top + 6.5, r.bottom - 6.5}) {
        if (selected && y > r.getHeight() / 2) continue;
        fillCircle(c, CPoint(r.getWidth() / 2, y), 3.5, CColor(58, 60, 65));
        fillCircle(c, CPoint(r.getWidth() / 2 + 0.4, y + 0.4), 2.8, CColor(30, 31, 34));
        c->setFrameColor(CColor(90, 92, 98));
        c->drawLine(CPoint(r.getWidth() / 2 - 2, y - 0.5), CPoint(r.getWidth() / 2 + 2, y + 0.5));
    }
    // En-tête : numéro, nom, voyant.
    const double dim = dimmed_ || !on ? 0.38 : 1.0;
    text(c, number_, CRect(8, 12, 22, 28), selected ? color::kAccent : color::kText3, mono(10));
    text(c, title_, CRect(22, 12, r.getWidth() - 36, 28), withAlpha(color::kText, dim), sans(11.5, true));
    const CRect lr = ledRect();
    hwFace(c, lr, false);
    led(c, CPoint(lr.left + 8, lr.top + lr.getHeight() / 2), on, 2.6);
    text(c, on ? "ON" : "OFF", CRect(lr.left + 12, lr.top, lr.right - 2, lr.bottom), on ? color::kAccent : color::kText3, mono(8),
         kCenterText);
    // État : texte et barre de LED.
    const CRect st(8, r.getHeight() - 40, r.getWidth() - 8, r.getHeight() - 12);
    screen(c, st);
    if (selected) {
        c->setFrameColor(color::kAccent);
        c->drawLine(CPoint(st.left, st.bottom - 0.5), CPoint(st.right, st.bottom - 0.5));
    }
    const std::string status = cb_.status ? cb_.status() : std::string();
    text(c, status, CRect(st.left + 4, st.top + 2, st.right - 4, st.top + 15), withAlpha(selected ? color::kText : color::kText2, dim),
         mono(9), kCenterText);
    const double level = cb_.level ? std::clamp(cb_.level(), 0.0, 1.0) : 0.0;
    const int cells = 14;
    const double cw = (st.getWidth() - 8 - (cells - 1) * 2.0) / cells;
    for (int i = 0; i < cells; ++i) {
        const CRect cell(st.left + 4 + i * (cw + 2), st.top + 18, st.left + 4 + i * (cw + 2) + cw, st.top + 22);
        const bool lit = on && (i + 0.5) / cells <= level && level > 0.001;
        c->setFillColor(lit ? (i >= cells - 3 ? color::kAccentHot : color::kAccent) : CColor(255, 255, 255, 18));
        c->drawRect(cell, kDrawFilled);
    }
    if (dropMark_ != 0) {
        const CCoord x = dropMark_ < 0 ? 1.5 : r.getWidth() - 1.5;
        c->setLineWidth(3.0);
        c->setFrameColor(color::kAccent);
        c->drawLine(CPoint(x, 4), CPoint(x, r.getHeight() - 4));
        c->setLineWidth(1.0);
    }
    shownStatus_ = status;
    shownLevel_ = level;
    shownFlags_ = (selected ? 1 : 0) | (on ? 2 : 0) | (dimmed_ ? 4 : 0);
}

void RackSlot::onMouseDownEvent(MouseDownEvent& e) { CViewContainer::onMouseDownEvent(e); }
void RackSlot::onMouseMoveEvent(MouseMoveEvent& e) { CViewContainer::onMouseMoveEvent(e); }
void RackSlot::onMouseUpEvent(MouseUpEvent& e) { CViewContainer::onMouseUpEvent(e); }

void RackSlot::tick() {
    const bool selected = cb_.isSelected && cb_.isSelected();
    const bool on = !cb_.isOn || cb_.isOn();
    const int flags = (selected ? 1 : 0) | (on ? 2 : 0) | (dimmed_ ? 4 : 0);
    const std::string status = cb_.status ? cb_.status() : std::string();
    const double level = cb_.level ? std::clamp(cb_.level(), 0.0, 1.0) : 0.0;
    if (flags != shownFlags_ || status != shownStatus_ || std::fabs(level - shownLevel_) > 0.02) {
        invalid();
    }
}

} // namespace omnis::vst3::gui
