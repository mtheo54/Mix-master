// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/RefBrowser.inc — La fenêtre RÉF : tous les genres, une illustration chacun.
//
//  Inclus par OmnisEditor.cpp (mêmes aides de dessin). Familles en onglets, puis une
//  tuile par sous-genre : son illustration (dessinée par le code : aucune image livrée),
//  son profil mesuré (ou « repère : la famille »), ses artistes connus. À droite, la
//  fiche du genre choisi : UTILISER CE GENRE, et tes propres références de ce style.
// =============================================================================

namespace {

/// La teinte de chaque famille (même ordre que data/genres_classement.txt).
double familyHue(const std::string& family) {
    const std::string f = ai::normalizeText(family);
    if (f.find("francais") != std::string::npos) return 14.0;
    if (f.find("international") != std::string::npos || f.find("us/uk") != std::string::npos) return 212.0;
    if (f.find("electro") != std::string::npos) return 145.0;
    if (f.find("pop") != std::string::npos) return 285.0;
    if (f.find("rock") != std::string::npos) return 38.0;
    if (f.find("anime") != std::string::npos || f.find("b.o") != std::string::npos) return 180.0;
    if (f.find("hyperpop") != std::string::npos) return 325.0;
    return 0.0;
}

/// Le nom court d'une famille, pour les onglets.
std::string shortFamily(const std::string& family) {
    const std::string f = ai::normalizeText(family);
    if (f.find("francais") != std::string::npos) return "Rap FR";
    if (f.find("international") != std::string::npos) return "Rap US / UK";
    if (f.find("electro") != std::string::npos) return "Électro";
    if (f.find("rock") != std::string::npos) return "Rock / Metal";
    if (f.find("hyperpop") != std::string::npos) return "Hyperpop";
    if (f.find("pop") != std::string::npos) return "Pop / R&B";
    if (f.find("anime") != std::string::npos) return "B.O. / Anime";
    return family;
}

CColor hsv(double h, double s, double v, double a = 1.0) {
    CColor c;
    c.fromHSV(std::fmod(h + 360.0, 360.0), std::clamp(s, 0.0, 1.0), std::clamp(v, 0.0, 1.0));
    return withAlpha(c, a);
}

enum class Motif { Glide, Hats, Vinyl, Clipped, Bubbles, Bars, Bolt, Sun, Pixels, Waves, Haze, Shards };

/// Le motif d'un genre : ce qu'on entend de lui, dessiné (la drill glisse, la trap tape en
/// triolets, le boom bap tourne sur un vinyle, la rage est écrêtée…).
Motif motifOf(const std::string& family, const std::string& style) {
    const std::string s = ai::normalizeText(style.empty() ? family : style);
    const auto has = [&](std::initializer_list<const char*> words) {
        for (const char* w : words)
            if (s.find(w) != std::string::npos) return true;
        return false;
    };
    if (has({"french touch", "disco", "italo"})) return Motif::Vinyl;
    if (has({"dubstep", "brostep", "riddim", "bass music"})) return Motif::Clipped;
    if (has({"trance", "eurodance", "tropical"})) return Motif::Waves;
    if (has({"techno", "idm", "tech house", "gabber", "hardstyle"})) return Motif::Pixels;
    if (has({"rage", "hard clipping", "hyperpop", "digicore", "opium", "phonk", "dark trap", "horror", "punk rap", "rap metal"})) return Motif::Clipped;
    if (has({"drill"})) return Motif::Glide;
    if (has({"ost", "anime", "jeu video", "film", "chiptune", "b.o"})) return Motif::Pixels;
    if (has({"ambient", "shoegaze", "downtempo", "trip-hop", "lo-fi", "dream", "chillwave", "witch"})) return Motif::Haze;
    if (has({"boom bap", "old school", "jazz", "conscient", "kickeur", "east coast", "griselda", "soul", "g-funk", "west coast", "abstrait"}))
        return Motif::Vinyl;
    if (has({"plugg", "cloud", "new wave", "melodique", "r&b", "rnb", "emo", "psych", "bedroom"})) return Motif::Bubbles;
    if (has({"afro", "dancehall", "reggaeton", "latin", "baile", "carioca", "moombah", "festif", "cumbia"})) return Motif::Waves;
    if (has({"house", "techno", "trance", "dance", "electro", "disco", "french touch", "dubstep", "bass", "garage", "gabber", "rave", "edm",
             "funk", "big room", "breakbeat", "synthwave", "big beat"}))
        return Motif::Bars;
    if (has({"rock", "metal", "grunge", "punk", "hardcore", "doom", "thrash"})) return Motif::Bolt;
    if (has({"pop", "indie", "chanson", "k-pop", "britpop", "city"})) return Motif::Sun;
    if (has({"trap", "dirty south", "gangsta", "mumble", "pop rap", "rap"})) return Motif::Hats;
    // Une famille sans mot reconnu : son motif.
    const double h = familyHue(family);
    if (h == 145.0) return Motif::Bars;
    if (h == 285.0) return Motif::Sun;
    if (h == 38.0) return Motif::Bolt;
    if (h == 180.0) return Motif::Pixels;
    if (h == 325.0) return Motif::Clipped;
    return Motif::Shards;
}

/// L'illustration d'un genre. Fond : la couleur de sa famille ; motif : son caractère ;
/// silhouette : la courbe de son profil mesuré (deux genres mesurés ne se ressemblent pas).
void paintGenreArt(CDrawContext* c, const CRect& r, const std::string& family, const std::string& style, const analysis::SpectralSignature& sig,
                   bool measured) {
    std::uint32_t seed = 2166136261u;
    for (unsigned char ch : style + "|" + family) seed = (seed ^ ch) * 16777619u;
    const auto rnd = [&seed] { // pseudo-hasard déterministe
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return (seed & 0xFFFFFF) / static_cast<double>(0xFFFFFF);
    };
    const double hue = familyHue(family) + (style.empty() ? 0.0 : (rnd() - 0.5) * 46.0);
    CRect clip;
    c->getClipRect(clip);
    CRect inside = r;
    inside.bound(clip);
    c->setClipRect(inside);
    // Fond en dégradé (bandes : même rendu sous Windows, macOS et Linux).
    const int strips = 18;
    for (int i = 0; i < strips; ++i) {
        const double t = i / (strips - 1.0);
        c->setFillColor(hsv(hue + 22.0 * t, 0.66 - 0.14 * t, 0.46 - 0.24 * t));
        c->drawRect(CRect(r.left, r.top + r.getHeight() * i / strips, r.right, r.top + r.getHeight() * (i + 1) / strips + 1), kDrawFilled);
    }
    const CColor ink = hsv(hue + 24.0, 0.70, 1.0, 0.85), soft = hsv(hue - 10.0, 0.35, 0.95, 0.22), hot = hsv(hue + 40.0, 0.85, 1.0, 0.95);
    const double w = r.getWidth(), h = r.getHeight();
    const auto P = [&](double x, double y) { return CPoint(r.left + x * w, r.top + y * h); };
    c->setLineStyle(kLineSolid);
    switch (motifOf(family, style)) {
    case Motif::Glide: // la 808 qui glisse d'une note à l'autre
        c->setLineWidth(std::max(2.0, h * 0.035));
        for (int i = -3; i < 9; ++i) {
            const double x0 = i * 0.16 + rnd() * 0.04;
            c->setFrameColor(i % 3 == 0 ? ink : soft);
            c->drawLine(P(x0, 1.05), P(x0 + 0.42, -0.05));
        }
        break;
    case Motif::Hats: { // hi-hats en triolets
        const int rows = 3;
        for (int row = 0; row < rows; ++row)
            for (int k = 0; k < 12; ++k) {
                const bool accent = k % 3 == 0;
                fillCircle(c, P(0.08 + k * 0.078, 0.22 + row * 0.16), h * (accent ? 0.035 : 0.022), accent && row == 1 ? hot : soft);
            }
        break;
    }
    case Motif::Vinyl: {
        const CPoint center = P(0.72, 0.42);
        for (int k = 1; k <= 9; ++k) strokeCircle(c, center, h * 0.06 * k, k % 3 == 0 ? ink : soft, k % 3 == 0 ? 1.6 : 1.0);
        fillCircle(c, center, h * 0.07, hot);
        break;
    }
    case Motif::Clipped: { // onde écrêtée : les crêtes coupées net
        if (auto path = VSTGUI::owned(c->createGraphicsPath())) {
            path->beginSubpath(P(0.0, 0.45));
            for (int k = 0; k <= 40; ++k) {
                const double x = k / 40.0, v = std::sin(x * 22.0 + rnd() * 0.6) * (0.6 + 0.6 * rnd());
                path->addLine(P(x, 0.45 - 0.26 * std::clamp(v, -0.55, 0.55) / 0.55));
            }
            c->setLineWidth(std::max(2.0, h * 0.03));
            c->setFrameColor(hot);
            c->drawGraphicsPath(path, CDrawContext::kPathStroked);
        }
        c->setFrameColor(soft);
        c->setLineWidth(1.0);
        c->drawLine(P(0.0, 0.19), P(1.0, 0.19));
        c->drawLine(P(0.0, 0.71), P(1.0, 0.71));
        break;
    }
    case Motif::Bubbles:
        for (int k = 0; k < 14; ++k) {
            const CPoint p = P(rnd(), rnd() * 0.7);
            const double rad = h * (0.03 + 0.09 * rnd());
            fillCircle(c, p, rad, k % 4 == 0 ? withAlpha(hot, 0.55) : soft);
        }
        for (int k = 0; k < 5; ++k) { // éclats de cloches
            const CPoint p = P(rnd(), rnd() * 0.6);
            c->setFrameColor(ink);
            c->setLineWidth(1.2);
            c->drawLine(CPoint(p.x - 4, p.y), CPoint(p.x + 4, p.y));
            c->drawLine(CPoint(p.x, p.y - 4), CPoint(p.x, p.y + 4));
        }
        break;
    case Motif::Bars: // les quatre temps, et un égaliseur
        for (int k = 0; k < 14; ++k) {
            const double x = 0.05 + k * 0.066, top = 0.15 + 0.5 * rnd();
            c->setFillColor(k % 4 == 0 ? hot : soft);
            const CPoint a = P(x, top), b = P(x + 0.04, 0.72);
            c->drawRect(CRect(a.x, a.y, b.x, b.y), kDrawFilled);
        }
        break;
    case Motif::Bolt:
        if (auto path = VSTGUI::owned(c->createGraphicsPath())) {
            path->beginSubpath(P(0.62, 0.02));
            path->addLine(P(0.40, 0.40));
            path->addLine(P(0.55, 0.40));
            path->addLine(P(0.36, 0.80));
            path->addLine(P(0.70, 0.32));
            path->addLine(P(0.54, 0.32));
            path->addLine(P(0.70, 0.02));
            path->closeSubpath();
            c->setFillColor(hot);
            c->drawGraphicsPath(path, CDrawContext::kPathFilled);
        }
        for (int k = 0; k < 9; ++k) {
            c->setFrameColor(soft);
            c->setLineWidth(1.0);
            c->drawLine(P(k * 0.12, 0.74), P(k * 0.12 + 0.06, 0.56));
        }
        break;
    case Motif::Sun: {
        const CPoint center = P(0.5, 0.48);
        c->setLineWidth(1.4);
        for (int k = 0; k < 24; ++k) {
            const double a = k * 2.0 * std::numbers::pi / 24.0;
            c->setFrameColor(k % 2 ? soft : ink);
            c->drawLine(CPoint(center.x + std::cos(a) * h * 0.2, center.y + std::sin(a) * h * 0.2),
                        CPoint(center.x + std::cos(a) * w * 0.7, center.y + std::sin(a) * w * 0.7));
        }
        fillCircle(c, center, h * 0.17, hot);
        break;
    }
    case Motif::Pixels: {
        const double cell = std::max(4.0, h / 11.0);
        for (double y = r.top; y < r.top + h * 0.75; y += cell)
            for (double x = r.left; x < r.right; x += cell) {
                const double v = rnd();
                if (v < 0.55) continue;
                c->setFillColor(v > 0.9 ? hot : v > 0.75 ? withAlpha(ink, 0.5) : soft);
                c->drawRect(CRect(x + 1, y + 1, x + cell - 1, y + cell - 1), kDrawFilled);
            }
        break;
    }
    case Motif::Waves:
        for (int line = 0; line < 5; ++line)
            if (auto path = VSTGUI::owned(c->createGraphicsPath())) {
                const double phase = rnd() * 6.0, y0 = 0.16 + line * 0.12;
                path->beginSubpath(P(0.0, y0));
                for (int k = 1; k <= 40; ++k) path->addLine(P(k / 40.0, y0 + 0.05 * std::sin(k * 0.55 + phase)));
                c->setLineWidth(line == 2 ? 2.2 : 1.2);
                c->setFrameColor(line == 2 ? hot : soft);
                c->drawGraphicsPath(path, CDrawContext::kPathStroked);
            }
        break;
    case Motif::Haze:
        for (int k = 0; k < 6; ++k) {
            c->setFillColor(hsv(hue + k * 8.0, 0.30, 0.9, 0.08 + 0.03 * k));
            const CPoint a = P(0.0, 0.12 + k * 0.09), b = P(1.0, 0.18 + k * 0.09);
            c->drawRect(CRect(a.x, a.y, b.x, b.y), kDrawFilled);
        }
        fillCircle(c, P(0.76, 0.26), h * 0.12, withAlpha(hot, 0.7));
        break;
    case Motif::Shards:
        for (int k = 0; k < 7; ++k)
            if (auto path = VSTGUI::owned(c->createGraphicsPath())) {
                path->beginSubpath(P(rnd(), rnd() * 0.7));
                path->addLine(P(rnd(), rnd() * 0.7));
                path->addLine(P(rnd(), rnd() * 0.7));
                path->closeSubpath();
                c->setFillColor(k == 0 ? withAlpha(hot, 0.6) : soft);
                c->drawGraphicsPath(path, CDrawContext::kPathFilled);
            }
        break;
    }
    // La silhouette du profil mesuré, en bas (sa courbe Mid) ; sans profil, une ligne pointillée.
    const double base = r.bottom - h * 0.06;
    if (measured && !sig.midDb.empty()) {
        if (auto path = VSTGUI::owned(c->createGraphicsPath())) {
            const double top = *std::max_element(sig.midDb.begin(), sig.midDb.end());
            const int n = static_cast<int>(sig.midDb.size());
            path->beginSubpath(CPoint(r.left, base));
            for (int k = 0; k < n; ++k) {
                const double v = std::clamp((sig.midDb[static_cast<std::size_t>(k)] - (top - 30.0)) / 30.0, 0.03, 1.0);
                path->addLine(CPoint(r.left + w * (k + 0.5) / n, base - h * 0.30 * v));
            }
            path->addLine(CPoint(r.right, base));
            path->closeSubpath();
            c->setFillColor(hsv(hue, 0.15, 0.08, 0.78));
            c->drawGraphicsPath(path, CDrawContext::kPathFilled);
        }
    } else {
        c->setFrameColor(withAlpha(color::kText2, 0.35));
        c->setLineWidth(1.0);
        for (double x = r.left + 4; x < r.right - 4; x += 8) c->drawLine(CPoint(x, base - 6), CPoint(x + 4, base - 6));
    }
    c->setClipRect(clip);
    c->setFrameColor(color::kEdgeLo);
    c->drawRect(r, kDrawStroked);
}

/// Une chaîne de format traduite, puis remplie (« profil · %d titres »).
std::string trf(const char* format, int a, int b = 0) {
    char out[200];
    std::snprintf(out, sizeof out, i18n::tr(format).c_str(), a, b);
    return out;
}

/// Des mots sur plusieurs lignes, coupés à la largeur (« a · b · c »).
std::vector<std::string> wrapWords(CDrawContext* c, const std::vector<std::string>& words, double width, std::size_t maxLines) {
    std::vector<std::string> lines;
    std::string line;
    for (const auto& w : words) {
        const std::string next = line.empty() ? w : line + " · " + w;
        if (!line.empty() && c->getStringWidth(next.c_str()) > width) {
            lines.push_back(line);
            if (lines.size() == maxLines) return lines;
            line = w;
        } else {
            line = next;
        }
    }
    if (!line.empty() && lines.size() < maxLines) lines.push_back(line);
    return lines;
}

} // namespace

