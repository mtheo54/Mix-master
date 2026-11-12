// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisEditor.h — La console OMNIS (VSTGUI, dessinée en code),
//  d'après la maquette console v11 :
//
//   ┌───────────────────────────────────────────────┬──────────┐
//   │ ruban 1 : marque, AUTO, A / B, bypass, Match,  │  MASTER  │
//   │           Delta, écoute, aide, latence         │  In/Out  │
//   │ ruban 2 : la demande à l'IA                    │ bargraph │
//   │ rack : 8 modules au format 500 (gros bouton,   │ mesures  │
//   │        voyant, état ; glisser pour réordonner) │ (verdict │
//   │ tiroir : TOUS les réglages du module choisi    │ en mots) │
//   │          | écran du module (courbe, GR…)       │ écoute,  │
//   │ écrans : analyseur de sortie | goniomètre      │ export   │
//   └───────────────────────────────────────────────┴──────────┘
//
//  Les commandes écrivent les paramètres par le contrôleur (beginEdit /
//  performEdit / endEdit : automation et annulation de l'hôte). Une minuterie
//  (30 Hz) relit le flux du processeur (UiFeed) et redessine ce qui a changé.
//
//  buildInto() et tick() sont publics : l'outil omnis_guishot rend la console
//  hors écran (PNG) pour la vérifier sans DAW.
// =============================================================================
#pragma once

#include "public.sdk/source/vst/vstguieditor.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace VSTGUI {
class CViewContainer;
class CTextEdit;
class CTextLabel;
class CVSTGUITimer;
class COptionMenu;
class CControl;
class CBitmap;
class IControlListener;
} // namespace VSTGUI

namespace omnis::analysis {
struct SpectralSignature;
}

namespace omnis::vst3 {

class OmnisController;
class UiFeed;
namespace gui {
class Live;
class RackSlot;
class Screen;
class HwButton;
struct Model;
} // namespace gui

class OmnisEditor : public Steinberg::Vst::VSTGUIEditor {
public:
    explicit OmnisEditor(OmnisController* controller);
    ~OmnisEditor() override;

    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) override;
    void PLUGIN_API close() override;

    static constexpr int kWidth = 1200, kHeight = 1040; ///< la maquette, à ses mesures
    static constexpr double kZoom = 0.83;               ///< à l'écran : 996 × 863

    /// Construit la console dans `root` (kWidth × kHeight).
    void buildInto(VSTGUI::CViewContainer* root);
    /// Un pas de minuterie : relire le flux, redessiner ce qui a changé.
    void tick();
    /// Page du tiroir : 0 … 7 les modules (index params::Module), kCompPage les pistes et bus, kDiagPage le diagnostic du mix.
    void selectPage(int page);
    void paintComposition(VSTGUI::CDrawContext* c, const VSTGUI::CRect& r);
    static VSTGUI::CColor familyColor(int family);
    /// Analyse les échantillons arrivés (spectre, goniomètre) : pour l'outil.
    void pumpFeed();
    /// Tutoriel : montré à la première ouverture, relancé par « ? ». step < 0 : fermé.
    void startTutorial(int step = 0);
    [[nodiscard]] int tutorialStep() const noexcept { return tutorialStep_; }
    [[nodiscard]] static int tutorialSteps() noexcept;
    /// La fenêtre RÉF : tous les genres, une illustration chacun (ouverte par un clic sur RÉF).
    void openReferenceBrowser(const std::string& family = {}, const std::string& style = {});
    void closeReferenceBrowser();
    /// Match EQ : vue 0 (la courbe) ou 1 (EQ dynamique, bande `band` au tiroir).
    void showMatchView(int view, int band);

    enum Page : int { kCompPage = 8, kDiagPage = 9 }; // 7 : le compresseur ; 8 : pistes et bus (COMPOSITION)

private:
    OmnisController* omnis() const;
    [[nodiscard]] double norm(std::uint32_t id) const;
    [[nodiscard]] double plain(std::uint32_t id) const;
    void setPlain(std::uint32_t id, double value);
    void setNorm(std::uint32_t id, double value);
    gui::Model model(std::uint32_t id) const;
    [[nodiscard]] std::string valueText(std::uint32_t id) const;

