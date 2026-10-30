// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisController.cpp
// =============================================================================
#include "OmnisController.h"

#include "OmnisIds.h"
#include "OmnisState.h"
#include "UiFeed.h"

#include "ai/ConsoleActions.h"
#include "core/UserDataDir.h"

#include "params/Parameters.h"

#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"

#ifdef OMNIS_HAS_GUI
#include "OmnisEditor.h"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
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

namespace {

ai::ReferenceLibrary libraryWithUser() {
    ai::ReferenceLibrary lib = ai::ReferenceLibrary::builtin();
    std::ifstream in(userDataDir() / "references_utilisateur.txt");
    if (in) {
        std::stringstream text;
        text << in.rdbuf();
        ai::ReferenceLibrary user;
        user.parse(text.str());
        for (const auto& e : user.entries()) lib.add(e);
    }
    return lib;
}

bool playable(const ai::ReferenceEntry& e) { return e.measured; }

/// Les titres mesurés derrière un choix du menu RÉFÉRENCE.
std::vector<const ai::ReferenceEntry*> referenceEntries(const ai::ReferenceLibrary& lib, OmnisController::RefKind kind, const std::string& name) {
    std::vector<const ai::ReferenceEntry*> out;
    switch (kind) {
    case OmnisController::RefKind::Style: return lib.measuredWithGenre(name);
    case OmnisController::RefKind::Artist:
        for (const auto& e : lib.entries())
            if (playable(e) && e.artist == name) out.push_back(&e);
        break;
    case OmnisController::RefKind::Title:
        for (const auto& e : lib.entries())
            if (playable(e) && e.displayName() == name) out.push_back(&e);
        break;
    case OmnisController::RefKind::None: break;
    }
    return out;
}

bool lessNoCase(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](unsigned char x, unsigned char y) {
        return std::tolower(x) < std::tolower(y);
    });
}

std::string joined(const std::vector<std::string>& parts, const char* sep) {
    std::string s;
    for (const auto& p : parts) s += (s.empty() ? "" : sep) + p;
    return s;
}

} // namespace

double OmnisController::plainOf(std::uint32_t pid) {
    const auto* spec = params::findParameter(pid);
    return spec ? params::toPlain(*spec, getParamNormalized(pid)) : 0.0;
}

void OmnisController::applyPlan(const std::vector<std::pair<std::uint32_t, double>>& settings) {
    for (const auto& [pid, plain] : settings)
        if (const auto* spec = params::findParameter(pid)) editParameter(pid, params::toNormalized(*spec, params::clampPlain(*spec, plain)));
}

std::string OmnisController::ask(const std::string& request) {
    if (!feed_) requestFeed();
    if (!feed_) return "OMNIS ne trouve pas son moteur : réessaie dans un instant.";
    if (std::lround(plainOf(static_cast<std::uint32_t>(params::ParamId::LinkRole))) == static_cast<long>(params::LinkRole::Stem))
        return "Cette instance est un stem : écris ta demande sur l'instance Master (ou Seul).";
    // Un style nommé sert aussi à AUTO.
    const auto lib = libraryWithUser();
    for (const auto& g : lib.genres())
        if (ai::namesMatch(request, g) || request.find(g) != std::string::npos) autoStyle_ = g;
    const auto snap = feed_->prod.snapshot();
    if (snap.seconds < 8.0) {
        pendingAsk_ = request;
        return "Lance la lecture : OMNIS écoute ta prod quelques secondes, puis applique ta demande.";
    }
    return runRequest(request);
}

std::string OmnisController::runRequest(const std::string& text) {
    const auto lib = libraryWithUser();
    const auto u = ai::understandRequest(text, ai::Lexicon::builtin(), lib, false);
    if (u.question) return *u.question;
    const auto snap = feed_->prod.snapshot();
    const auto plan = ai::planRequest(u, snap.signature, [this](params::ParamId pid) { return plainOf(static_cast<std::uint32_t>(pid)); });
    std::vector<std::pair<std::uint32_t, double>> settings;
    for (const auto& [pid, v] : plan.settings) settings.emplace_back(static_cast<std::uint32_t>(pid), v);
    applyPlan(settings);
    if (plan.targetLufs) {
        loudTarget_ = *plan.targetLufs;
        pursuing_ = true;
        pursuitTicks_ = pursuitStable_ = pursuitSteps_ = 0;
    }
    std::vector<std::string> understood;
    for (const auto& h : u.interpretation.hits) understood.push_back(h.text);
    std::string out = understood.empty() ? std::string() : "Compris : " + joined(understood, ", ") + ". ";
    out += "Fait : " + joined(plan.said, " · ") + ".";
    if (!u.interpretation.unknownWords.empty()) out += " (Pas compris : " + joined(u.interpretation.unknownWords, ", ") + ")";
    return out;
}