class RefBrowser : public CView {
public:
    RefBrowser(const CRect& r, OmnisEditor& editor) : CView(r), editor_(editor) { setVisible(false); }

    /// `family` : l'onglet ouvert (vide : tous) ; `style` : la fiche montrée (outil de captures).
    void open(const std::string& family = {}, const std::string& style = {}) {
        auto* oc = editor_.omnis();
        cards_ = oc ? oc->genreCatalog() : std::vector<OmnisController::GenreCard>{};
        families_.clear();
        for (const auto& g : cards_)
            if (g.style.empty()) families_.push_back(g.family);
        // Le genre déjà visé, s'il y en a un, est montré d'abord.
        selected_ = -1;
        if (oc && oc->referenceKind() == OmnisController::RefKind::Style)
            for (std::size_t i = 0; i < cards_.size(); ++i)
                if ((cards_[i].style.empty() ? cards_[i].family : cards_[i].style) == oc->referenceName()) {
                    selected_ = static_cast<int>(i);
                    family_ = cards_[i].family;
                }
        if (!family.empty()) {
            family_ = family;
            for (std::size_t i = 0; i < cards_.size(); ++i)
                if (cards_[i].family == family && cards_[i].style == style) selected_ = static_cast<int>(i);
        }
        scroll_ = 0.0;
        refreshMine();
        setVisible(true);
        invalid();
    }
    void close() {
        setVisible(false);
        invalid();
    }

