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
    // Le carré du SVG de la maquette (74 px sur les cartes, 58 px au réglage).
    switch (size_) {
        case Size::XSmall: return 42;
        case Size::Small: return 50;
        case Size::Mid: return 58;
        case Size::Big: return 74;
    }
    return 54;
}

void LedKnob::draw(CDrawContext* c) {
    c->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const CRect r = getViewSize();
    const double d = diameter();
    const double v = model_.get ? std::clamp(model_.get(), 0.0, 1.0) : 0.0;
    shownValue_ = v;
    shownText_ = model_.text ? model_.text() : std::string();
    // Le nom au-dessus (`.macro-name` : capitales espacées, gris).
    {
        // Un nom long se resserre (interlettrage, puis taille) plutôt que de déborder sur le voisin.
        const std::string name = upperText(label_);
        double em = 0.22, size = size_ == Size::Big ? 9.9 : 9.4;
        while (em > 0.04 && spacedWidth(c, name, mono(size), em) > r.getWidth() + 8) em -= 0.03;
        while (size > 7.5 && spacedWidth(c, name, mono(size), em) > r.getWidth() + 8) size -= 0.4;
        spaced(c, name, CRect(r.left - 6, r.top, r.right + 6, r.top + 12), color::kText2, mono(size), em, kCenterText);
    }
    const CPoint center(r.left + r.getWidth() * 0.5, r.top + 16 + d * 0.5);
    const double R = d / 2 - 6;
    // La piste, puis l'arc (du repos à la valeur : d'un bout, ou du centre pour un réglage ± ).
    const auto arc = [&](double from, double to, const CColor& col) {
        if (to - from < 0.002) return;
        auto path = VSTGUI::owned(c->createGraphicsPath());
        if (!path) return;
        path->addArc(CRect(center.x - R, center.y - R, center.x + R, center.y + R), 135.0 + 270.0 * from, 135.0 + 270.0 * to, true);
        c->setLineWidth(3.0);
        c->setLineStyle(CLineStyle(CLineStyle::kLineCapButt));
        c->setFrameColor(col);
        c->drawGraphicsPath(path, CDrawContext::kPathStroked);
    };
    arc(0.0, 1.0, CColor(255, 255, 255, 36));
    arc(std::min(v, model_.origin), std::max(v, model_.origin), dragging_ ? color::kText : color::kAccent);
    c->setLineStyle(kLineSolid);
    // Le disque noir, cerclé à 22 %, et l'aiguille blanche.
    fillCircle(c, center, R - 7, CColor(0, 0, 0));
    strokeCircle(c, center, R - 7, color::kLine2, 1.0);
    const double a = (135.0 + 270.0 * v) * std::numbers::pi / 180.0;
    c->setLineWidth(2.0);
    c->setLineStyle(CLineStyle(CLineStyle::kLineCapSquare));
    c->setFrameColor(CColor(255, 255, 255));
    c->drawLine(center, CPoint(center.x + (R - 9) * std::cos(a), center.y + (R - 9) * std::sin(a)));
    c->setLineStyle(kLineSolid);
    c->setLineWidth(1.0);
    // La valeur dessous (`.val`).
    text(c, shownText_, CRect(r.left - 6, center.y + d * 0.5 + 2, r.right + 6, center.y + d * 0.5 + 18), color::kText, mono(size_ == Size::Big ? 12.5 : 11.5),
         kCenterText);
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
    c->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const CRect r = getViewSize();
    const bool on = isOn_ && isOn_();
    const bool lit = on && (!blink_ || phase_ < 5);
    const double size = r.getHeight() >= 40 ? 13.1 : 11.5; // .btn : .82rem ; .btn.sm : .72rem
    if (filled_ || white_) { // `.btn.acc` (AUTO) ou `.btn` (APPLIQUER) : plein, coins coupés
        cutFill(c, r, filled_ ? (lit ? CColor(255, 255, 255) : color::kAccent) : CColor(255, 255, 255), r.getHeight() >= 40 ? 8.0 : 6.0);
        const double tw = spacedWidth(c, upperText(label_), mono(size, true), 0.1);
        double x = r.getCenter().x - tw / 2;
        if (filled_) { // le petit carré noir devant le mot
            x += 8;
            c->setFillColor(CColor(0, 0, 0));
            c->drawRect(CRect(x - 15, r.getCenter().y - 3.5, x - 8, r.getCenter().y + 3.5), kDrawFilled);
        }
        spaced(c, upperText(label_), CRect(x, r.top, r.right, r.bottom), CColor(5, 6, 10), mono(size, true), 0.1);
        shownState_ = (on ? 1 : 0) | (lit ? 2 : 0);
        setDirty(false);
        return;
    }
    // `.btn.ghost` : transparent, liseré blanc ; allumé : ambre.
    c->setFillColor(lit ? color::kAccent15 : CColor(0, 0, 0, 0));
    c->drawRect(r, kDrawFilled);
    c->setLineWidth(1.0);
    c->setFrameColor(lit ? color::kAccent : CColor(255, 255, 255, 153));
    c->drawRect(CRect(r).inset(0.5, 0.5), kDrawStroked);
    spaced(c, upperText(label_), r, lit ? color::kAccent : (accentText_ ? color::kAccent : color::kText), mono(size, true), 0.1, kCenterText);
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
    c->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const CRect r = getViewSize();
    const int n = static_cast<int>(labels_.size());
    const int current = get_ ? get_() : 0;
    if (chips_) { // `.chip` : des puces séparées à la largeur de leur mot, la choisie en ambre plein
        // La plus grande taille où toutes les puces tiennent (à la ligne si besoin) dans la zone.
        const double gap = 6.0, h = std::min<double>(24.0, r.getHeight());
        // D'abord sur une ligne (en resserrant un peu la taille), sinon à la ligne.
        double size = 11.5;
        bool oneLine = true;
        for (; size >= 9.5; size -= 0.5) {
            double total = gap * (n - 1);
            for (int i = 0; i < n; ++i) total += spacedWidth(c, upperText(labels_[static_cast<std::size_t>(i)]), mono(size), 0.1) + 1.2 * size + 4;
            if (total <= r.getWidth() + 0.5) break;
        }
        if (size < 9.5) {
            oneLine = false;
            size = 11.5;
        }
        std::vector<CRect> rects;
        for (;; size -= 0.5) {
            rects.clear();
            double x = r.left, y = r.top;
            for (int i = 0; i < n; ++i) {
                const double w = std::min<double>(r.getWidth(), spacedWidth(c, upperText(labels_[static_cast<std::size_t>(i)]), mono(size), 0.1) + 1.2 * size + 4);
                if (x > r.left && x + w > r.right + 0.5) {
                    x = r.left;
                    y += h + gap;
                }
                rects.emplace_back(x, y, x + w, y + h);
                x += w + gap;
            }
            if (oneLine || size <= 8.5 || rects.empty() || rects.back().bottom <= r.bottom + 0.5) break;
        }
        for (int i = 0; i < n; ++i) {
            const CRect& s = rects[static_cast<std::size_t>(i)];
            const bool on = i == current;
            c->setFillColor(on ? color::kAccent : CColor(0, 0, 0, 90));
            c->drawRect(s, kDrawFilled);
            c->setLineWidth(1.0);
            c->setFrameColor(on ? color::kAccent : color::kLine2);
            c->drawRect(CRect(s).inset(0.5, 0.5), kDrawStroked);
            spaced(c, upperText(labels_[static_cast<std::size_t>(i)]), s, on ? CColor(0, 0, 0) : color::kChip, mono(size, on), 0.1, kCenterText);
        }
        chipRects_ = std::move(rects);
        shown_ = current;
        setDirty(false);
        return;
    }
    // `.seg` : liseré à 22 %, fond noir à 50 %, 3 px de marge ; le choix en blanc plein.
    c->setFillColor(CColor(0, 0, 0, 128));
    c->drawRect(r, kDrawFilled);
    c->setLineWidth(1.0);
    c->setFrameColor(color::kLine2);
    c->drawRect(CRect(r).inset(0.5, 0.5), kDrawStroked);
    // Chaque bouton à la largeur de son mot (min-width 3.4rem, 0.8rem de chaque côté), étirés pour remplir.
    std::vector<double> want(static_cast<std::size_t>(n));
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += want[static_cast<std::size_t>(i)] = std::max(54.4, spacedWidth(c, upperText(labels_[static_cast<std::size_t>(i)]), mono(11.2, true), 0.14) + 25.6);
    const double k = (r.getWidth() - 6.0) / std::max(1.0, sum);
    double x = r.left + 3;
    for (int i = 0; i < n; ++i) {
        const double bw = want[static_cast<std::size_t>(i)] * k;
        const CRect s(x, r.top + 3, x + bw, r.bottom - 3);
        x += bw;
        const bool on = i == current;
        if (on) {
            c->setFillColor(CColor(255, 255, 255, 40)); // le halo
            c->drawRect(CRect(s).extend(2, 2), kDrawFilled);
            c->setFillColor(CColor(255, 255, 255));
            c->drawRect(s, kDrawFilled);
        }
        spaced(c, upperText(labels_[static_cast<std::size_t>(i)]), s, on ? CColor(0, 0, 0) : color::kText2, mono(11.2, true), 0.14, kCenterText);
        if (static_cast<int>(bounds_.size()) < n) bounds_.resize(static_cast<std::size_t>(n));
        bounds_[static_cast<std::size_t>(i)] = s.right;
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
    int i = std::clamp(static_cast<int>(p.x / std::max<CCoord>(1.0, getViewSize().getWidth()) * n), 0, n - 1);
    if (chips_ && !chipRects_.empty()) { // les puces : celle sous la souris, sinon rien
        const CPoint q(p.x + getViewSize().left, p.y + getViewSize().top);
        i = -1;
        for (int k = 0; k < static_cast<int>(chipRects_.size()); ++k)
            if (chipRects_[static_cast<std::size_t>(k)].pointInside(q)) i = k;
        if (i < 0) return;
    }
    if (!chips_ && !bounds_.empty()) // les boutons n'ont pas tous la même largeur
        for (int k = 0; k < n && k < static_cast<int>(bounds_.size()); ++k)
            if (p.x + getViewSize().left < bounds_[static_cast<std::size_t>(k)]) {
                i = k;
                break;
            }
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

/// La carte (sous ses commandes) : clic = la régler, le voyant = allumer / éteindre, × = la retirer
/// de la chaîne, l'étiquette = changer de caractère, glisser = la déplacer.
class SlotGrip : public CView {
public:
    SlotGrip(const CRect& r, RackSlot& owner, RackSlot::Callbacks& cb) : CView(r), owner_(owner), cb_(cb) {}
    void draw(CDrawContext*) override { setDirty(false); }
    void onMouseDownEvent(MouseDownEvent& e) override {
        if (!e.buttonState.isLeft()) return;
        e.consumed = true;
        CPoint p = localPoint(this, e.mousePosition);
        p.offset(getViewSize().left, getViewSize().top); // coordonnées de la carte (vue du module)
        const auto click = [&](const std::function<void()>& f) {
            if (f) f();
            e.ignoreFollowUpMoveAndUpEvents(true);
            owner_.invalid();
        };
        if (owner_.ledRect().pointInside(p)) return click(cb_.toggle);
        if (owner_.removeRect().pointInside(p)) return click(cb_.remove);
        if (owner_.charRect().pointInside(p) && cb_.nextCharacter) return click(cb_.nextCharacter);
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
    void onMouseEnterEvent(MouseEnterEvent& e) override {
        owner_.setHover(true);
        e.consumed = true;
    }
    void onMouseExitEvent(MouseExitEvent& e) override {
        owner_.setHover(false);
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
    setTransparency(true); // les marges (étiquette au-dessus, crochets) laissent voir la scène
    addView(new SlotGrip(cardRect(), *this, cb_));
}

CRect RackSlot::cardRect() const {
    const CRect r = getViewSize();
    return CRect(kMarginX, kMarginTop, r.getWidth() - kMarginX, r.getHeight() - kMarginBottom);
}

CRect RackSlot::ledRect() const {
    const CRect k = cardRect();
    return CRect(k.right - 46, k.top + 10, k.right - 28, k.top + 28);
}

CRect RackSlot::removeRect() const {
    const CRect k = cardRect();
    return CRect(k.right - 26, k.top + 9, k.right - 6, k.top + 29);
}

CRect RackSlot::charRect() const {
    const CRect k = cardRect();
    return CRect(k.left + 10, k.top + 35, k.right - 10, k.top + 56);
}

void RackSlot::setHover(bool h) {
    if (h != hover_) {
        hover_ = h;
        invalid();
    }
}

void RackSlot::drawBackgroundRect(CDrawContext* c, const CRect& /*updateRect*/) {
    c->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const CRect k = cardRect();
    const bool selected = cb_.isSelected && cb_.isSelected();
    const bool on = !cb_.isOn || cb_.isOn();
    // `.ctrk` au-dessus : « 01 · OBJ · SUB ».
    {
        const auto f = mono(9.0);
        const std::string num = number_.size() < 2 ? "0" + number_ : number_;
        const double nw = spacedWidth(c, num, f, 0.14);
        const double x0 = k.left + 8; // à droite du crochet de la carte choisie
        spaced(c, num, CRect(x0, 0, x0 + nw + 2, 12), color::kText, f, 0.14);
        spaced(c, " · OBJ · " + cb_.code, CRect(x0 + nw, 0, k.right + 20, 12), color::kText3, f, 0.14);
    }
    // La carte : #050505, liseré à 12 % ; survolée : ambre à 55 % ; choisie : ambre à 70 %, fond #0b0906, crochets.
    c->setFillColor(selected ? color::kMetalTop : color::kScreen);
    c->drawRect(k, kDrawFilled);
    c->setLineWidth(1.0);
    c->setFrameColor(selected ? CColor(255, 170, 0, 179) : (hover_ ? color::kAccent35 : color::kLine));
    c->drawRect(CRect(k).inset(0.5, 0.5), kDrawStroked);
    if (selected) brackets(c, k, color::kAccent, 4.0);
    const double dim = on ? 1.0 : 0.4; // `.card.off`
    // `.head` : le nom, le voyant, la croix.
    const CRect x = removeRect(), l = ledRect();
    const std::string title = upperText(title_);
    double titleSize = 13.1; // un nom long (MULTIBANDE) se resserre avant le voyant
    while (titleSize > 9.5 && spacedWidth(c, title, mono(titleSize, true), 0.08) > l.left - k.left - 16) titleSize -= 0.4;
    const auto font = mono(titleSize, true);
    spaced(c, title, CRect(k.left + 10, k.top + 11, l.left - 2, k.top + 29), withAlpha(color::kText, dim), font, 0.08);
    const CPoint lc = l.getCenter();
    if (on) fillCircle(c, lc, 7.0, CColor(255, 170, 0, 70)); // box-shadow 0 0 10px
    fillCircle(c, lc, 3.5, on ? color::kAccent : color::kText3);
    text(c, "×", x, withAlpha(color::kText3, std::max(dim, 0.6)), mono(14.4), kCenterText);
    // `.char` : le caractère, en étiquette (un clic : le suivant).
    if (const std::string ch = cb_.character ? cb_.character() : std::string(); !ch.empty()) {
        const std::string up = upperText(ch);
        const double tw = spacedWidth(c, up, mono(10.2), 0.14) + 15;
        const CRect tag(k.getCenter().x - tw / 2, k.top + 35, k.getCenter().x + tw / 2, k.top + 56);
        c->setFrameColor(withAlpha(color::kAccent, 0.55 * dim));
        c->drawRect(CRect(tag).inset(0.5, 0.5), kDrawStroked);
        spaced(c, up, tag, withAlpha(color::kAccent, dim), mono(10.2), 0.14, kCenterText);
    }
    // `.gr` : la barre d'activité, 3 px.
    const double level = cb_.level ? std::clamp(cb_.level(), 0.0, 1.0) : 0.0;
    const CRect bar(k.left + 10, k.bottom - 16, k.right - 10, k.bottom - 13);
    c->setFillColor(CColor(255, 255, 255, 20));
    c->drawRect(bar, kDrawFilled);
    if (on && level > 0.001) {
        c->setFillColor(color::kAccent);
        c->drawRect(CRect(bar.left, bar.top, bar.left + bar.getWidth() * level, bar.bottom), kDrawFilled);
    }
    // `.slot-mark` : là où la carte glissée se posera.
    if (dropMark_ != 0) {
        const CCoord mx = dropMark_ < 0 ? k.left - 9 : k.right + 9;
        c->setFillColor(CColor(255, 170, 0, 60));
        c->drawRect(CRect(mx - 4, k.top, mx + 4, k.bottom), kDrawFilled);
        c->setFillColor(color::kAccent);
        c->drawRect(CRect(mx - 1, k.top, mx + 1, k.bottom), kDrawFilled);
    }
    shownStatus_ = cb_.status ? cb_.status() : std::string();
    shownLevel_ = level;
    shownFlags_ = (selected ? 1 : 0) | (on ? 2 : 0) | (dimmed_ ? 4 : 0) | (hover_ ? 8 : 0);
}

void RackSlot::onMouseDownEvent(MouseDownEvent& e) { CViewContainer::onMouseDownEvent(e); }
void RackSlot::onMouseMoveEvent(MouseMoveEvent& e) { CViewContainer::onMouseMoveEvent(e); }
void RackSlot::onMouseUpEvent(MouseUpEvent& e) { CViewContainer::onMouseUpEvent(e); }

void RackSlot::tick() {
    const bool selected = cb_.isSelected && cb_.isSelected();
    const bool on = !cb_.isOn || cb_.isOn();
    const int flags = (selected ? 1 : 0) | (on ? 2 : 0) | (dimmed_ ? 4 : 0) | (hover_ ? 8 : 0);
    const double level = cb_.level ? std::clamp(cb_.level(), 0.0, 1.0) : 0.0;
    const std::string ch = cb_.character ? cb_.character() : std::string();
    if (flags != shownFlags_ || std::fabs(level - shownLevel_) > 0.02 || ch != shownCharacter_) {
        shownCharacter_ = ch;
        invalid();
    }
}

} // namespace omnis::vst3::gui