OmnisController::ReferenceChoices OmnisController::referenceChoices() const {
    const auto lib = libraryWithUser();
    ReferenceChoices c;
    for (const auto& g : lib.genres())
        if (const auto n = lib.measuredWithGenre(g).size(); n > 0) c.styles.emplace_back(g, static_cast<int>(n));
    for (const auto& e : lib.entries()) {
        if (!playable(e)) continue;
        c.titles.push_back(e.displayName());
        auto it = std::find_if(c.artists.begin(), c.artists.end(), [&](const auto& a) { return a.first == e.artist; });
        if (it == c.artists.end()) c.artists.emplace_back(e.artist, 1);
        else ++it->second;
    }
    const auto byName = [](const auto& a, const auto& b) { return lessNoCase(a.first, b.first); };
    std::sort(c.styles.begin(), c.styles.end(), byName);
    std::sort(c.artists.begin(), c.artists.end(), byName);
    std::sort(c.titles.begin(), c.titles.end(), lessNoCase);
    c.titles.erase(std::unique(c.titles.begin(), c.titles.end()), c.titles.end());
    return c;
}

std::string OmnisController::chooseReference(RefKind kind, const std::string& name) {
    if (std::lround(plainOf(static_cast<std::uint32_t>(params::ParamId::LinkRole))) == static_cast<long>(params::LinkRole::Stem))
        return "Cette instance est un stem : choisis la référence sur l'instance Master (ou Seul).";
    pendingRef_ = false;
    if (kind == RefKind::None || name.empty()) {
        refKind_ = RefKind::None;
        refName_.clear();
        return "Pas de référence : OMNIS suit tes mots, AUTO vise le style " + autoStyle_ + ".";
    }
    if (referenceEntries(libraryWithUser(), kind, name).empty()) return name + " : aucun titre mesuré dans la base.";
    refKind_ = kind;
    refName_ = name;
    if (kind == RefKind::Style) autoStyle_ = name;
    if (!feed_) requestFeed();
    if (!feed_ || feed_->prod.snapshot().seconds < 8.0) {
        pendingRef_ = true;
        return "Référence " + name + " : lance la lecture, OMNIS écoute ta prod quelques secondes puis règle vers elle.";
    }
    return runReference();
}

std::string OmnisController::runReference() {
    const auto lib = libraryWithUser();
    const auto refs = referenceEntries(lib, refKind_, refName_);
    const auto plan = ai::planReference(feed_->prod.snapshot().signature, refs, refName_,
                                        [this](params::ParamId pid) { return plainOf(static_cast<std::uint32_t>(pid)); });
    std::vector<std::pair<std::uint32_t, double>> settings;
    for (const auto& [pid, v] : plan.settings) settings.emplace_back(static_cast<std::uint32_t>(pid), v);
    applyPlan(settings);
    if (plan.targetLufs) {
        loudTarget_ = *plan.targetLufs;
        pursuing_ = true;
        pursuitTicks_ = pursuitStable_ = pursuitSteps_ = 0;
    }
    return "Fait : " + joined(plan.said, " · ") + ".";
}

void OmnisController::startAuto() {
    if (!feed_) requestFeed();
    if (!feed_) return;
    feed_->prod.startAuto();
    autoRunning_ = true;
}

double OmnisController::autoProgress() const {
    if (!feed_ || !autoRunning_) return -1.0;
    return feed_->prod.autoState() == ProdListener::AutoState::Waiting ? 0.0 : feed_->prod.autoProgress();
}

