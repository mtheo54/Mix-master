// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisEditor.h — La console OMNIS (VSTGUI, dessinée en code),
//  d'après la maquette console v11 :
//
//   ┌───────────────────────────────────────────────┬──────────┐
//   │ ruban 1 : marque, AUTO, A / B, bypass, Match,  │  MASTER  │
//   │           Delta, écoute, aide, latence         │  In/Out  │
//   │ ruban 2 : la demande à l'IA                    │ bargraph │
//   │ rack : 7 modules au format 500 (gros bouton,   │ mesures  │
//   │        voyant, état ; glisser pour réordonner) │ (verdict │
//   │ tiroir : TOUS les réglages du module choisi    │ en mots) │
//   │          | écran du module (courbe, GR…)       │ instances│
//   │ écrans : analyseur de sortie | goniomètre      │  liées   │
//   └───────────────────────────────────────────────┴──────────┘
//
//  En rôle Stem, le tiroir montre la chaîne de la piste ; en Master, le chef
//  d'orchestre a sa page (propositions, Oui / Non, retouches en place).
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
class IControlListener;
} // namespace VSTGUI

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

    static constexpr int kWidth = 1200, kHeight = 750;

    /// Construit la console dans `root` (kWidth × kHeight).
    void buildInto(VSTGUI::CViewContainer* root);
    /// Un pas de minuterie : relire le flux, redessiner ce qui a changé.
    void tick();
    /// Page du tiroir : 0 … 6 les modules, 7 la piste (Stem), 8 le chef d'orchestre.
    void selectPage(int page);
    /// Analyse les échantillons arrivés (spectre, goniomètre) : pour l'outil.
    void pumpFeed();

    enum Page : int { kStemPage = 7, kConductorPage = 8 };

private:
    OmnisController* omnis() const;
    [[nodiscard]] double norm(std::uint32_t id) const;
    [[nodiscard]] double plain(std::uint32_t id) const;
    void setPlain(std::uint32_t id, double value);
    void setNorm(std::uint32_t id, double value);
    gui::Model model(std::uint32_t id) const;
    [[nodiscard]] std::string valueText(std::uint32_t id) const;
    [[nodiscard]] int role() const;

    void buildRibbons(VSTGUI::CViewContainer* root);
    void buildRack(VSTGUI::CViewContainer* root);
    void buildDrawer(VSTGUI::CViewContainer* root);
    void buildScreens(VSTGUI::CViewContainer* root);
    void buildMaster(VSTGUI::CViewContainer* root);
    VSTGUI::CViewContainer* page(VSTGUI::CViewContainer* drawer, int index, const char* number, const char* title, const char* help);
    void knobs(VSTGUI::CViewContainer* parent, const VSTGUI::CRect& area, const char* title, const std::vector<std::uint32_t>& ids, int size = 1);
    void layoutRack();
    [[nodiscard]] std::array<int, 7> order() const;
    void dragMove(int module, double frameX);
    void drop(int module, double frameX);
    void askAi();

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
    int lastOrder_ = -1, lastRole_ = -1;
    std::array<gui::RackSlot*, 7> slots_{};
    VSTGUI::CViewContainer* rack_ = nullptr;
    std::array<VSTGUI::CViewContainer*, 9> pages_{};
    std::array<gui::Screen*, 9> pageScreens_{};
    gui::Screen* spectrum_ = nullptr;
    gui::Screen* scope_ = nullptr;
    gui::Screen* meter_ = nullptr;
    gui::Screen* readouts_ = nullptr;
    gui::Screen* stemBanner_ = nullptr;
    gui::HwButton* matchButton_ = nullptr;
    VSTGUI::CTextEdit* askField_ = nullptr;
    VSTGUI::CTextLabel* askAnswer_ = nullptr;
    VSTGUI::CVSTGUITimer* timer_ = nullptr;
    int page_ = 6;
    int dragFrom_ = -1, dropAt_ = -1;
    int matchSide_ = 0;  ///< Match EQ : 0 édite le Mid, 1 le Side
    int matchNode_ = -1; ///< nœud tenu à la souris
    double matchStartY_ = 0.0, matchStartDb_ = 0.0;
    int proposalIndex_ = 0, ticks_ = 0;
    std::string answer_;

    // Ce que le processeur montre (UiFeed), analysé ici (thread de l'interface).
    std::shared_ptr<UiFeed> feed_;
    struct Analysis;
    std::unique_ptr<Analysis> an_;
};

} // namespace omnis::vst3
