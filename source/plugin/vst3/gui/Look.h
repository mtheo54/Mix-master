// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/gui/Look.h — L'apparence de la console (maquette v11) : face en
//  aluminium anodisé sombre, découpes usinées, écrans noirs, ambre réservé aux
//  signaux actifs. Polices du système (pas de fichier de police à livrer).
// =============================================================================
#pragma once

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/crect.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace omnis::vst3::gui {

using VSTGUI::CColor;
using VSTGUI::CCoord;
using VSTGUI::CDrawContext;
using VSTGUI::CPoint;
using VSTGUI::CRect;

namespace color {
inline const CColor kVoid(6, 7, 7);
inline const CColor kMetal(22, 23, 26);
inline const CColor kMetal2(27, 28, 32);
inline const CColor kMetalTop(32, 33, 37);
inline const CColor kInset(12, 13, 14);
inline const CColor kScreen(5, 5, 5);
inline const CColor kEdgeHi(255, 255, 255, 15);
inline const CColor kEdgeLo(0, 0, 0, 166);
inline const CColor kLine(255, 255, 255, 18);
inline const CColor kText(228, 229, 231);
inline const CColor kText2(154, 157, 164);
inline const CColor kText3(93, 96, 103);
inline const CColor kAccent(255, 170, 0);
inline const CColor kAccentHot(255, 119, 0);
inline const CColor kAccent15(255, 170, 0, 38);
inline const CColor kAccent35(255, 170, 0, 89);
inline const CColor kGrid(255, 255, 255, 23);
inline const CColor kClip(255, 59, 47);
inline const CColor kGood(110, 196, 140);
inline const CColor kCool(110, 170, 255);
inline const CColor kDot(255, 255, 255, 19);
inline const CColor kKnobBody(36, 37, 41);
inline const CColor kKnobCap(50, 52, 57);
} // namespace color

/// Couleur d'une LED : ambre près du repos, orange chaud vers les butées (d ∈ [0, 1]).
inline CColor ledColor(double d) {
    d = std::clamp(d, 0.0, 1.0);
    return CColor(255, static_cast<uint8_t>(std::lround(170.0 - 51.0 * d)), 0);
}

inline CColor withAlpha(CColor c, double a) {
    c.alpha = static_cast<uint8_t>(std::lround(std::clamp(a, 0.0, 1.0) * 255.0));
    return c;
}

// Polices du système : sans empattement pour les titres, chasse fixe pour les valeurs.
#if defined(_WIN32)
inline constexpr const char* kSansFace = "Segoe UI";
inline constexpr const char* kMonoFace = "Consolas";
#elif defined(__APPLE__)
inline constexpr const char* kSansFace = "Helvetica Neue";
inline constexpr const char* kMonoFace = "Menlo";
#else
inline constexpr const char* kSansFace = "DejaVu Sans";
inline constexpr const char* kMonoFace = "DejaVu Sans Mono";
#endif

inline VSTGUI::SharedPointer<VSTGUI::CFontDesc> sans(CCoord size, bool bold = false) {
    return VSTGUI::makeOwned<VSTGUI::CFontDesc>(kSansFace, size, bold ? VSTGUI::kBoldFace : VSTGUI::kNormalFace);
}
inline VSTGUI::SharedPointer<VSTGUI::CFontDesc> mono(CCoord size, bool bold = false) {
    return VSTGUI::makeOwned<VSTGUI::CFontDesc>(kMonoFace, size, bold ? VSTGUI::kBoldFace : VSTGUI::kNormalFace);
}

inline void text(CDrawContext* c, const std::string& s, const CRect& r, const CColor& col,
                 const VSTGUI::SharedPointer<VSTGUI::CFontDesc>& font, VSTGUI::CHoriTxtAlign align = VSTGUI::kLeftText) {
    c->setFont(font);
    c->setFontColor(col);
    c->drawString(VSTGUI::UTF8String(s), r, align, true);
}

/// Cercles par un chemin (le drawEllipse de VSTGUI sous Linux a la mauvaise échelle).
inline void fillCircle(CDrawContext* c, const CPoint& center, double radius, const CColor& col) {
    auto path = VSTGUI::owned(c->createGraphicsPath());
    if (!path) return;
    path->addEllipse(CRect(center.x - radius, center.y - radius, center.x + radius, center.y + radius));
    c->setFillColor(col);
    c->drawGraphicsPath(path, CDrawContext::kPathFilled);
}
inline void strokeCircle(CDrawContext* c, const CPoint& center, double radius, const CColor& col, double width = 1.0) {
    auto path = VSTGUI::owned(c->createGraphicsPath());
    if (!path) return;
    path->addEllipse(CRect(center.x - radius, center.y - radius, center.x + radius, center.y + radius));
    c->setLineWidth(width);
    c->setFrameColor(col);
    c->drawGraphicsPath(path, CDrawContext::kPathStroked);
}

/// Découpe usinée : fond noir mat, arête haute claire, arête basse sombre.
inline void inset(CDrawContext* c, const CRect& r, const CColor& fill = color::kInset) {
    c->setFillColor(fill);
    c->drawRect(r, VSTGUI::kDrawFilled);
    c->setLineWidth(1.0);
    c->setFrameColor(color::kEdgeHi);
    c->drawLine(CPoint(r.left + 1, r.top + 0.5), CPoint(r.right - 1, r.top + 0.5));
    c->setFrameColor(color::kEdgeLo);
    c->drawLine(CPoint(r.left + 1, r.bottom - 0.5), CPoint(r.right - 1, r.bottom - 0.5));
    c->setFrameColor(VSTGUI::CColor(0, 0, 0, 255));
    c->drawLine(CPoint(r.left + 0.5, r.top), CPoint(r.left + 0.5, r.bottom));
    c->drawLine(CPoint(r.right - 0.5, r.top), CPoint(r.right - 0.5, r.bottom));
}

/// Écran (CRT / OLED) : noir, cadre fin, liseré clair.
inline void screen(CDrawContext* c, const CRect& r) {
    c->setFillColor(color::kScreen);
    c->drawRect(r, VSTGUI::kDrawFilled);
    c->setLineWidth(1.0);
    c->setFrameColor(VSTGUI::CColor(255, 255, 255, 13));
    c->drawRect(CRect(r).inset(0.5, 0.5), VSTGUI::kDrawStroked);
}

/// Petit texte gravé (majuscules, gris), comme sur une face avant.
inline void engraved(CDrawContext* c, const std::string& s, const CRect& r, VSTGUI::CHoriTxtAlign align = VSTGUI::kLeftText,
                     const CColor& col = color::kText3) {
    text(c, s, r, col, mono(9.0), align);
}

inline void led(CDrawContext* c, const CPoint& center, bool on, CCoord radius = 3.0) {
    if (on) {
        fillCircle(c, center, radius + 3.0, withAlpha(color::kAccent, 0.22));
        fillCircle(c, center, radius, color::kAccent);
        fillCircle(c, CPoint(center.x - radius * 0.3, center.y - radius * 0.3), radius * 0.35, CColor(255, 236, 190));
    } else {
        fillCircle(c, center, radius, CColor(42, 43, 46));
    }
}

} // namespace omnis::vst3::gui
