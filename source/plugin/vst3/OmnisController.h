// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisController.h — Contrôleur d'édition VST3 (thread UI).
//
//  Expose les paramètres de la table omnis::params à l'hôte, avec affichage et
//  saisie texte dans les unités physiques (dB, LUFS…). Les mesures sont des
//  paramètres en lecture seule : tant que l'interface NanoVG (module 6)
//  n'existe pas, l'éditeur générique du DAW affiche déjà LUFS M/S/I, True Peak,
//  corrélation et LRA en temps réel.
//
//  createView() renvoie nullptr pour l'instant (éditeur générique de l'hôte) :
//  l'IPlugView OpenGL + NanoVG arrive au module 6.
//
//  Nom de la piste : l'hôte le donne par ChannelContext::IInfoListener (la
//  norme VST3, quel que soit le DAW qui la suit) ; le contrôleur le passe au
//  processeur par message, qui s'en sert pour reconnaître l'élément d'un stem.
// =============================================================================
#pragma once

#include "pluginterfaces/vst/ivstchannelcontextinfo.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

namespace omnis::vst3 {

class OmnisController : public Steinberg::Vst::EditController, public Steinberg::Vst::ChannelContext::IInfoListener {
public:
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new OmnisController());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    /// « Link Role » : un stem n'a pas de latence, un master si ; l'hôte doit
    /// relire la latence et réactiver l'instance.
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) override;

    /// Nom (et couleur…) de la piste où le plug-in est inséré, à chaque changement.
    Steinberg::tresult PLUGIN_API setChannelContextInfos(Steinberg::Vst::IAttributeList* list) override;

    OBJ_METHODS(OmnisController, EditController)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::ChannelContext::IInfoListener)
    END_DEFINE_INTERFACES(EditController)
    REFCOUNT_METHODS(EditController)
};

} // namespace omnis::vst3
