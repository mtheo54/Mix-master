// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisController.cpp
// =============================================================================
#include "OmnisController.h"

#include "OmnisIds.h"
#include "OmnisState.h"

#include "params/Parameters.h"

#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vstparameters.h"

#ifdef OMNIS_HAS_GUI
#include "OmnisEditor.h"
#endif

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>

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
    if (result == kResultOk && tag == static_cast<ParamID>(params::ParamId::LinkRole) && before != value) {
        // Le processeur change de rôle tout de suite (sans attendre une réactivation).
        if (IPtr<IMessage> message = owned(allocateMessage())) {
            message->setMessageID(kRoleMessage);
            message->getAttributes()->setInt(kRoleAttribute, static_cast<int64>(std::lround(value * 2.0)));
            sendMessage(message);
        }
        if (componentHandler) componentHandler->restartComponent(kLatencyChanged);
    }
    return result;
}

tresult PLUGIN_API OmnisController::setChannelContextInfos(IAttributeList* list) {
    if (!list) return kInvalidArgument;
    String128 name{};
    if (list->getString(ChannelContext::kChannelNameKey, name, sizeof(name)) != kResultTrue) return kResultOk;
    const std::string utf8 = StringConvert::convert(name);
    IPtr<IMessage> message = owned(allocateMessage());
    if (!message) return kResultOk;
    message->setMessageID(vst3::kTrackNameMessage);
    message->getAttributes()->setBinary(vst3::kTrackNameAttribute, utf8.data(), static_cast<uint32>(utf8.size()));
    sendMessage(message);
    return kResultOk;
}

IPlugView* PLUGIN_API OmnisController::createView(FIDString name) {
#ifdef OMNIS_HAS_GUI
    if (name && std::string_view(name) == ViewType::kEditor) return new OmnisEditor(this);
#else
    (void)name;
#endif
    return nullptr;
}

void OmnisController::requestProposals() {
    IPtr<IMessage> message = owned(allocateMessage());
    if (!message) return;
    message->setMessageID(kProposalsRequest);
    sendMessage(message);
}

void OmnisController::answerProposal(std::uint32_t key, int answer) {
    IPtr<IMessage> message = owned(allocateMessage());
    if (!message) return;
    message->setMessageID(kAnswerMessage);
    message->getAttributes()->setInt(kAnswerKeyAttribute, static_cast<int64>(key));
    message->getAttributes()->setInt(kAnswerAttribute, answer);
    sendMessage(message);
}

tresult PLUGIN_API OmnisController::notify(IMessage* message) {
    if (!message || !message->getMessageID() || std::string_view(message->getMessageID()) != kProposalsMessage)
        return EditController::notify(message);
    const void* data = nullptr;
    uint32 size = 0;
    if (!message->getAttributes() || message->getAttributes()->getBinary(kProposalsAttribute, data, size) != kResultTrue) return kResultOk;
    pending_.clear();
    applied_.clear();
    std::istringstream in(std::string(static_cast<const char*>(data), size));
    std::string line;
    static const char* kKinds[] = {"", "passe-haut", "cloche", "plateau grave"};
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        std::size_t start = 0;
        for (std::size_t tab; (tab = line.find('\t', start)) != std::string::npos; start = tab + 1) f.push_back(line.substr(start, tab - start));
        f.push_back(line.substr(start));
        if (f.size() >= 3 && f[0] == "P") {
            pending_.push_back({static_cast<std::uint32_t>(std::strtoul(f[1].c_str(), nullptr, 10)), f[2], {}});
        } else if (f.size() >= 6 && f[0] == "A") {
            const int kind = std::atoi(f[3].c_str());
            std::string d = "« " + f[2] + " » : " + (kind >= 1 && kind <= 3 ? kKinds[kind] : "retouche") + " " + f[4] + " Hz";
            if (kind != 1) d += ", " + f[5] + " dB";
            applied_.push_back({static_cast<std::uint32_t>(std::strtoul(f[1].c_str(), nullptr, 10)), {}, d});
        }
    }
    return kResultOk;
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