    void draw(CDrawContext* c) override {
        c->setDrawMode(kAntiAliasing | kNonIntegralMode);
        hits_.clear();
        const CRect all = getViewSize();
        c->setFillColor(CColor(0, 0, 0, 200));
        c->drawRect(all, kDrawFilled);
        panel_ = CRect(24, 18, all.right - 24, all.bottom - 18);
        inset(c, panel_, color::kMetal);
        c->setFrameColor(color::kAccent35);
        c->drawRect(CRect(panel_).inset(0.5, 0.5), kDrawStroked);
        text(c, "RÉFÉRENCES", CRect(panel_.left + 22, panel_.top + 14, panel_.left + 300, panel_.top + 38), color::kAccent, sans(19, true));
        text(c, "Choisis un genre : OMNIS règle ta prod vers son profil mesuré. Sans profil, il vise sa famille.",
             CRect(panel_.left + 22, panel_.top + 40, panel_.left + 760, panel_.top + 56), color::kText2, mono(10));
        // Les actions, en haut à droite.
        double x = panel_.right - 18;
        const auto action = [&](const std::string& label, bool accent, std::function<void()> f, double width) {
            const CRect b(x - width, panel_.top + 16, x, panel_.top + 42);
            inset(c, b, CColor(34, 35, 39));
            text(c, label, b, accent ? color::kAccent : color::kText2, mono(10, accent), kCenterText);
            hits_.push_back({b, std::move(f)});
            x = b.left - 8;
        };
        auto* oc = editor_.omnis();
        action("×", true, [this] { close(); }, 34);
        action(oc && oc->onlineRecognition() ? "MUSICBRAINZ : OUI" : "MUSICBRAINZ : NON", false, [this] {
            if (auto* o = editor_.omnis()) {
                const bool on = !o->onlineRecognition();
                o->setOnlineRecognition(on);
                editor_.answer_ = on ? "Recherche MusicBrainz activée : une requête par fichier ajouté, rien d'autre ne va sur internet."
                                     : "Recherche MusicBrainz désactivée : OMNIS reconnaît les fichiers hors ligne (étiquettes, nom « Artiste - Titre »).";
            }
            invalid();
        }, 150);
        action("+ AJOUTER UN FICHIER", false, [this] {
            close();
            editor_.pickReferenceFile();
        }, 170);
        action("AUCUNE", false, [this] {
            if (auto* o = editor_.omnis()) editor_.answer_ = o->chooseReference(OmnisController::RefKind::None, {});
            close();
        }, 90);
        // Les familles, en onglets.
        double fx = panel_.left + 22;
        const double fy = panel_.top + 66;
        const auto tab = [&](const std::string& label, const std::string& fam, double hue) {
            c->setFont(mono(10.5, true));
            const double width = c->getStringWidth(i18n::tr(label).c_str()) + (fam.empty() ? 22 : 36);
            const CRect b(fx, fy, fx + width, fy + 26);
            const bool on = family_ == fam;
            inset(c, b, on ? CColor(44, 45, 50) : CColor(26, 27, 31));
            if (on) {
                c->setFrameColor(color::kAccent);
                c->setLineWidth(1.0);
                c->drawLine(CPoint(b.left + 1, b.bottom - 1.5), CPoint(b.right - 1, b.bottom - 1.5));
            }
            if (!fam.empty()) fillCircle(c, CPoint(b.left + 13, b.getCenter().y), 5, hsv(hue, 0.65, 0.95));
            text(c, label, CRect(b.left + (fam.empty() ? 11 : 24), b.top, b.right - 6, b.bottom), on ? color::kText : color::kText2, mono(10.5, on));
            hits_.push_back({b, [this, fam] {
                                 family_ = fam;
                                 scroll_ = 0.0;
                                 invalid();
                             }});
            fx = b.right + 6;
        };
        tab("TOUS", {}, 0.0);
        for (const auto& f : families_) tab(shortFamily(f), f, familyHue(f));
        // La grille des genres (défile à la molette).
        grid_ = CRect(panel_.left + 22, fy + 38, panel_.right - 316, panel_.bottom - 16);
        detail_ = CRect(grid_.right + 14, grid_.top, panel_.right - 18, grid_.bottom);
        drawGrid(c);
        drawDetail(c);
        setDirty(false);
    }

