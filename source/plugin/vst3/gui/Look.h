// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/gui/Look.h — L'apparence de la console (maquette v11) : face en
//  aluminium anodisé sombre, découpes usinées, écrans noirs, ambre réservé aux
//  signaux actifs. Polices du système (pas de fichier de police à livrer).
// =============================================================================
#pragma once

#include "core/I18n.h"

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

// La palette de la maquette « OMNIS Rack » (l'univers du site abrasion labs) : noir, lignes
// blanches très fines, ambre d'OMNIS, orange chaud pour ce qui alerte. Les anciens noms restent
// (tout le code les emploie) ; leurs valeurs sont celles de la maquette.
namespace color {
inline const CColor kVoid(0, 0, 0);
inline const CColor kMetal(5, 5, 5);
inline const CColor kMetal2(6, 6, 6);
inline const CColor kMetalTop(11, 9, 6);           ///< #0b0906 : une carte choisie
inline const CColor kInset(5, 5, 5);
inline const CColor kScreen(5, 5, 5);
inline const CColor kEdgeHi(255, 255, 255, 31);    ///< --line
inline const CColor kEdgeLo(0, 0, 0, 0);
inline const CColor kLine(255, 255, 255, 31);      ///< --line (12 %)
inline const CColor kLine2(255, 255, 255, 56);     ///< --line-2 (22 %)
inline const CColor kDash(255, 255, 255, 128);     ///< --dash
inline const CColor kText(242, 242, 242);          ///< --ink
inline const CColor kInk2(201, 205, 216);          ///< --ink-2
inline const CColor kChip(230, 230, 230);          ///< texte des puces
inline const CColor kText2(140, 143, 153);         ///< --dim
inline const CColor kText3(92, 95, 104);           ///< --mute
inline const CColor kAccent(255, 170, 0);          ///< --acc
inline const CColor kAccentHot(255, 77, 0);        ///< --hot
inline const CColor kAccent15(255, 170, 0, 31);    ///< --acc-12
inline const CColor kAccent35(255, 170, 0, 140);   ///< ambre à 55 % (liserés d'étiquette)
inline const CColor kGrid(255, 255, 255, 20);
inline const CColor kClip(255, 77, 0);
inline const CColor kGood(110, 196, 140);
inline const CColor kCool(143, 163, 255);          ///< --glow
inline const CColor kDot(255, 255, 255, 36);
inline const CColor kKnobBody(0, 0, 0);
inline const CColor kKnobCap(0, 0, 0);
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

// Les polices de la maquette, livrées dans le paquet (Resources/Fonts, licence OFL) : Space
// Grotesk pour les phrases, Space Mono pour tout le reste.
inline constexpr const char* kSansFace = "Space Grotesk";
inline constexpr const char* kMonoFace = "Space Mono";

inline VSTGUI::SharedPointer<VSTGUI::CFontDesc> sans(CCoord size, bool bold = false) {
    return VSTGUI::makeOwned<VSTGUI::CFontDesc>(kSansFace, size, bold ? VSTGUI::kBoldFace : VSTGUI::kNormalFace);
}
inline VSTGUI::SharedPointer<VSTGUI::CFontDesc> mono(CCoord size, bool bold = false) {
    return VSTGUI::makeOwned<VSTGUI::CFontDesc>(kMonoFace, size, bold ? VSTGUI::kBoldFace : VSTGUI::kNormalFace);
}

/// En capitales, accents compris (UTF-8 : « é » -> « É », « ç » -> « Ç »).
inline std::string upperText(std::string s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto ch = static_cast<unsigned char>(s[i]);
        if (ch >= 'a' && ch <= 'z') s[i] = static_cast<char>(ch - 32);
        else if (ch == 0xC3 && i + 1 < s.size()) {
            const auto nx = static_cast<unsigned char>(s[i + 1]);
            if (nx >= 0xA0 && nx <= 0xBE && nx != 0xB7) s[i + 1] = static_cast<char>(nx - 0x20);
            ++i;
        }
    }
    return s;
}

inline void text(CDrawContext* c, const std::string& s, const CRect& r, const CColor& col,
                 const VSTGUI::SharedPointer<VSTGUI::CFontDesc>& font, VSTGUI::CHoriTxtAlign align = VSTGUI::kLeftText) {
    c->setFont(font);
    c->setFontColor(col);
    c->drawString(VSTGUI::UTF8String(omnis::i18n::tr(s)), r, align, true); // anglais : traduit à l'affichage
}

