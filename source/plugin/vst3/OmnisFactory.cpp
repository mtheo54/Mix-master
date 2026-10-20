// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisFactory.cpp — Point d'entrée du module VST3.
// =============================================================================
#include "OmnisController.h"
#include "OmnisIds.h"
#include "OmnisProcessor.h"
#include "OmnisVersion.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "public.sdk/source/main/pluginfactory_constexpr.h"

BEGIN_FACTORY_DEF(OMNIS_VENDOR, OMNIS_VENDOR_URL, OMNIS_VENDOR_EMAIL, 2)

DEF_CLASS(omnis::vst3::kProcessorUID,
          Steinberg::PClassInfo::kManyInstances,
          kVstAudioEffectClass,
          OMNIS_PLUGIN_NAME,
          Steinberg::Vst::kDistributable,
          "Fx|Mastering|Dynamics|EQ",
          OMNIS_VERSION_STR,
          kVstVersionString,
          omnis::vst3::OmnisProcessor::createInstance,
          nullptr)

DEF_CLASS(omnis::vst3::kControllerUID,
          Steinberg::PClassInfo::kManyInstances,
          kVstComponentControllerClass,
          OMNIS_PLUGIN_NAME " Controller",
          0,
          "",
          OMNIS_VERSION_STR,
          kVstVersionString,
          omnis::vst3::OmnisController::createInstance,
          nullptr)

END_FACTORY