    void onMouseDownEvent(MouseDownEvent& e) override {
        e.consumed = true; // la fenêtre est modale
        e.ignoreFollowUpMoveAndUpEvents(true);
        if (!panel_.pointInside(e.mousePosition)) {
            close();
            return;
        }
        for (const auto& [r, f] : hits_)
            if (r.pointInside(e.mousePosition)) {
                f();
                return;
            }
    }
    void onMouseMoveEvent(MouseMoveEvent& e) override {
        int over = -1;
        for (std::size_t i = 0; i < tiles_.size(); ++i)
            if (tiles_[i].first.pointInside(e.mousePosition)) over = tiles_[i].second;
        if (over != hover_) {
            hover_ = over;
            invalid();
        }
        e.consumed = true;
    }
    void onMouseWheelEvent(MouseWheelEvent& e) override {
        if (!grid_.pointInside(e.mousePosition)) return;
        scroll_ = std::clamp(scroll_ - e.deltaY * 40.0, 0.0, std::max(0.0, contentHeight_ - grid_.getHeight()));
        e.consumed = true;
        invalid();
    }

private:
    static constexpr double kTileW = 150.0, kTileH = 134.0, kGap = 12.0;

    void drawGrid(CDrawContext* c) {
        tiles_.clear();
        CRect clip;
        c->getClipRect(clip);
        c->setClipRect(grid_);
        const int cols = std::max(1, static_cast<int>((grid_.getWidth() + kGap) / (kTileW + kGap)));
        double y = grid_.top - scroll_;
        std::string current;
        int col = 0;
        auto* oc = editor_.omnis();
        const std::string aimed = oc && oc->referenceKind() == OmnisController::RefKind::Style ? oc->referenceName() : std::string();
        for (std::size_t i = 0; i < cards_.size(); ++i) {
            const auto& g = cards_[i];
            if (!family_.empty() && g.family != family_) continue;
            if (g.family != current) { // en-tête de la famille
                if (col) y += kTileH + kGap;
                col = 0;
                current = g.family;
                const CRect head(grid_.left, y, grid_.right, y + 26);
                text(c, g.family, CRect(head.left, head.top, head.right - 220, head.bottom), hsv(familyHue(g.family), 0.55, 1.0), sans(13, true));
                int styles = 0;
                for (const auto& x : cards_)
                    if (x.family == g.family && !x.style.empty()) ++styles;
                text(c, trf("%d titres mesurés · %d styles", g.titles, styles),
                     CRect(head.right - 260, head.top, head.right - 4, head.bottom), color::kText3, mono(9.5), kRightText);
                y += 30;
            }
            const double x = grid_.left + col * (kTileW + kGap);
            const CRect tile(x, y, x + kTileW, y + kTileH);
            if (tile.bottom >= grid_.top && tile.top <= grid_.bottom) drawTile(c, tile, g, static_cast<int>(i), aimed);
            tiles_.push_back({tile, static_cast<int>(i)});
            if (++col == cols) {
                col = 0;
                y += kTileH + kGap;
            }
        }
        if (col) y += kTileH + kGap;
        contentHeight_ = y + scroll_ - grid_.top;
        // Barre de défilement.
        if (contentHeight_ > grid_.getHeight()) {
            const double k = grid_.getHeight() / contentHeight_;
            const double top = grid_.top + scroll_ * k;
            c->setFillColor(withAlpha(color::kText2, 0.35));
            c->drawRect(CRect(grid_.right - 4, top, grid_.right, top + grid_.getHeight() * k), kDrawFilled);
        }
        c->setClipRect(clip);
        for (const auto& [r, i] : tiles_)
            if (r.bottom > grid_.top && r.top < grid_.bottom) hits_.push_back({r, [this, i = i] {
                                                                                   selected_ = i;
                                                                                   refreshMine();
                                                                                   invalid();
                                                                               }});
    }