    void buildRibbons(VSTGUI::CViewContainer* root);
    void buildRack(VSTGUI::CViewContainer* root);
    void buildDrawer(VSTGUI::CViewContainer* root);
    void buildMaster(VSTGUI::CViewContainer* root);
    VSTGUI::CViewContainer* page(VSTGUI::CViewContainer* drawer, int index, const char* number, const char* title, const char* help);
    void knobs(VSTGUI::CViewContainer* parent, const VSTGUI::CRect& area, const char* title, const std::vector<std::uint32_t>& ids, int size = 1);
    void layoutRack();
    void scrollChainTo(int module);
    /// La case d'un module (rang i dans le rack) dans la mini-carte du bandeau de la chaîne.
    [[nodiscard]] VSTGUI::CRect miniMapCell(const VSTGUI::CRect& hud, int i) const;
    [[nodiscard]] std::string moduleCharacter(int module) const; ///< « VCA », « Lampe »… (vide : aucun)
    void nextCharacter(int module);
    /// `later` (un module) est allumé et placé après `first` dans le rack.
    [[nodiscard]] bool onAfter(int first, int later) const;
    [[nodiscard]] std::array<int, 8> order() const; ///< modules dans l'ordre du rack (params::kModules)
    void dragMove(int module, double frameX);
    void drop(int module, double frameX);
    void askAi();
    void buildReferenceMenu(VSTGUI::CViewContainer* parent, const VSTGUI::CRect& r);
    /// Une image du dossier images/ (chargée une fois) ; nullptr si illisible.
    VSTGUI::CBitmap* picture(const std::string& path);
    /// Pochette d'un titre (ou photo d'un artiste) : l'image si elle existe,
    /// sinon une pochette dessinée d'après la fiche ; aussi en petite icône de menu.
    void paintCover(VSTGUI::CDrawContext* c, const VSTGUI::CRect& r, const std::string& imagePath, const analysis::SpectralSignature& signature,
                    const std::string& seed);
    void paintReferenceCard(VSTGUI::CDrawContext* c, const VSTGUI::CRect& r);
    /// Mode promo : monogramme vectoriel (initiales + couronne des 21 bandes de la
    /// fiche) dans les deux couleurs dominantes de la vraie image ; aucune image.
    void paintMonogram(VSTGUI::CDrawContext* c, const VSTGUI::CRect& r, const std::string& imagePath, const analysis::SpectralSignature& signature,
                       const std::string& seed);
    /// Mode promo basculé (« /fbi », Maj + Alt + 5 clics sur le logo) : icônes et carte redessinées.
    void refreshPromo();
    /// « Ajouter un fichier de référence » : le sélecteur du système, puis l'import (contrôleur).
    void pickReferenceFile();

    void paintModuleScreen(int module, VSTGUI::CDrawContext* c, const VSTGUI::CRect& r);
    void paintSpectrum(VSTGUI::CDrawContext* c, const VSTGUI::CRect& r);
    void paintScope(VSTGUI::CDrawContext* c, const VSTGUI::CRect& r);
    void paintMeter(VSTGUI::CDrawContext* c, const VSTGUI::CRect& r);
    void paintReadouts(VSTGUI::CDrawContext* c, const VSTGUI::CRect& r);
    bool matchMouse(const VSTGUI::CPoint& p, int phase, bool fine, const VSTGUI::CRect& r);

    VSTGUI::IControlListener* listen(std::function<void(VSTGUI::CControl*)> f);