std::string OmnisController::pollAutomation() {
    if (!feed_) return {};
    std::string message;
    // AUTO terminé : la chaîne vers le style.
    analysis::SpectralSignature loud;
    if (autoRunning_ && feed_->prod.takeAutoResult(loud)) {
        autoRunning_ = false;
        const auto lib = libraryWithUser();
        const auto current = [this](params::ParamId pid) { return plainOf(static_cast<std::uint32_t>(pid)); };
        // La référence choisie au menu passe avant le style deviné des mots.
        const auto chosen = referenceEntries(lib, refKind_, refName_);
        const auto plan = chosen.empty() ? ai::planAuto(loud, lib, autoStyle_, current) : ai::planAuto(loud, chosen, refName_, current);
        std::vector<std::pair<std::uint32_t, double>> settings;
        for (const auto& [pid, v] : plan.settings) settings.emplace_back(static_cast<std::uint32_t>(pid), v);
        applyPlan(settings);
        if (plan.targetLufs) {
            loudTarget_ = *plan.targetLufs;
            pursuing_ = true;
            pursuitTicks_ = pursuitStable_ = pursuitSteps_ = 0;
        }
        message = joined(plan.said, " · ") + ". Tout reste réglable à la main.";
    }
    // Demande en attente : appliquée dès qu'OMNIS a assez écouté.
    if (!pendingAsk_.empty() && feed_->prod.snapshot().seconds >= 8.0) {
        message = runRequest(pendingAsk_);
        pendingAsk_.clear();
    }
    // Référence choisie au menu, en attente d'écoute.
    if (pendingRef_ && feed_->prod.snapshot().seconds >= 8.0) {
        pendingRef_ = false;
        message = runReference();
    }
    // Le volume mené vers la cible par la mesure (sonie court terme de la sortie).
    if (pursuing_ && ++pursuitTicks_ % 18 == 0) {
        const auto lim = static_cast<std::uint32_t>(params::ParamId::LimGain);
        const double st = plainOf(static_cast<std::uint32_t>(params::ParamId::MeterShortTerm));
        if (st > -60.0) {
            const double err = loudTarget_ - st;
            if (std::fabs(err) < 0.4) {
                if (++pursuitStable_ >= 3) {
                    pursuing_ = false;
                    char b[96];
                    std::snprintf(b, sizeof b, "Volume atteint : %.1f LUFS (limiteur +%.1f dB).", st, plainOf(lim));
                    message = b;
                }
            } else {
                pursuitStable_ = 0;
                const double gain = std::clamp(plainOf(lim) + std::clamp(0.6 * err, -1.5, 1.5), 0.0, 24.0);
                if (const auto* spec = params::findParameter(lim)) editParameter(lim, params::toNormalized(*spec, gain));
                if (++pursuitSteps_ > 40 || (gain >= 24.0 && err > 0.0)) {
                    pursuing_ = false;
                    message = "Volume : le limiteur est au maximum raisonnable ; pousse l'entrée ou le clip si tu veux plus fort.";
                }
            }
        }
    }
    if (!message.empty()) lastMessage_ = message;
    return message;
}

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
    if (!s.readInt32(version) || version < 1 || version > 2 || !s.readInt32(slot)) return kResultOk; // état inconnu : rien à relire
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
    // Version 2 : la référence choisie au menu (gardée avec la session).
    int32 kind = 0;
    if (version >= 2 && s.readInt32(kind)) {
        std::unique_ptr<char8[]> name(s.readStr8());
        if (kind > 0 && kind <= static_cast<int32>(RefKind::Title) && name && *name.get()) {
            refKind_ = static_cast<RefKind>(kind);
            refName_ = name.get();
            if (refKind_ == RefKind::Style) autoStyle_ = refName_;
        }
    }
    return kResultOk;
}

tresult PLUGIN_API OmnisController::getState(IBStream* state) {
    if (!state) return kResultFalse;
    ab_[static_cast<std::size_t>(abSlot_)] = snapshot();
    IBStreamer s(state, kLittleEndian);
    s.writeInt32(2);
    s.writeInt32(abSlot_);
    for (const auto& mem : ab_) {
        s.writeInt32(static_cast<int32>(mem.size()));
        for (const auto& [id, v] : mem) {
            s.writeInt32u(id);
            s.writeDouble(v);
        }
    }
    s.writeInt32(static_cast<int32>(refKind_));
    s.writeStr8(refName_.c_str());
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
    drumLines_.clear();
    std::istringstream in(std::string(static_cast<const char*>(data), size));
    std::string line;
    static const char* kKinds[] = {"", "passe-haut", "cloche", "plateau grave"};
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        std::size_t start = 0;
        for (std::size_t tab; (tab = line.find('\t', start)) != std::string::npos; start = tab + 1) f.push_back(line.substr(start, tab - start));
        f.push_back(line.substr(start));
        if (f.size() >= 2 && f[0] == "D") {
            drumLines_.push_back(f[1]);
        } else if (f.size() >= 3 && f[0] == "P") {
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