    void drawTile(CDrawContext* c, const CRect& tile, const OmnisController::GenreCard& g, int index, const std::string& aimed) {
        inset(c, tile, index == hover_ ? CColor(34, 35, 40) : CColor(24, 25, 29));
        const CRect art(tile.left + 5, tile.top + 5, tile.right - 5, tile.top + 82);
        paintGenreArt(c, art, g.family, g.style, g.signature, g.measured());
        const std::string name = g.style.empty() ? "Toute la famille" : g.style;
        text(c, name, CRect(tile.left + 8, art.bottom + 5, tile.right - 6, art.bottom + 21), color::kText, mono(10.5, true));
        text(c, g.measured() ? trf("profil · %d titres", g.titles) : std::string("repère : la famille"),
             CRect(tile.left + 8, art.bottom + 22, tile.right - 6, art.bottom + 35), g.measured() ? color::kAccent : color::kText3, mono(9));
        if (!g.artists.empty())
            text(c, trf(g.artists.size() > 1 ? "%d artistes" : "%d artiste", static_cast<int>(g.artists.size())),
                 CRect(tile.left + 8, art.bottom + 35, tile.right - 6, art.bottom + 47), color::kText3, mono(9));
        if ((g.style.empty() ? g.family : g.style) == aimed && g.measured()) { // le genre visé
            const CRect badge(art.right - 50, art.top + 4, art.right - 4, art.top + 18);
            c->setFillColor(color::kAccent);
            c->drawRect(badge, kDrawFilled);
            text(c, "VISÉ", badge, CColor(10, 10, 10), mono(8.5, true), kCenterText);
        }
        if (index == selected_) {
            c->setFrameColor(color::kAccent);
            c->setLineWidth(2.0);
            c->drawRect(CRect(tile).inset(1, 1), kDrawStroked);
            c->setLineWidth(1.0);
        } else if (index == hover_) {
            c->setFrameColor(withAlpha(color::kText2, 0.6));
            c->drawRect(CRect(tile).inset(0.5, 0.5), kDrawStroked);
        }
    }