/// Le texte avec l'interlettrage du CSS de la maquette (`em` : en fraction de la taille).
inline double spacedWidth(CDrawContext* c, const std::string& s, const VSTGUI::SharedPointer<VSTGUI::CFontDesc>& font, double em) {
    const std::string t = omnis::i18n::tr(s);
    c->setFont(font);
    double w = 0.0;
    int n = 0;
    for (std::size_t i = 0; i < t.size();) {
        std::size_t k = 1;
        while (i + k < t.size() && (static_cast<unsigned char>(t[i + k]) & 0xC0) == 0x80) ++k;
        w += c->getStringWidth(VSTGUI::UTF8String(t.substr(i, k)));
        ++n;
        i += k;
    }
    return w + em * font->getSize() * std::max(0, n - 1);
}
inline void spaced(CDrawContext* c, const std::string& s, const CRect& r, const CColor& col, const VSTGUI::SharedPointer<VSTGUI::CFontDesc>& font,
                   double em, VSTGUI::CHoriTxtAlign align = VSTGUI::kLeftText) {
    const std::string t = omnis::i18n::tr(s);
    const double total = spacedWidth(c, s, font, em);
    double x = align == VSTGUI::kCenterText ? r.left + (r.getWidth() - total) / 2 : (align == VSTGUI::kRightText ? r.right - total : r.left);
    c->setFont(font);
    c->setFontColor(col);
    for (std::size_t i = 0; i < t.size();) {
        std::size_t k = 1;
        while (i + k < t.size() && (static_cast<unsigned char>(t[i + k]) & 0xC0) == 0x80) ++k;
        const VSTGUI::UTF8String g(t.substr(i, k));
        const double gw = c->getStringWidth(g);
        if (x + gw > r.right + 0.5 && align == VSTGUI::kLeftText) break; // pas au-delà de la zone
        c->drawString(g, CRect(x, r.top, x + gw + 2, r.bottom), VSTGUI::kLeftText, true);
        x += gw + em * font->getSize();
        i += k;
    }
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

/// Un fond de la maquette : presque noir, liseré fin (--line).
inline void inset(CDrawContext* c, const CRect& r, const CColor& fill = color::kInset) {
    c->setFillColor(fill);
    c->drawRect(r, VSTGUI::kDrawFilled);
    c->setLineWidth(1.0);
    c->setFrameColor(color::kLine);
    c->drawRect(CRect(r).inset(0.5, 0.5), VSTGUI::kDrawStroked);
}

/// Un écran (`.stage`) : #050505, liseré fin.
inline void screen(CDrawContext* c, const CRect& r) {
    c->setFillColor(color::kScreen);
    c->drawRect(r, VSTGUI::kDrawFilled);
    c->setLineWidth(1.0);
    c->setFrameColor(color::kLine);
    c->drawRect(CRect(r).inset(0.5, 0.5), VSTGUI::kDrawStroked);
}

/// Petit texte gravé (majuscules, gris), comme sur une face avant.
inline void engraved(CDrawContext* c, const std::string& s, const CRect& r, VSTGUI::CHoriTxtAlign align = VSTGUI::kLeftText,
                     const CColor& col = color::kText3) {
    text(c, s, r, col, mono(9.0), align);
}

/// Les crochets d'angle de la maquette (14 px, posés 6 px hors du cadre, `push` : écartés au survol).
inline void brackets(CDrawContext* c, const CRect& r, const CColor& col, double push = 0.0, double len = 14.0, double off = 6.0) {
    c->setLineWidth(1.0);
    c->setFrameColor(col);
    const double l = r.left - off - push + 0.5, t = r.top - off - push + 0.5, rr = r.right + off + push - 0.5, b = r.bottom + off + push - 0.5;
    c->drawLine(CPoint(l, t), CPoint(l + len, t));
    c->drawLine(CPoint(l, t), CPoint(l, t + len));
    c->drawLine(CPoint(rr, t), CPoint(rr - len, t));
    c->drawLine(CPoint(rr, t), CPoint(rr, t + len));
    c->drawLine(CPoint(l, b), CPoint(l + len, b));
    c->drawLine(CPoint(l, b), CPoint(l, b - len));
    c->drawLine(CPoint(rr, b), CPoint(rr - len, b));
    c->drawLine(CPoint(rr, b), CPoint(rr, b - len));
}

/// Une étiquette de la maquette (`.tag`) : capitales espacées, liseré à 55 %.
inline void tagBox(CDrawContext* c, const CRect& r, const std::string& s, const CColor& col, double size = 10.6) {
    c->setLineWidth(1.0);
    c->setFrameColor(withAlpha(col, 0.55 * col.alpha / 255.0));
    c->drawRect(CRect(r).inset(0.5, 0.5), VSTGUI::kDrawStroked);
    spaced(c, s, r, col, mono(size), 0.18, VSTGUI::kCenterText);
}

/// Un bouton de la maquette (`.btn`) : plein, coins coupés à 45°.
inline void cutFill(CDrawContext* c, const CRect& r, const CColor& fill, double cut = 8.0) {
    auto path = VSTGUI::owned(c->createGraphicsPath());
    if (!path) return;
    path->beginSubpath(CPoint(r.left, r.top + cut));
    path->addLine(CPoint(r.left + cut, r.top));
    path->addLine(CPoint(r.right - cut, r.top));
    path->addLine(CPoint(r.right, r.top + cut));
    path->addLine(CPoint(r.right, r.bottom - cut));
    path->addLine(CPoint(r.right - cut, r.bottom));
    path->addLine(CPoint(r.left + cut, r.bottom));
    path->addLine(CPoint(r.left, r.bottom - cut));
    path->closeSubpath();
    c->setFillColor(fill);
    c->drawGraphicsPath(path, CDrawContext::kPathFilled);
}

/// Le petit carré ambre des titres de la maquette (`.eyebrow i`).
inline void eyebrowMark(CDrawContext* c, double x, double cy) {
    c->setLineWidth(1.0);
    c->setFrameColor(color::kAccent);
    c->drawRect(CRect(x + 0.5, cy - 2.5, x + 6.5, cy + 3.5), VSTGUI::kDrawStroked);
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