    std::vector<gui::Live*> live_;
    std::vector<std::function<void()>> syncs_; ///< menus relus à chaque minuterie
    std::vector<std::unique_ptr<VSTGUI::IControlListener>> listeners_;
    int lastOrder_ = -1;
    int tutorialStep_ = -1;
    VSTGUI::CView* tutorial_ = nullptr;
    friend class TutorialOverlay;
    VSTGUI::CView* refBrowser_ = nullptr;
    friend class RefBrowser;
    std::array<gui::RackSlot*, 8> slots_{};
    std::array<VSTGUI::CView*, 8> slotKnobs_{}; ///< le gros bouton de chaque carte (suit la largeur de la carte)
    std::vector<int> shownModules_;              ///< les modules de la chaîne (allumés), dans l'ordre
    double slotW_ = 150.0;
    int lastMask_ = -1;                          ///< modules allumés au dernier rangement
    int lastModule_ = 6;
    double refChipLeft_ = 0.0;                   ///< où finissent les puces de style (la référence vient après)
    VSTGUI::CViewContainer* chain_ = nullptr;   ///< la bande des cartes (défile à la molette)
    double chainScroll_ = 0.0, chainContentW_ = 0.0;                         ///< le module ouvert avant COMPOSITION
    VSTGUI::CView* library_ = nullptr;           ///< les modules hors de la chaîne
    VSTGUI::CViewContainer* rack_ = nullptr;
    std::array<VSTGUI::CViewContainer*, 10> pages_{};
    std::string diagnosis_; ///< texte du diagnostic du mix (page D)
    std::string refFamily_;               ///< filtre du menu RÉF (vide : tous les styles)
    std::string refStyle_;                ///< sous-genre du filtre (vide : toute la famille)
    std::vector<std::string> refFamilies_; ///< familles proposées au filtre
    std::function<void()> refFill_;        ///< refait le contenu du menu RÉF
    /// Vivant tant que l'éditeur existe : un rappel tardif (sélecteur de fichiers
    /// asynchrone sur macOS) ne touche pas un éditeur détruit.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::array<gui::Screen*, 10> pageScreens_{};
    gui::Screen* spectrum_ = nullptr;
    gui::Screen* scope_ = nullptr;
    gui::Screen* meter_ = nullptr;
    gui::Screen* readouts_ = nullptr;
    gui::Screen* cardScreen_ = nullptr; ///< page Match EQ : la référence visée
    gui::HwButton* matchButton_ = nullptr;
    VSTGUI::CTextEdit* askField_ = nullptr;
    VSTGUI::CTextLabel* askAnswer_ = nullptr;
    VSTGUI::CVSTGUITimer* timer_ = nullptr;
    int page_ = 6;
    int dragFrom_ = -1, dropAt_ = -1;
    int matchSide_ = 0;  ///< Match EQ : 0 édite le Mid, 1 le Side
    int matchNode_ = -1; ///< nœud tenu à la souris
    double matchStartY_ = 0.0, matchStartDb_ = 0.0;
    // Match EQ, vue DYN : l'EQ dynamique (4 bandes).
    int matchView_ = 0; ///< 0 : la courbe (EQ), 1 : les bandes dynamiques (DYN)
    int dynBand_ = 0;   ///< bande montrée dans le tiroir
    bool dynHeld_ = false;
    double dynStartX_ = 0.0, dynStartY_ = 0.0, dynStartHz_ = 0.0, dynStartRange_ = 0.0;
    VSTGUI::CViewContainer* matchStatic_ = nullptr;
    VSTGUI::CViewContainer* matchDynamic_ = nullptr;
    std::array<VSTGUI::CViewContainer*, 4> dynBandViews_{};
    int mbBand_ = 0; ///< bande du multibande montrée dans le tiroir
    std::array<VSTGUI::CViewContainer*, 4> mbBandViews_{};
    bool dynMouse(const VSTGUI::CPoint& p, int phase, bool fine, const VSTGUI::CRect& r);
    int ticks_ = 0;
    std::string answer_;
    struct Card;
    std::shared_ptr<Card> card_; ///< la référence choisie, relue quand elle change

    // Ce que le processeur montre (UiFeed), analysé ici (thread de l'interface).
    std::shared_ptr<UiFeed> feed_;
    struct Analysis;
    std::unique_ptr<Analysis> an_;
};

} // namespace omnis::vst3
