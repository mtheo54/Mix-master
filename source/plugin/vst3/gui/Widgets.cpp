// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/gui/Widgets.cpp
// =============================================================================
#include "Widgets.h"

#include <algorithm>
#include <cctype>

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

/// Fond d'un bouton : plat, liseré fin (l'univers du site abrasion labs).
void hwFace(CDrawContext* c, const CRect& r, bool pressed) {
    c->setFillColor(pressed ? CColor(8, 8, 9) : CColor(15, 15, 17));
    c->drawRect(r, kDrawFilled);
    c->setLineWidth(1.0);
    c->setFrameColor(CColor(52, 52, 57));
    c->drawRect(CRect(r).inset(0.5, 0.5), kDrawStroked);
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
    if (filled_) { // le bouton principal (AUTO) : plein, couleur de la marque
        c->setFillColor(lit ? color::kAccentHot : color::kAccent);
        c->drawRect(r, kDrawFilled);
        c->setFillColor(CColor(10, 10, 10));
        c->drawRect(CRect(r.left + 14, r.getCenter().y - 3.5, r.left + 21, r.getCenter().y + 3.5), kDrawFilled);
        text(c, label_, CRect(r.left + 24, r.top, r.right - 8, r.bottom), CColor(10, 10, 10), mono(12, true), kCenterText);
        shownState_ = (on ? 1 : 0) | (lit ? 2 : 0);
        setDirty(false);
        return;
    }
    hwFace(c, r, false);
    if (on && !withLed_) {
        c->setFrameColor(color::kAccent);
        c->drawRect(CRect(r).inset(0.5, 0.5), kDrawStroked);
    }
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
    // Segments : le choix en blanc plein, les autres en gris (comme le site).
    c->setFillColor(CColor(10, 10, 11));
    c->drawRect(r, kDrawFilled);
    c->setLineWidth(1.0);
    c->setFrameColor(CColor(52, 52, 57));
    c->drawRect(CRect(r).inset(0.5, 0.5), kDrawStroked);
    const int n = static_cast<int>(labels_.size());
    const int current = get_ ? get_() : 0;
    const double w = (r.getWidth() - 6.0 - 2.0 * (n - 1)) / std::max(1, n);
    for (int i = 0; i < n; ++i) {
        const CRect s(r.left + 3 + i * (w + 2), r.top + 3, r.left + 3 + i * (w + 2) + w, r.bottom - 3);
        if (i == current) {
            c->setFillColor(CColor(236, 236, 238));
            c->drawRect(s, kDrawFilled);
        }
        text(c, labels_[static_cast<std::size_t>(i)], s, i == current ? CColor(10, 10, 10) : color::kText2, mono(9.5, i == current), kCenterText);
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
    // La croix : retirer le module de la chaîne (il retourne dans la bibliothèque).
    const CRect r = getViewSize();
    return CRect(r.getWidth() - 22, 6, r.getWidth() - 4, 24);
}

void RackSlot::drawBackgroundRect(CDrawContext* c, const CRect& /*updateRect*/) {
    c->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const CRect r(0, 0, getViewSize().getWidth(), getViewSize().getHeight());
    const bool selected = cb_.isSelected && cb_.isSelected();
    const bool on = !cb_.isOn || cb_.isOn();
    // Une carte plate, liseré fin ; choisie : liseré et coins de la marque.
    c->setFillColor(selected ? CColor(17, 16, 13) : CColor(13, 13, 14));
    c->drawRect(r, kDrawFilled);
    c->setLineWidth(1.0);
    c->setFrameColor(selected ? color::kAccent : CColor(44, 44, 48));
    c->drawRect(CRect(r).inset(0.5, 0.5), kDrawStroked);
    if (selected) {
        c->setLineWidth(2.0);
        const double k = 9.0;
        for (const CPoint corner : {CPoint(r.left + 1, r.top + 1), CPoint(r.right - 1, r.top + 1), CPoint(r.left + 1, r.bottom - 1), CPoint(r.right - 1, r.bottom - 1)}) {
            const double dx = corner.x < r.getCenter().x ? k : -k, dy = corner.y < r.getCenter().y ? k : -k;
            c->drawLine(corner, CPoint(corner.x + dx, corner.y));
            c->drawLine(corner, CPoint(corner.x, corner.y + dy));
        }
        c->setLineWidth(1.0);
    }
    const double dim = dimmed_ || !on ? 0.38 : 1.0;
    // En-tête : numéro, nom (en capitales), voyant, croix.
    text(c, number_.size() < 2 ? "0" + number_ : number_, CRect(8, 7, 26, 21), selected ? color::kAccent : color::kText3, mono(9, true));
    const std::string upper = upperText(i18n::tr(title_));
    auto font = mono(11, true);
    c->setFont(font);
    for (double size = 10.5; size >= 8.5 && c->getStringWidth(upper.c_str()) > r.getWidth() - 62; size -= 0.5) {
        font = mono(size, true);
        c->setFont(font);
    }
    text(c, upper, CRect(26, 6, r.getWidth() - 36, 22), withAlpha(color::kText, dim), font);
    if (on) fillCircle(c, CPoint(r.getWidth() - 30, 15), 5.5, withAlpha(color::kAccent, 0.22));
    fillCircle(c, CPoint(r.getWidth() - 30, 15), 3.0, on ? color::kAccent : CColor(70, 70, 75));
    const CRect x = ledRect();
    text(c, "×", x, color::kText3, mono(12), kCenterText);
    // Le caractère, en étiquette.
    if (const std::string ch = cb_.character ? cb_.character() : std::string(); !ch.empty()) {
        const std::string up = upperText(ch);
        c->setFont(mono(8.5, true));
        const double tw = std::min(r.getWidth() - 20, c->getStringWidth(up.c_str()) + 16);
        const CRect tag(r.getCenter().x - tw / 2, 28, r.getCenter().x + tw / 2, 43);
        c->setFrameColor(withAlpha(color::kAccent, 0.8 * dim));
        c->drawRect(CRect(tag).inset(0.5, 0.5), kDrawStroked);
        text(c, up, tag, withAlpha(color::kAccent, dim), mono(8.5, true), kCenterText);
    }
    // État et barre d'activité, en bas.
    const std::string status = cb_.status ? cb_.status() : std::string();
    text(c, status, CRect(6, r.getHeight() - 32, r.getWidth() - 6, r.getHeight() - 18), withAlpha(selected ? color::kText : color::kText2, dim), mono(9),
         kCenterText);
    const double level = cb_.level ? std::clamp(cb_.level(), 0.0, 1.0) : 0.0;
    const CRect bar(10, r.getHeight() - 12, r.getWidth() - 10, r.getHeight() - 9);
    c->setFillColor(CColor(255, 255, 255, 22));
    c->drawRect(bar, kDrawFilled);
    if (on && level > 0.001) {
        c->setFillColor(level > 0.8 ? color::kAccentHot : color::kAccent);
        c->drawRect(CRect(bar.left, bar.top, bar.left + bar.getWidth() * level, bar.bottom), kDrawFilled);
    }
    if (dropMark_ != 0) {
        const CCoord dx = dropMark_ < 0 ? 1.5 : r.getWidth() - 1.5;
        c->setLineWidth(3.0);
        c->setFrameColor(color::kAccent);
        c->drawLine(CPoint(dx, 4), CPoint(dx, r.getHeight() - 4));
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