    void drawDetail(CDrawContext* c) {
        inset(c, detail_, CColor(17, 18, 21));
        const CRect d = CRect(detail_).inset(14, 14);
        if (selected_ < 0 || selected_ >= static_cast<int>(cards_.size())) {
            text(c, "Clique un genre", CRect(d.left, d.top + 4, d.right, d.top + 24), color::kText, sans(14, true));
            text(c, "pour voir son profil, ses artistes", CRect(d.left, d.top + 30, d.right, d.top + 44), color::kText2, mono(10));
            text(c, "et tes références de ce style.", CRect(d.left, d.top + 46, d.right, d.top + 60), color::kText2, mono(10));
            return;
        }
        const auto& g = cards_[static_cast<std::size_t>(selected_)];
        const CRect art(d.left, d.top, d.right, d.top + 150);
        paintGenreArt(c, art, g.family, g.style, g.signature, g.measured());
        double y = art.bottom + 10;
        text(c, g.style.empty() ? g.family : g.style, CRect(d.left, y, d.right, y + 22), color::kText, sans(15, true));
        y += 24;
        if (!g.style.empty()) {
            text(c, g.family, CRect(d.left, y, d.right, y + 14), hsv(familyHue(g.family), 0.55, 1.0), mono(9.5));
            y += 16;
        }
        text(c, g.measured() ? trf("Profil mesuré sur %d titres", g.titles) : std::string("Pas encore de profil mesuré : OMNIS vise la famille."),
             CRect(d.left, y, d.right, y + 14), g.measured() ? color::kAccent : color::kText3, mono(9.5));
        y += 22;
        const CRect use(d.left, y, d.right, y + 30);
        inset(c, use, CColor(48, 38, 18));
        c->setFrameColor(color::kAccent);
        c->drawRect(CRect(use).inset(0.5, 0.5), kDrawStroked);
        text(c, "UTILISER CE GENRE", use, color::kAccent, mono(11, true), kCenterText);
        hits_.push_back({use, [this, target = g.target, style = g.style, measured = g.measured()] {
                             if (auto* o = editor_.omnis()) {
                                 std::string said = o->chooseReference(OmnisController::RefKind::Style, target);
                                 if (!measured && !style.empty())
                                     said = i18n::tr("Pas encore de profil pour ") + style + i18n::tr(" : je vise sa famille. ") + said;
                                 editor_.answer_ = said;
                             }
                             close();
                         }});
        y = use.bottom + 14;
        if (!g.artists.empty()) {
            engraved(c, "ARTISTES DE CE STYLE", CRect(d.left, y, d.right, y + 12));
            y += 15;
            c->setFont(mono(9.5));
            for (const auto& line : wrapWords(c, g.artists, d.getWidth(), 5)) {
                text(c, line, CRect(d.left, y, d.right, y + 13), color::kText2, mono(9.5));
                y += 13;
            }
            y += 8;
        }
        engraved(c, g.measured() || g.style.empty() ? (g.style.empty() ? "TES RÉFÉRENCES DE LA FAMILLE" : "TES RÉFÉRENCES DE CE STYLE") : "TES RÉFÉRENCES DE LA FAMILLE",
                 CRect(d.left, y, d.right, y + 12));
        y += 16;
        if (mine_.empty()) {
            text(c, "Aucune pour l'instant :", CRect(d.left, y, d.right, y + 13), color::kText3, mono(9.5));
            text(c, "ajoute un fichier « Artiste - Titre ».", CRect(d.left, y + 14, d.right, y + 27), color::kText3, mono(9.5));
            return;
        }
        for (const auto& t : mine_) {
            if (y + 20 > d.bottom) break;
            const CRect row(d.left, y, d.right, y + 20);
            editor_.paintCover(c, CRect(row.left, row.top + 2, row.left + 16, row.top + 18), t.cover, t.signature, t.name);
            text(c, t.name, CRect(row.left + 22, row.top, row.right, row.bottom), color::kText, mono(9.5));
            hits_.push_back({row, [this, name = t.name] {
                                 if (auto* o = editor_.omnis()) editor_.answer_ = o->chooseReference(OmnisController::RefKind::Title, name);
                                 close();
                             }});
            y += 22;
        }
    }

    /// Les références de la personne (pas les profils livrés) dans le genre choisi.
    void refreshMine() {
        mine_.clear();
        auto* oc = editor_.omnis();
        if (!oc || selected_ < 0 || selected_ >= static_cast<int>(cards_.size())) return;
        const auto& g = cards_[static_cast<std::size_t>(selected_)];
        const auto tree = oc->referenceTree(g.family, g.measured() ? g.style : std::string());
        for (const auto& a : tree.artists) {
            if (a.name == ai::kProfileArtist) continue;
            for (const auto& t : a.titles) {
                mine_.push_back(t);
                if (mine_.size() >= 12) return;
            }
        }
    }

    OmnisEditor& editor_;
    std::vector<OmnisController::GenreCard> cards_;
    std::vector<std::string> families_;
    std::vector<OmnisController::ReferenceTitle> mine_;
    std::string family_;
    int selected_ = -1, hover_ = -1;
    double scroll_ = 0.0, contentHeight_ = 0.0;
    CRect panel_, grid_, detail_;
    std::vector<std::pair<CRect, std::function<void()>>> hits_;
    std::vector<std::pair<CRect, int>> tiles_;
};
