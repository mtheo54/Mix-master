// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisController.cpp
// =============================================================================
#include "OmnisController.h"

#include "OmnisState.h"

#include "params/Parameters.h"

#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <string>

namespace omnis::vst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;
using params::ParamKind;

namespace {

/// Paramètre VST3 adossé à une entrée de la table omnis::params.
class OmnisParameter : public Parameter {
public:
    explicit OmnisParameter(const params::ParamSpec& spec) : spec_(spec) {
        info.id = static_cast<ParamID>(spec.id);
        StringConvert::convert(std::string(spec.name), info.title);
        StringConvert::convert(std::string(spec.shortName), info.shortTitle);
        StringConvert::convert(std::string(spec.unit), info.units);
        info.stepCount = spec.stepCount;
        info.defaultNormalizedValue = params::toNormalized(spec, spec.defaultValue);
        info.unitId = kRootUnitId;
        switch (spec.kind) {
            case ParamKind::Toggle:
                info.flags = ParameterInfo::kCanAutomate;
                if (spec.id == params::ParamId::Bypass) info.flags |= ParameterInfo::kIsBypass;
                break;
            case ParamKind::Choice: info.flags = ParameterInfo::kCanAutomate | ParameterInfo::kIsList; break;
            case ParamKind::Meter: info.flags = ParameterInfo::kIsReadOnly; break;
            case ParamKind::Continuous:
            default: info.flags = ParameterInfo::kCanAutomate; break;
        }
        setPrecision(spec.displayDecimals);
        valueNormalized = info.defaultNormalizedValue;
    }

    void toString(ParamValue normalized, String128 string) const override {
        StringConvert::convert(params::formatValue(spec_, params::toPlain(spec_, normalized)), string);
    }

    bool fromString(const TChar* string, ParamValue& normalized) const override {
        const auto plain = params::parseValue(spec_, StringConvert::convert(string));
        if (!plain) return false;
        normalized = params::toNormalized(spec_, *plain);
        return true;
    }

    ParamValue toPlain(ParamValue normalized) const override { return params::toPlain(spec_, normalized); }
    ParamValue toNormalized(ParamValue plain) const override { return params::toNormalized(spec_, plain); }

private:
    const params::ParamSpec& spec_;
};

} // namespace

tresult PLUGIN_API OmnisController::initialize(FUnknown* context) {
    const tresult result = EditController::initialize(context);
    if (result != kResultOk) return result;
    for (const auto& spec : params::allParameters()) parameters.addParameter(new OmnisParameter(spec));
    return kResultOk;
}

tresult PLUGIN_API OmnisController::setParamNormalized(ParamID tag, ParamValue value) {
    const ParamValue before = getParamNormalized(tag);
    const tresult result = EditController::setParamNormalized(tag, value);
    if (result == kResultOk && tag == static_cast<ParamID>(params::ParamId::LinkRole) && before != value && componentHandler)
        componentHandler->restartComponent(kLatencyChanged);
    return result;
}

tresult PLUGIN_API OmnisController::setComponentState(IBStream* state) {
    // Les paramètres absents de l'état reviennent à leur valeur par défaut,
    // exactement comme côté processeur.
    for (const auto& spec : params::allParameters())
        if (params::isPersistent(spec))
            setParamNormalized(static_cast<ParamID>(spec.id), params::toNormalized(spec, spec.defaultValue));
    const bool ok = readState(state, [this](std::uint32_t id, double value) { setParamNormalized(id, value); });
    return ok ? kResultOk : kResultFalse;
}

} // namespace omnis::vst3
