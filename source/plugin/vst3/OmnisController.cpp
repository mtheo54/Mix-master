// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisController.cpp
// =============================================================================
#include "OmnisController.h"

#include "OmnisIds.h"
#include "OmnisState.h"
#include "UiFeed.h"

#include "params/Parameters.h"

#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"

#ifdef OMNIS_HAS_GUI
#include "OmnisEditor.h"
#endif

#include <algorithm>
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
        if (spec.kind == ParamKind::Choice && spec.choices.empty()) info.flags &= ~ParameterInfo::kIsList;
        if (!spec.automatable) info.flags &= ~ParameterInfo::kCanAutomate;
        // L'ordre du rack se règle dans l'interface (glisser un module), pas en liste.
        if (spec.id == params::ParamId::ChainOrder) info.flags |= ParameterInfo::kIsHidden;
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
    // Pendant setComponentState (projet rechargé), le processeur a déjà son état :
    // pas de message ni de redémarrage demandé à l'hôte au milieu de son appel.
    if (result == kResultOk && !loadingState_ && tag == static_cast<ParamID>(params::ParamId::LinkRole) && before != value) {
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

std::string OmnisController::ask(const std::string& request) {
    (void)request;
    return "La barre IA arrive : elle lira ta demande et réglera la chaîne.";
}

void OmnisController::startAuto() {}

void OmnisController::restartTutorial() {}

void OmnisController::requestFeed() {
    IPtr<IMessage> message = owned(allocateMessage());
    if (!message) return;
    message->setMessageID(kFeedRequest);
    sendMessage(message);
}

void OmnisController::editParameter(ParamID id, ParamValue normalized) {
    beginEdit(id);
    performGesture(id, normalized);
    endEdit(id);
}

void OmnisController::performGesture(ParamID id, ParamValue normalized) {
    setParamNormalized(id, normalized);
    performEdit(id, getParamNormalized(id));
}

std::vector<std::pair<std::uint32_t, double>> OmnisController::snapshot() {
    std::vector<std::pair<std::uint32_t, double>> out;
    for (const auto& spec : params::allParameters())
        if (params::isPersistent(spec)) out.emplace_back(static_cast<std::uint32_t>(spec.id), getParamNormalized(static_cast<ParamID>(spec.id)));
    return out;
}

void OmnisController::apply(const std::vector<std::pair<std::uint32_t, double>>& values) {
    for (const auto& [id, v] : values) {
        // Le rôle et le groupe tiennent à la place de l'instance dans le projet : pas dans A / B.
        if (id == static_cast<std::uint32_t>(params::ParamId::LinkRole) || id == static_cast<std::uint32_t>(params::ParamId::LinkGroup) ||
            id == static_cast<std::uint32_t>(params::ParamId::LinkStemKind))
            continue;
        if (getParamNormalized(id) != v) editParameter(id, v);
    }
}

void OmnisController::selectAB(int slot) {
    slot = slot != 0 ? 1 : 0;
    if (slot == abSlot_) return;
    ab_[static_cast<std::size_t>(abSlot_)] = snapshot();
    abSlot_ = slot;
    // Première visite de B : il part du réglage de A.
    if (ab_[static_cast<std::size_t>(slot)].empty()) ab_[static_cast<std::size_t>(slot)] = ab_[static_cast<std::size_t>(1 - slot)];
    apply(ab_[static_cast<std::size_t>(slot)]);
}

void OmnisController::copyABToOther() { ab_[static_cast<std::size_t>(1 - abSlot_)] = snapshot(); }

tresult PLUGIN_API OmnisController::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 version = 0, slot = 0;
    if (!s.readInt32(version) || version != 1 || !s.readInt32(slot)) return kResultOk; // ancien état : rien à relire
    abSlot_ = slot != 0 ? 1 : 0;
    for (auto& mem : ab_) {
        mem.clear();
        int32 count = 0;
        if (!s.readInt32(count) || count < 0 || count > 4096) return kResultOk;
        for (int32 i = 0; i < count; ++i) {
            uint32 id = 0;
            double v = 0.0;
            if (!s.readInt32u(id) || !s.readDouble(v)) return kResultOk;
            if (params::findParameter(id)) mem.emplace_back(id, std::clamp(v, 0.0, 1.0));
        }
    }
    return kResultOk;
}

tresult PLUGIN_API OmnisController::getState(IBStream* state) {
    if (!state) return kResultFalse;
    ab_[static_cast<std::size_t>(abSlot_)] = snapshot();
    IBStreamer s(state, kLittleEndian);
    s.writeInt32(1);
    s.writeInt32(abSlot_);
    for (const auto& mem : ab_) {
        s.writeInt32(static_cast<int32>(mem.size()));
        for (const auto& [id, v] : mem) {
            s.writeInt32u(id);
            s.writeDouble(v);
        }
    }
    return kResultOk;
}

tresult PLUGIN_API OmnisController::notify(IMessage* message) {
    if (message && message->getMessageID() && std::string_view(message->getMessageID()) == kFeedMessage) {
        int64 token = 0;
        if (message->getAttributes() && message->getAttributes()->getInt(kFeedAttribute, token) == kResultTrue)
            feed_ = findFeed(static_cast<std::uint64_t>(token));
        return kResultOk;
    }
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
    loadingState_ = true;
    for (const auto& spec : params::allParameters())
        if (params::isPersistent(spec))
            setParamNormalized(static_cast<ParamID>(spec.id), params::toNormalized(spec, spec.defaultValue));
    const bool ok = readState(state, [this](std::uint32_t id, double value) { setParamNormalized(id, value); });
    loadingState_ = false;
    return ok ? kResultOk : kResultFalse;
}

} // namespace omnis::vst3
