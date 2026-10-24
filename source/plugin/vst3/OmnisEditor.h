// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisEditor.h — Interface du plug-in (VSTGUI, dessinée en code).
//
//  Première version fonctionnelle de la console :
//    - en-tête : rôle (Seul / Stem / Master), groupe, bypass ;
//    - STEM : mode (Off / Auto / Manuel), élément (Auto ou choisi), élément
//      reconnu, doses du mode Manuel (passe-haut, compression, saturation,
//      EQ dynamique, largeur, trim) ;
//    - MASTER : mesures (LUFS M / S / I, true peak, LRA, corrélation, stems
//      liés) et réglages principaux (entrée, Inflator, clip, limiteur, sortie) ;
//    - CHEF D'ORCHESTRE : les propositions du master, une à une, avec Oui / Non,
//      et les retouches en place (Annuler).
//  Les contrôles écrivent les paramètres par le contrôleur (beginEdit /
//  performEdit / endEdit : automation et annulation de l'hôte) ; une minuterie
//  (10 Hz) relit paramètres et mesures, et demande les propositions au
//  processeur (2 Hz).
// =============================================================================
#pragma once

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include <cstdint>
#include <string>
#include <vector>

namespace VSTGUI {
class CControl;
class CTextLabel;
class CMultiLineTextLabel;
class CVSTGUITimer;
} // namespace VSTGUI

namespace omnis::vst3 {

class OmnisController;

class OmnisEditor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener {
public:
    explicit OmnisEditor(OmnisController* controller);

    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) override;
    void PLUGIN_API close() override;

    void valueChanged(VSTGUI::CControl* control) override;
    void controlBeginEdit(VSTGUI::CControl* control) override;
    void controlEndEdit(VSTGUI::CControl* control) override;

    static constexpr int kWidth = 900, kHeight = 560;

private:
    struct Bound {
        VSTGUI::CControl* control = nullptr;
        std::uint32_t param = 0;
        int steps = 0;                          ///< > 0 : menu (index)
        VSTGUI::CTextLabel* value = nullptr;    ///< valeur affichée sous un potard
    };
    struct Meter {
        VSTGUI::CTextLabel* label = nullptr;
        std::uint32_t param = 0;
    };

    void refresh();
    void showProposal();
    OmnisController* omnis() const;

    std::vector<Bound> bound_;
    std::vector<Meter> meters_;
    VSTGUI::CMultiLineTextLabel* proposalText_ = nullptr;
    VSTGUI::CTextLabel* proposalCount_ = nullptr;
    VSTGUI::CMultiLineTextLabel* appliedText_ = nullptr;
    VSTGUI::CVSTGUITimer* timer_ = nullptr;
    int proposalIndex_ = 0;
    int ticks_ = 0;
};

} // namespace omnis::vst3
