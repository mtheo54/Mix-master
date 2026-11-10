// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisController.cpp
// =============================================================================
#include "core/I18n.h"
#include "OmnisController.h"

#include "OmnisIds.h"
#include "OmnisState.h"
#include "UiFeed.h"

#include "ai/ConsoleActions.h"
#include "ai/GenreProfiles.h"
#include "ai/EmbeddedData.h"
#include "ai/MixDiagnosis.h"
#include "ai/StyleRecognizer.h"
#include "ai/HabitLearning.h"
#include "ai/TextMatch.h"
#include "import/ReferenceImport.h"
#include "AudioRead.h"
#include "LoudnessTuner.h"
#include "core/UserDataDir.h"
#include "gfx/AvatarCache.h"

#include "params/Parameters.h"

#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"

#ifdef OMNIS_HAS_GUI
#include "OmnisEditor.h"
#endif

#include <algorithm>
#include <cstring>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace omnis::vst3 {

namespace {

// Groupes sonores livrés (moyenne et dispersion par groupe, data/groupes_sonores.txt).
const ai::StyleStats& soundGroups() {
    static const ai::StyleStats stats = ai::parseStyleStats(std::string(ai::data::groupes()));
    return stats;
}
std::filesystem::path styleMemoryPath() { return userDataDir() / "styles_appris.txt"; }
std::filesystem::path learningDir() { return userDataDir() / "apprentissage"; }
std::filesystem::path journalPath() { return learningDir() / "journal.tsv"; }
std::filesystem::path habitNetPath() { return learningDir() / "reseau.txt"; }
std::filesystem::path usageGraphPath() { return learningDir() / "graphe.tsv"; }
std::string pathText(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::optional<ai::HabitNet> loadHabitNet() {
    std::ifstream in(habitNetPath(), std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return ai::HabitNet::parse(text.str());
}
std::vector<ai::StyleExample> loadStyleMemory() {
    std::ifstream in(styleMemoryPath(), std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return ai::parseStyleExamples(text.str());
}

} // namespace

namespace {
/// Format traduit avant d'être rempli (anglais).
std::string format(const char* f, double v) {
    char b[200];
    std::snprintf(b, sizeof b, i18n::tr(f).c_str(), v);
    return b;
}
} // namespace

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
    // NIVEAU changé à la main : le volume y sera mené une fois (pollAutomation).
    if (result == kResultOk && !loadingState_ && tag == static_cast<ParamID>(params::ParamId::LoudnessLevel) && before != value) levelChanged_ = true;
    // Pendant une séance (après AUTO ou un preset) : le chemin parcouru par chaque réglage clé (les
    // allers-retours disent une hésitation). Ce qu'OMNIS règle lui-même fait partie de sa proposition.
    if (result == kResultOk && !loadingState_ && autoPending_ && before != value) {
        using P = params::ParamId;
        static constexpr P kTracked[] = {P::MatchTilt, P::SubGain, P::InflHighEffect, P::ClipDrive, P::LimGain, P::CompOn, P::CompThreshold,
                                         static_cast<P>(static_cast<int>(P::ImgWidthFirst) + 2), static_cast<P>(static_cast<int>(P::ImgWidthFirst) + 3)};
        if (std::find(std::begin(kTracked), std::end(kTracked), static_cast<P>(tag)) != std::end(kTracked)) {
            const auto now = habitValues();
            for (int i = 0; i < ai::kHabits; ++i) {
                const auto k = static_cast<std::size_t>(i);
                const double d = now[k] - lastHabits_[k];
                if (omnisEditing_) session_.proposed.habits[k] += d;
                else session_.movement[k] += std::fabs(d);
            }
            lastHabits_ = now;
        }
    }
    // Mode de latence : la latence annoncée change, l'hôte doit réactiver le plug-in.
    if (result == kResultOk && !loadingState_ && tag == static_cast<ParamID>(params::ParamId::LowLatency) && before != value && componentHandler)
        componentHandler->restartComponent(kLatencyChanged);
    // Rôle : le processeur change tout de suite ; la latence annoncée aussi (Bus / Piste : 0).
    if (result == kResultOk && !loadingState_ && tag == static_cast<ParamID>(params::ParamId::InstanceRole) && before != value) {
        if (IPtr<IMessage> message = owned(allocateMessage())) {
            message->setMessageID(kRoleMessage);
            message->getAttributes()->setInt(kRoleAttribute, static_cast<int64>(std::lround(value)));
            sendMessage(message);
        }
        compositionAtMs_ = 0;
        if (componentHandler) componentHandler->restartComponent(kLatencyChanged);
    }
    return result;
}

tresult PLUGIN_API OmnisController::setChannelContextInfos(IAttributeList* list) {
    if (!list) return kInvalidArgument;
    String128 name{};
    if (list->getString(ChannelContext::kChannelNameKey, name, sizeof(name)) != kResultTrue) return kResultOk;
    trackName_ = StringConvert::convert(name);
    if (IPtr<IMessage> message = owned(allocateMessage())) {
        message->setMessageID(kTrackNameMessage);
        message->getAttributes()->setBinary(kTrackNameAttribute, trackName_.data(), static_cast<uint32>(trackName_.size()));
        sendMessage(message);
    }
    return kResultOk;
}

params::InstanceRole OmnisController::instanceRole() {
    return static_cast<params::InstanceRole>(std::clamp<long>(std::lround(plainOf(static_cast<std::uint32_t>(params::ParamId::InstanceRole))), 0, 1));
}

const stems::Composition& OmnisController::composition() {
    const std::uint64_t now = link::nowMs();
    if (compositionAtMs_ != 0 && now < compositionAtMs_ + 1000) return composition_;
    compositionAtMs_ = now;
    if (!observer_.isOpen()) observer_.open();
    composition_ = observer_.isOpen() ? stems::buildComposition(observer_.instances()) : stems::Composition{};
    return composition_;
}

std::string OmnisController::relistenBuses() {
    if (!observer_.isOpen()) observer_.open();
    if (!observer_.isOpen()) return "Aucun bus OMNIS dans le projet : rien à réécouter.";
    observer_.requestRelisten();
    compositionAtMs_ = 0;
    return "Les bus repartent de zéro : lance la lecture du début à la fin, ils réécoutent tout le morceau.";
}

std::string OmnisController::compositionText() {
    const auto& c = composition();
    if (c.empty())
        return i18n::tr("Aucun bus OMNIS dans le projet. Pose OMNIS sur tes bus (DRUMS, MÉLO, EFX…), choisis RÔLE : Bus, "
                        "puis lance la lecture : je te dis ce qu'il y a dans chacun. Rien à mettre sur les pistes.");
    return stems::describeComposition(c);
}

IPlugView* PLUGIN_API OmnisController::createView(FIDString name) {
#ifdef OMNIS_HAS_GUI
    if (name && std::string_view(name) == ViewType::kEditor) return new OmnisEditor(this);
#else
    (void)name;
#endif
    return nullptr;
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
    // Sous-genres précis de l'artiste ajoutés à chaque titre (filtre du menu RÉF,
    // demandes « un son drill fr »), co-crédits compris. Le classement de
    // l'utilisateur reste : on ajoute, on ne remplace pas. Les profils OMNIS ont
    // déjà le leur.
    ai::ReferenceLibrary out;
    for (auto e : lib.entries()) {
        if (!e.isProfile())
            for (const auto& g : ai::stylesOfArtist(e.artist))
                if (std::find(e.genres.begin(), e.genres.end(), g) == e.genres.end()) e.genres.push_back(g);
        out.add(std::move(e));
    }
    return out;
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

void OmnisController::rememberForUndo(const std::string& label) {
    UndoStep step;
    step.label = label;
    for (const auto& spec : params::allParameters())
        if (params::isPersistent(spec) && spec.id != params::ParamId::LowLatency)
            step.values.emplace_back(static_cast<std::uint32_t>(spec.id), plainOf(static_cast<std::uint32_t>(spec.id)));
    undo_.push_back(std::move(step));
    if (undo_.size() > 20) undo_.erase(undo_.begin());
}

std::string OmnisController::undoLast() {
    if (undo_.empty()) return "Rien à annuler.";
    UndoStep step = std::move(undo_.back());
    undo_.pop_back();
    pursuing_ = false; // la course au volume de l'action annulée s'arrête
    if ((step.label == "AUTO" || step.label.rfind("référence ", 0) == 0) && autoPending_) {
        // AUTO (ou un preset) défait aussitôt : noté comme un mauvais choix (sans direction à apprendre).
        session_.undone = true;
        session_.kept = usageState();
        auto graph = ai::loadUsageGraph(pathText(usageGraphPath()));
        graph.record(session_, ai::judgeSession(session_));
        ai::saveUsageGraph(pathText(usageGraphPath()), graph);
        ai::JournalEvent e;
        e.kind = ai::JournalKind::Undo;
        e.time = static_cast<std::int64_t>(std::time(nullptr));
        e.group = recognizedGroup_;
        e.features = autoFeatures_;
        std::error_code ec;
        std::filesystem::create_directories(learningDir(), ec);
        ai::appendJournal(pathText(journalPath()), e);
        autoPending_ = false;
    }
    applyPlan(step.values);
    return "Annulé : la console est revenue à l'état d'avant « " + step.label + " »" +
           (undo_.empty() ? "." : " (encore " + std::to_string(undo_.size()) + " retour(s) possible(s)).");
}

void OmnisController::applyPlan(const std::vector<std::pair<std::uint32_t, double>>& settings) {
    for (const auto& [pid, plain] : settings)
        if (const auto* spec = params::findParameter(pid)) editParameter(pid, params::toNormalized(*spec, params::clampPlain(*spec, plain)));
}

OmnisController::~OmnisController() {
    // Le DAW se ferme pendant un import : les requêtes restantes sont abandonnées
    // (au plus une en cours, 6 s), puis on attend le thread (le code du plug-in ne
    // doit pas disparaître sous lui).
    if (import_) import_->cancel = true;
    if (importThread_.joinable()) importThread_.join();
    // La séance se ferme : ce que la personne a gardé du dernier AUTO est noté.
    try {
        noteRetouch();
    } catch (...) {
    }
    if (autoJob_) autoJob_->cancel = true; // AUTO précis : au plus un rendu à finir
    if (autoThread_.joinable()) autoThread_.join();
    if (fileThread_.joinable()) fileThread_.join(); // un fichier en lecture : quelques secondes
}

bool OmnisController::onlineRecognition() const {
    if (online_ < 0) {
        std::ifstream in(userDataDir() / "musicbrainz.txt");
        std::string word;
        in >> word;
        online_ = word == "non" ? 0 : 1;
    }
    return online_ == 1;
}

void OmnisController::setOnlineRecognition(bool on) {
    online_ = on ? 1 : 0;
    std::error_code ec;
    const auto flag = userDataDir() / "musicbrainz.txt";
    std::filesystem::create_directories(flag.parent_path(), ec);
    std::ofstream(flag) << (on ? "oui" : "non") << "\n";
}

bool OmnisController::importRunning() const { return import_ && !import_->done.load(); }

bool OmnisController::takeLibraryChanged() {
    const bool changed = libraryChanged_;
    libraryChanged_ = false;
    return changed;
}

std::string OmnisController::importReferenceFile(const std::string& utf8Path) {
    if (importRunning()) return "J'écoute déjà un fichier : un instant.";
    if (importThread_.joinable()) importThread_.join();
    const std::filesystem::path path(std::u8string(utf8Path.begin(), utf8Path.end()));
    auto job = std::make_shared<ImportJob>();
    import_ = job;
    const bool online = onlineRecognition();
    importThread_ = std::thread([job, path, online] {
        // Rien ne doit sortir de ce thread : une exception non rattrapée arrêterait le DAW.
        try {
        const auto lib = libraryWithUser();
        const auto known = [&lib](const std::string& name) {
            const auto m = lib.find(name);
            return m.kind == ai::ReferenceMatch::Kind::Artist && m.exact;
        };
        const auto genresOf = [&lib](const std::string& name) {
            std::vector<std::string> out;
            for (const auto& e : lib.entries())
                if (!e.isProfile() && ai::namesMatch(e.artist, name))
                    for (const auto& g : e.genres)
                        if (std::find(out.begin(), out.end(), g) == out.end()) out.push_back(g);
            return out;
        };
        const import::Fetch online = import::onlineFetch();
        const import::Fetch fetch = [job, online](const std::string& url, std::string& body) { return !job->cancel && online(url, body); };
        const auto r = import::importReferenceFile(path, userDataDir() / "references_utilisateur.txt", known, genresOf, online ? &fetch : nullptr);
        job->ok = r.ok;
        job->message = r.message;
        } catch (const std::exception& e) {
            job->ok = false;
            job->message = std::string("Je n'ai pas pu ajouter ce fichier : ") + e.what();
        } catch (...) {
            job->ok = false;
            job->message = "Je n'ai pas pu ajouter ce fichier.";
        }
        job->done = true;
    });
    const auto name = path.filename().u8string();
    return std::string("J'écoute « ") + std::string(name.begin(), name.end()) + (online ? " » (et je le cherche sur MusicBrainz)…" : " »…");
}

bool OmnisController::promoMode() const {
    if (promo_ < 0) {
        std::error_code ec;
        promo_ = std::filesystem::exists(userDataDir() / "mode_promo.txt", ec) ? 1 : 0;
    }
    return promo_ == 1;
}

void OmnisController::setPromoMode(bool on) {
    promo_ = on ? 1 : 0;
    std::error_code ec;
    const auto flag = userDataDir() / "mode_promo.txt";
    if (on) {
        std::filesystem::create_directories(flag.parent_path(), ec);
        std::ofstream(flag) << "monogrammes à la place des avatars\n";
    } else {
        std::filesystem::remove(flag, ec);
    }
}

std::string OmnisController::ask(const std::string& request) {
    {
        std::string command = request;
        while (!command.empty() && std::isspace(static_cast<unsigned char>(command.back()))) command.pop_back();
        while (!command.empty() && std::isspace(static_cast<unsigned char>(command.front()))) command.erase(0, 1);
        if (command == "/fbi") {
            setPromoMode(!promoMode());
            return promoMode() ? "Mode promo : des monogrammes à la place des avatars (rien à flouter en vidéo). /fbi pour revenir."
                               : "Mode promo désactivé : les avatars reviennent.";
        }
        // « remets tout à zéro », « reset », « par défaut » : tous les réglages
        // reviennent au départ.
        const std::string plain = ai::normalizeText(command);
        // Langue de l'interface (gardée pour les sessions suivantes).
        if (plain == "en" || plain == "english" || plain == "in english" || plain == "en anglais" || plain == "anglais" || command == "/en") {
            i18n::setLanguage(i18n::Lang::En);
            return "Interface in English. Type /fr pour revenir en français.";
        }
        if (plain == "fr" || plain == "francais" || plain == "en francais" || plain == "french" || plain == "in french" || command == "/fr") {
            i18n::setLanguage(i18n::Lang::Fr);
            return "Interface en français. Type /en for English.";
        }
        // « tuto », « aide », « comment ça marche » : le guide se rouvre.
        static constexpr std::string_view kHelpExact[] = {"aide", "help", "guide", "le guide", "?", "aide moi", "tuto", "tutoriel", "tutorial"};
        static constexpr std::string_view kHelpPart[] = {"tuto", "comment ca marche", "comment ça marche", "how does it work", "how it works",
                                                         "mode d emploi", "explique moi le plugin", "explique le plugin"};
        bool help = false;
        for (const auto phrase : kHelpExact) help = help || plain == phrase || command == phrase;
        for (const auto phrase : kHelpPart) help = help || plain.find(phrase) != std::string::npos;
        if (help) {
            restartTutorial();
            return "Le guide s'ouvre : SUIVANT pour avancer, PASSER pour le fermer. Le mode d'emploi complet est livré avec le plug-in.";
        }
        // Apprentissage : exporter (anonyme, un fichier) ou oublier.
        static constexpr std::string_view kExportLearning[] = {"exporte mon apprentissage", "export apprentissage", "exporte l apprentissage",
                                                               "export my learning", "partage mon apprentissage"};
        for (const auto phrase : kExportLearning)
            if (plain.find(phrase) != std::string::npos) {
                const auto journal = ai::loadJournal(pathText(journalPath()));
                const auto file = learningDir() / "export_anonyme.tsv";
                std::error_code ec;
                std::filesystem::create_directories(learningDir(), ec);
                std::ofstream(file, std::ios::binary) << ai::exportAnonymous(journal);
                // Le graphe des usages, pour la page de données (des noms de réglages et des nombres).
                std::ofstream(learningDir() / "graphe_usages.json", std::ios::binary) << ai::loadUsageGraph(pathText(usageGraphPath())).exportJson();
                const auto samples = ai::samplesFromJournal(journal).size();
                char b[400];
                std::snprintf(b, sizeof b, i18n::tr("Export anonyme écrit (%d séance(s), des nombres seulement, rien n'est envoyé) : %s").c_str(),
                              static_cast<int>(samples), pathText(file).c_str());
                return b;
            }
        // « mes habitudes », « qu'est-ce que je fais le plus » : le graphe des usages en clair.
        static constexpr std::string_view kHabits[] = {"mes habitudes", "ce que je fais le plus", "qu est ce que je fais", "mon graphe", "my habits",
                                                       "what do i do most"};
        for (const auto phrase : kHabits)
            if (plain.find(phrase) != std::string::npos && plain.find("oublie") == std::string::npos) {
                const std::string d = ai::loadUsageGraph(pathText(usageGraphPath())).describe();
                return d.empty() ? "Rien encore : lance AUTO ou choisis un preset (menu RÉF), puis règle à ton goût ; je note ce qui revient et qui sert."
                                 : d;
            }
        static constexpr std::string_view kForget[] = {"oublie mes habitudes", "oublie tout ce que tu as appris", "efface l apprentissage",
                                                       "reset apprentissage", "forget my habits"};
        for (const auto phrase : kForget)
            if (plain.find(phrase) != std::string::npos) {
                std::error_code ec;
                std::filesystem::remove(journalPath(), ec);
                std::filesystem::remove(habitNetPath(), ec);
                std::filesystem::remove(styleMemoryPath(), ec);
                std::filesystem::remove(usageGraphPath(), ec);
                std::filesystem::remove(learningDir() / "graphe_usages.json", ec);
                autoPending_ = false;
                return "Oublié : tes habitudes, tes styles nommés et le journal sont effacés de ce poste.";
            }
        static constexpr std::string_view kExport[] = {"export", "exporter", "pret a exporter", "je peux exporter", "check export", "ready to export"};
        for (const auto phrase : kExport)
            if (plain == phrase || plain.find(std::string(phrase) + " ") == 0 || plain.find(" " + std::string(phrase)) != std::string::npos) {
                noteRetouch(true); // les réglages au moment d'exporter : ce que la personne a gardé (compte double)
                const auto problems = exportProblems();
                if (problems.empty()) return "PRÊT À EXPORTER : rien ne fausse le fichier (écoute normale, qualité max, true peak sous -1 dBTP).";
                std::string s = "PAS ENCORE PRÊT À EXPORTER : ";
                for (std::size_t i = 0; i < problems.size(); ++i) s += (i ? " ; " : "") + i18n::tr(problems[i]);
                return s + ".";
            }
        static constexpr std::string_view kDiag[] = {"diagnosis", "mix diagnosis", "check my mix", "whats wrong", "what s wrong","diagnostic", "diag", "analyse mon mix", "analyse le mix", "check mon mix", "mix check",
                                                     "qu est ce qui va pas", "quest ce qui va pas", "ce qui va pas", "conseils mix", "conseil mix"};
        for (const auto phrase : kDiag)
            if (plain.find(phrase) != std::string::npos) return mixDiagnosis();
        static constexpr std::string_view kReset[] = {"reset", "remet tout a zero", "remets tout a zero", "remettre a zero", "remise a zero",
                                                      "tout a zero", "reinitialise", "reinitialiser", "par defaut", "repart de zero",
                                                      "repars de zero", "on repart de zero", "efface tout", "annule tout"};
        for (const auto phrase : kReset)
            if (plain.find(phrase) != std::string::npos) {
                rememberForUndo("remise à zéro");
                std::vector<std::pair<std::uint32_t, double>> settings;
                for (const auto& spec : params::allParameters())
                    if (params::isPersistent(spec))
                        settings.emplace_back(static_cast<std::uint32_t>(spec.id), spec.defaultValue);
                applyPlan(settings);
                pendingAsk_.clear();
                pursuing_ = false;
                return "Tout est revenu aux réglages de départ.";
            }
    }
    {
        // « annule », « undo », « ctrl z » : l'action précédente de l'IA est défaite.
        const std::string plain = ai::normalizeText(request);
        static constexpr std::string_view kUndo[] = {"annule", "annuler", "undo", "ctrl z", "retour arriere", "reviens en arriere",
                                                     "defais", "remets comme avant", "remet comme avant", "c etait mieux avant", "go back"};
        for (const auto phrase : kUndo)
            if (plain == phrase || plain.starts_with(std::string(phrase) + " ")) return undoLast();
    }
    if (!feed_) requestFeed();
    // Pistes et bus : « qu'est-ce qu'il y a dans mes bus ? », « composition », « rôle bus ».
    {
        const std::string plain = ai::normalizeText(request);
        const auto hasAny = [&](std::initializer_list<std::string_view> phrases) {
            for (const auto phrase : phrases)
                if (plain.find(phrase) != std::string::npos) return true;
            return false;
        };
        const auto setRole = [&](params::InstanceRole r) {
            const auto pid = static_cast<ParamID>(params::ParamId::InstanceRole);
            editParameter(pid, params::toNormalized(*params::findParameter(params::ParamId::InstanceRole), static_cast<double>(r)));
        };
        if (hasAny({"role bus", "mode bus", "en bus", "c est un bus", "sur un bus"})) {
            setRole(params::InstanceRole::Bus);
            return "RÔLE : Bus. Le son passe tel quel ; lance la lecture, je te dis ce qu'il y a dans ce bus (page COMPOSITION).";
        }
        if (hasAny({"role master", "mode master", "mode mastering"})) {
            setRole(params::InstanceRole::Master);
            return "RÔLE : Master. La chaîne de mastering est de retour.";
        }
        if (hasAny({"reecoute", "re ecoute", "relisten", "ecoute a nouveau", "reecouter"})) return relistenBuses();
        if (hasAny({"composition", "dans mes bus", "dans le bus", "dans ce bus", "mes stems", "quels elements", "quel element", "what s in", "my stems",
                    "my buses"}))
            return compositionText();
        if (instanceRole() != params::InstanceRole::Master)
            return i18n::tr("Cette instance est en RÔLE Bus : elle écoute sans toucher au son. Demande « composition », ou « rôle master » pour masteriser ici.");
    }
    if (!feed_) return "OMNIS ne trouve pas son moteur : réessaie dans un instant.";
    // Un style nommé sert aussi à AUTO.
    const auto lib = libraryWithUser();
    std::string named;
    for (const auto& g : lib.genres())
        if (ai::namesMatch(request, g) || request.find(g) != std::string::npos) named = g;
    if (!named.empty()) {
        autoStyle_ = named;
        styleNamed_ = true;
    }
    // « c'est quel style ? » : ce qu'OMNIS reconnaît dans la prod entendue.
    {
        const std::string plain = ai::normalizeText(request);
        static constexpr std::string_view kWhich[] = {"quel style", "quel genre", "c est quoi le style", "c est quel style", "reconnais le style",
                                                      "what style", "which style", "what genre"};
        for (const auto phrase : kWhich)
            if (plain.find(phrase) != std::string::npos) {
                const auto heard = feed_->prod.snapshot();
                if (heard.seconds < 8.0) return "Lance la lecture : je reconnais le style après quelques secondes d'écoute.";
                return recognizeHeard(heard.signature, nullptr);
            }
    }
    const auto snap = feed_->prod.snapshot();
    if (snap.seconds < 8.0) {
        pendingAsk_ = request;
        return "Lance la lecture : OMNIS écoute ta prod quelques secondes, puis applique ta demande.";
    }
    std::string answer = runRequest(request);
    // Un style nommé sur une prod entendue : OMNIS s'en souvient pour les prochaines.
    if (!named.empty()) {
        if (const std::string note = learnStyle(named); !note.empty()) answer += " " + note;
    }
    return answer;
}

std::vector<std::string> OmnisController::exportProblems() {
    using P = params::ParamId;
    const auto v = [this](P id) { return plainOf(static_cast<std::uint32_t>(id)); };
    std::vector<std::string> out;
    // Outils d'écoute restés allumés : le fichier exporté ne serait pas le master.
    if (v(P::Monitor) >= 0.5) out.push_back(v(P::Monitor) >= 1.5 ? "écoute SIDE active : seul le Side serait exporté" : "écoute MID active : seul le Mid serait exporté");
    if (v(P::DeltaListen) >= 0.5) out.push_back("DELTA actif : seule la différence d'un module serait exportée");
    if (v(P::GainMatch) >= 0.5) out.push_back("MATCH actif : le volume exporté serait celui de l'original");
    if (v(P::LowLatency) >= 0.5) out.push_back("mode TRAVAIL actif : qualité réduite (repasse en qualité max)");
    if (v(P::ListenSim) >= 0.5) out.push_back("écoute simulée active : remets ÉCOUTE sur Off");
    if (v(P::Bypass) >= 0.5) out.push_back("BYPASS actif : OMNIS ne traite rien");
    // Le son de sortie mesuré (depuis la lecture).
    const double tp = v(P::MeterTruePeak), lufs = v(P::MeterIntegrated);
    if (tp > 0.0) out.push_back(format("la sortie dépasse 0 dBTP (%.1f) : distorsion au décodage", tp));
    else if (tp > -0.9) out.push_back(format("true peak à %.1f dBTP : au-dessus de -1 dBTP conseillé pour le streaming", tp));
    if (lufs > -60.0 && lufs > -6.0) out.push_back(format("sonie très forte (%.1f LUFS) : les plateformes vont la baisser", lufs));
    return out;
}

std::string OmnisController::mixDiagnosis() {
    if (!feed_) requestFeed();
    if (!feed_) return "OMNIS ne trouve pas son moteur : réessaie dans un instant.";
    const auto snap = feed_->prod.snapshot();
    if (snap.seconds < 8.0) return "Lance la lecture (le passage le plus chargé) : OMNIS écoute ta prod quelques secondes, puis RÉÉCOUTER.";
    const auto lib = libraryWithUser();
    const auto refs = referenceEntries(lib, refKind_, refName_);
    if (refs.empty()) return ai::formatDiagnosis(ai::diagnoseMix(snap.signature, nullptr));
    const auto target = ai::aggregateSignature(refs);
    return ai::formatDiagnosis(ai::diagnoseMix(snap.signature, &target));
}

std::string OmnisController::runRequest(const std::string& text) {
    const auto lib = libraryWithUser();
    const auto u = ai::understandRequest(text, ai::Lexicon::builtin(), lib, false);
    if (u.question) return *u.question;
    const auto snap = feed_->prod.snapshot();
    const auto plan = ai::planRequest(u, snap.signature, [this](params::ParamId pid) { return plainOf(static_cast<std::uint32_t>(pid)); });
    if (!plan.settings.empty()) rememberForUndo(text);
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

std::size_t OmnisController::ReferenceStyle::titleCount() const {
    std::size_t n = 0;
    for (const auto& a : artists) n += a.titles.size();
    return n;
}

namespace {

/// Une image de la fiche : un fichier du dossier images/ de l'utilisateur, s'il existe.
std::string imagePath(const std::string& file) {
    if (file.empty() || file.find("..") != std::string::npos) return {};
    std::error_code ec;
    const std::u8string name(reinterpret_cast<const char8_t*>(file.data()), file.size());
    const auto path = userDataDir() / "images" / std::filesystem::path(name);
    if (!std::filesystem::is_regular_file(path, ec)) return {};
    const auto utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

/// La pochette d'un titre : son illustration (cache), sinon une ancienne image.
std::string coverOf(const ai::ReferenceEntry& e) {
    if (!e.title.empty())
        if (auto cached = gfx::avatars::existing(gfx::avatars::coverPath(e.artist, e.title)); !cached.empty()) return cached;
    return imagePath(e.cover);
}

/// Les titres mesurés regroupés par artiste (photo : la première fiche qui en a une, avec son crédit).
std::vector<OmnisController::ReferenceArtist> byArtist(const std::vector<const ai::ReferenceEntry*>& entries) {
    std::vector<OmnisController::ReferenceArtist> out;
    for (const auto* e : entries) {
        auto it = std::find_if(out.begin(), out.end(), [&](const auto& a) { return a.name == e->artist; });
        if (it == out.end()) {
            out.push_back({e->artist, {}, {}, {}});
            it = out.end() - 1;
        }
        if (it->photo.empty()) {
            // L'illustration du cache (omnis_refdb), sinon une ancienne image avec son crédit.
            it->photo = gfx::avatars::existing(gfx::avatars::artistPath(e->artist));
            if (!it->photo.empty()) it->credit = e->imageCredit.empty() ? std::string("Illustration OMNIS") : e->imageCredit;
            else if (!e->imageCredit.empty()) {
                it->photo = imagePath(e->image);
                if (!it->photo.empty()) it->credit = e->imageCredit;
            }
        }
        if (std::none_of(it->titles.begin(), it->titles.end(), [&](const auto& t) { return t.name == e->displayName(); }))
            it->titles.push_back({e->displayName(), e->title.empty() ? e->artist : e->title, coverOf(*e), e->signature});
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return lessNoCase(a.name, b.name); });
    for (auto& a : out) std::sort(a.titles.begin(), a.titles.end(), [](const auto& x, const auto& y) { return lessNoCase(x.title, y.title); });
    return out;
}

} // namespace

namespace {
const std::vector<std::pair<std::string, std::vector<std::string>>>& families() { return ai::genreFamilies(); }
/// Un titre pèse ses titres : un profil OMNIS de 40 titres en compte 40.
int weightOf(const ai::ReferenceEntry& e) { return e.isProfile() ? e.profileTitles : 1; }
} // namespace

OmnisController::ReferenceTree OmnisController::referenceTree(const std::string& family, const std::string& style) const {
    const auto lib = libraryWithUser();
    std::vector<std::string> words;
    for (const auto& [name, w] : families())
        if (name == family) words = w;
    const bool filtered = !family.empty();
    ReferenceTree tree;
    const auto hasStyle = [&](const ai::ReferenceEntry& e) {
        return style.empty() || std::any_of(e.genres.begin(), e.genres.end(), [&](const std::string& g) { return g == style; });
    };
    for (const auto& g : lib.genres()) {
        if (!style.empty() && g != style) continue;
        std::vector<const ai::ReferenceEntry*> entries;
        for (const auto* e : lib.measuredWithGenre(g))
            if (!filtered || ai::inGenreFamily(*e, family, words)) entries.push_back(e);
        if (!entries.empty()) tree.styles.push_back({g, byArtist(entries)});
    }
    std::sort(tree.styles.begin(), tree.styles.end(), [](const auto& a, const auto& b) { return lessNoCase(a.name, b.name); });
    std::vector<const ai::ReferenceEntry*> all;
    for (const auto& e : lib.entries())
        if (playable(e) && (!filtered || ai::inGenreFamily(e, family, words)) && hasStyle(e)) all.push_back(&e);
    tree.artists = byArtist(all);
    return tree;
}

std::vector<OmnisController::FamilyFilter> OmnisController::referenceFilters() const {
    const auto lib = libraryWithUser();
    std::vector<FamilyFilter> out;
    for (const auto& [name, words] : families()) {
        FamilyFilter f;
        f.name = name;
        std::map<std::string, int> counts;
        for (const auto& e : lib.entries()) {
            if (!playable(e) || !ai::inGenreFamily(e, name, words)) continue;
            // Le profil de la famille compte ses titres ; ceux des sous-genres en font
            // partie (pas comptés deux fois).
            const bool familyProfile = e.isProfile() && e.title == name;
            if (!e.isProfile() || familyProfile) f.titles += weightOf(e);
            if (familyProfile) continue;
            for (const auto& g : e.genres)
                if (g != name && std::none_of(words.begin(), words.end(), [&](const std::string& w) { return ai::namesMatch(g, w); })) counts[g] += weightOf(e);
        }
        if (f.titles == 0) continue;
        for (const auto& [g, n] : counts) f.styles.emplace_back(g, n);
        std::sort(f.styles.begin(), f.styles.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
        out.push_back(std::move(f));
    }
    return out;
}

std::vector<std::string> OmnisController::referenceFamilies() const {
    const auto lib = libraryWithUser();
    std::vector<std::string> out;
    for (const auto& [name, words] : families())
        for (const auto& e : lib.entries())
            if (playable(e) && ai::inGenreFamily(e, name, words)) {
                out.push_back(name);
                break;
            }
    return out;
}

OmnisController::ReferenceCard OmnisController::referenceCard() const {
    ReferenceCard card;
    if (refKind_ == RefKind::None) return card;
    const auto lib = libraryWithUser();
    const auto refs = referenceEntries(lib, refKind_, refName_);
    if (refs.empty()) return card;
    card.name = refName_;
    card.titles = 0;
    for (const auto* e : refs) card.titles += weightOf(*e); // un profil compte ses titres
    card.signature = ai::aggregateSignature(refs);
    // Photo : un artiste (ou le titre d'un seul artiste) ; pochette : un titre.
    const auto artists = byArtist(refs);
    if (artists.size() == 1) {
        card.photo = artists.front().photo;
        card.credit = artists.front().credit;
    }
    if (refKind_ == RefKind::Title) {
        card.cover = coverOf(*refs.front());
        if (!card.cover.empty()) card.coverCredit = refs.front()->coverCredit.empty() ? std::string("Illustration OMNIS") : refs.front()->coverCredit;
        card.link = refs.front()->link;
    } else if (refKind_ == RefKind::Artist) {
        // La page de l'artiste : sur sa fiche de nom (sans titre), sinon aucune.
        for (const auto& e : lib.entries())
            if (e.artist == refName_ && e.title.empty() && !e.link.empty()) card.link = e.link;
    }
    char b[96];
    const auto& m = refs.front()->signature.music;
    if (refs.size() == 1 && m.valid && !refs.front()->isProfile()) {
        std::snprintf(b, sizeof b, "%.0f BPM · %s · %.1f LUFS", m.bpm, m.keyRoot >= 0 ? m.keyName().c_str() : "tonalité ?", refs.front()->signature.integratedLufs);
    } else {
        std::snprintf(b, sizeof b, "%d titre%s · %.1f LUFS en moyenne", card.titles, card.titles > 1 ? "s" : "", card.signature.integratedLufs);
    }
    card.detail = b;
    return card;
}

std::string OmnisController::chooseReference(RefKind kind, const std::string& name) {
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
    noteRetouch(); // la séance précédente : ce que la personne en a gardé
    if (!settings.empty()) rememberForUndo("référence " + refName_);
    applyPlan(settings);
    // Un preset choisi : une séance d'apprentissage commence (comme après AUTO).
    autoFeatures_ = ai::styleFeatures(feed_->prod.snapshot().signature);
    noteAuto(ai::SessionStart::Preset, (refKind_ == RefKind::Style ? "style " : "") + refName_);
    if (plan.targetLufs) {
        loudTarget_ = *plan.targetLufs;
        pursuing_ = true;
        pursuitTicks_ = pursuitStable_ = pursuitSteps_ = 0;
    }
    return "Fait : " + joined(plan.said, " · ") + ".";
}

std::string OmnisController::startAuto() {
    if (!feed_) requestFeed();
    if (!feed_) return "AUTO : le son n'arrive pas encore au plug-in ; relance la lecture.";
    if ((autoJob_ && !autoJob_->done.load()) || fileJob_) return autoStatus(); // fichier ou essais en cours : ils finissent
    if (autoRunning_) {
        // Second clic : pendant l'écoute, elle finit maintenant ; avant le son, AUTO s'annule.
        if (feed_->prod.autoState() == ProdListener::AutoState::Listening) {
            feed_->prod.finishAuto();
            return "AUTO : fin de l'écoute, réglage de la chaîne…";
        }
        feed_->prod.cancelAuto();
        autoRunning_ = false;
        return "AUTO annulé.";
    }
    pursuing_ = false;
    feed_->prod.startAuto();
    autoRunning_ = true;
    return autoStatus();
}

double OmnisController::autoProgress() const {
    if (autoJob_ || fileJob_) return 1.0; // fichier lu, essais en cours, ou finis et pas encore appliqués
    if (!feed_ || !autoRunning_) return -1.0;
    return feed_->prod.autoState() == ProdListener::AutoState::Waiting ? 0.0 : feed_->prod.autoProgress();
}

std::string OmnisController::autoStatus() const {
    char b[256];
    if (fileJob_) return i18n::tr("AUTO : j'analyse « ") + fileJob_->name + i18n::tr(" » en entier…");
    if (autoJob_ && !autoJob_->done.load()) {
        const int total = std::max(1, autoJob_->total.load());
        std::snprintf(b, sizeof b, i18n::tr("AUTO affine : essai %d/%d sur le passage le plus fort…").c_str(),
                      std::min(total, autoJob_->step.load() + 1), total);
        return b;
    }
    if (!feed_ || !autoRunning_) return {};
    if (feed_->prod.autoState() != ProdListener::AutoState::Listening)
        return "AUTO : lance la lecture du morceau, du début à la fin ; OMNIS l'écoute en entier.";
    const int s = static_cast<int>(feed_->prod.autoSeconds());
    std::snprintf(b, sizeof b,
                  i18n::tr(s < static_cast<int>(ProdListener::kAutoMinSeconds)
                               ? "AUTO : écoute %d:%02d…"
                               : "AUTO : écoute %d:%02d — arrête la lecture (ou reclique AUTO) à la fin du morceau.")
                      .c_str(),
                  s / 60, s % 60);
    return b;
}

void OmnisController::startAutoSearch(std::vector<float> excerpt, double sampleRate, double targetLufs) {
    if (autoThread_.joinable()) autoThread_.join();
    // Le départ des essais : la console telle qu'AUTO vient de la régler (clé -> valeur).
    std::vector<std::pair<std::string, double>> base;
    // Les outils d'écoute (bypass, Mid / Side seul) ne changent pas le master exporté : les
    // essais se font sur le son réel.
    for (const auto& spec : params::allParameters())
        if (params::isPersistent(spec) && spec.id != params::ParamId::Bypass && spec.id != params::ParamId::Monitor)
            base.emplace_back(std::string(spec.key), plainOf(static_cast<std::uint32_t>(spec.id)));
    // Garde-fou : la marge de crête de la prod (gain qui ne compresse rien) + le plafond du NIVEAU.
    const auto level = static_cast<params::LoudnessLevel>(
        std::clamp(static_cast<int>(std::lround(plainOf(static_cast<std::uint32_t>(params::ParamId::LoudnessLevel)))), 0, 4));
    const double ceiling = plainOf(static_cast<std::uint32_t>(params::ParamId::LimCeiling));
    const double headroom = heardPeakDb_ > -100.0 ? std::max(0.0, ceiling - heardPeakDb_) : 0.0;
    const double maxGain = headroom + params::masteringGainCapDb(level);
    auto job = std::make_shared<AutoJob>();
    job->targetLufs = targetLufs;
    autoJob_ = job;
    autoThread_ = std::thread([job, excerpt = std::move(excerpt), sampleRate, base = std::move(base), targetLufs, maxGain] {
        // Rien ne doit sortir de ce thread : une exception non rattrapée arrêterait le DAW.
        try {
            tools::AudioFile file;
            file.sampleRate = sampleRate;
            const std::size_t frames = excerpt.size() / 2;
            file.resize(2, static_cast<int>(frames));
            for (std::size_t i = 0; i < frames; ++i) {
                file.channels[0][i] = excerpt[2 * i];
                file.channels[1][i] = excerpt[2 * i + 1];
            }
            tools::SearchOptions opt;
            opt.targetLufs = targetLufs;
            opt.maxGainDb = maxGain;
            opt.cancel = &job->cancel;
            opt.progress = [&job](int done, int total) {
                job->total.store(total);
                job->step.store(done);
            };
            const auto found = tools::searchMaster(file, base, opt);
            if (!found.cancelled && found.best >= 0) {
                const auto& best = found.tried[static_cast<std::size_t>(found.best)];
                for (const auto& [key, v] : tools::settingsOf(best)) {
                    if (key == "clip_drive") job->clipDrive = v;
                    if (key == "lim_gain") job->limGain = v;
                }
                job->trials = static_cast<int>(found.tried.size());
                job->name = best.strategy.name;
                job->distortionDb = best.distortion.weightedDb;
                job->reachedLufs = best.integratedLufs;
                job->ok = true;
            }
        } catch (...) {
            job->ok = false;
        }
        job->done.store(true);
    });
}

std::string OmnisController::finishAutoSearch() {
    const auto job = std::move(autoJob_);
    autoJob_.reset();
    if (autoThread_.joinable()) autoThread_.join();
    if (!job || job->cancel.load()) return {};
    // Le volume est ensuite tenu à l'écoute (sortie mesurée), dans le même garde-fou. La sonie
    // court terme (3 s) et la latence de la chaîne : le compteur ne montre le nouveau réglage
    // qu'après ~4 s, la poursuite attend jusque-là (sinon elle pousse sur une mesure périmée).
    pursuing_ = true;
    pursuitStable_ = pursuitSteps_ = 0;
    pursuitTicks_ = -kPursuitSettleTicks;
    if (!job->ok) return "AUTO : les essais n'ont pas abouti ; le volume est mené à l'écoute.";
    if (job->trials == 1 && job->limGain <= 1e-9 && job->clipDrive <= 1e-9 && job->reachedLufs > job->targetLufs + 0.5) {
        // Déjà plus forte que le NIVEAU : OMNIS ne baisse jamais le volume d'une prod.
        pursuing_ = false;
        OmnisEdit omnis(*this);
        for (const auto pid : {params::ParamId::ClipDrive, params::ParamId::LimGain})
            if (const auto* spec = params::findParameter(pid)) editParameter(static_cast<std::uint32_t>(pid), params::toNormalized(*spec, 0.0));
        char b[320];
        std::snprintf(b, sizeof b,
                      i18n::tr("AUTO : ta prod est déjà à %.1f LUFS, plus fort que le NIVEAU (%.1f) : rien n'est poussé. "
                               "Pour la baisser, descends son volume avant OMNIS ; pour garder ce volume, choisis un NIVEAU plus fort.")
                          .c_str(),
                      job->reachedLufs, job->targetLufs);
        return b;
    }
    {
        OmnisEdit omnis(*this);
        for (const auto& [pid, v] : {std::pair{params::ParamId::ClipDrive, job->clipDrive}, std::pair{params::ParamId::LimGain, job->limGain}})
            if (const auto* spec = params::findParameter(pid))
                editParameter(static_cast<std::uint32_t>(pid), params::toNormalized(*spec, params::clampPlain(*spec, v)));
    }
    char b[320];
    std::snprintf(b, sizeof b,
                  i18n::tr("AUTO affiné : %d essais sur le passage le plus fort, retenu « %s » (clip +%.1f dB, limiteur +%.1f dB, "
                           "distorsion %.1f dB). Tout reste réglable à la main.")
                      .c_str(),
                  job->trials, i18n::tr(job->name).c_str(), job->clipDrive, job->limGain, job->distortionDb);
    return b;
}

std::string OmnisController::applyAuto(const analysis::SpectralSignature& loud, std::vector<float> excerpt, double sr) {
    heardPeakDb_ = std::max(loud.truePeakDb, loud.music.valid ? loud.music.samplePeakDb : -200.0); // crête du morceau écouté
    const auto lib = libraryWithUser();
    const auto current = [this](params::ParamId pid) { return plainOf(static_cast<std::uint32_t>(pid)); };
    // La référence choisie au menu passe avant tout ; puis le style nommé ; sinon OMNIS le reconnaît.
    const auto chosen = referenceEntries(lib, refKind_, refName_);
    std::string recognition;
    if (chosen.empty() && !styleNamed_) {
        std::string style;
        recognition = recognizeHeard(loud, &style);
        if (!style.empty()) autoStyle_ = style;
    }
    const auto plan = chosen.empty() ? ai::planAuto(loud, lib, autoStyle_, current) : ai::planAuto(loud, chosen, refName_, current);
    std::vector<std::pair<std::uint32_t, double>> settings;
    for (const auto& [pid, v] : plan.settings) settings.emplace_back(static_cast<std::uint32_t>(pid), v);
    noteRetouch(); // l'AUTO précédent : ce que la personne en a gardé
    std::optional<double> target = plan.targetLufs;
    const std::string habits = applyHabits(loud, settings, target);
    rememberForUndo("AUTO");
    applyPlan(settings);
    autoFeatures_ = ai::styleFeatures(loud);
    noteAuto(ai::SessionStart::Auto, recognizedGroup_.empty() ? autoStyle_ : recognizedGroup_);
    // AUTO précis : assez de son gardé (4 s au moins) -> essais clip / limiteur ; sinon le
    // volume est mené à l'écoute.
    const bool search = target && sr > 0.0 && static_cast<double>(excerpt.size()) >= 2.0 * 4.0 * sr;
    if (target) {
        loudTarget_ = *target;
        pursuitTicks_ = pursuitStable_ = pursuitSteps_ = 0;
        pursuing_ = !search;
        if (search) startAutoSearch(std::move(excerpt), sr, *target);
    }
    return (recognition.empty() ? std::string() : recognition + " ") + joined(plan.said, " · ") + (habits.empty() ? "" : " · " + habits) +
           (search ? ". Essais clip / limiteur en cours…" : ". Tout reste réglable à la main. (« annule » pour revenir en arrière)");
}

std::string OmnisController::recognizeHeard(const analysis::SpectralSignature& heard, std::string* style) {
    const auto memory = loadStyleMemory();
    const auto rec = ai::recognizeGroup(heard, soundGroups(), {}, &memory);
    if (rec.empty()) return {};
    if (style) {
        *style = ai::styleForGroup(rec.ranked.front().style);
        recognizedGroup_ = rec.ranked.front().style;
    }
    std::string text = ai::describeRecognition(rec);
    if (style && !rec.confident) text += " " + i18n::tr("Je vise le premier ; écris le style si je me trompe, je m'en souviendrai.");
    return text;
}

std::array<double, 6> OmnisController::habitValues() {
    using P = params::ParamId;
    const auto v = [this](P id) { return plainOf(static_cast<std::uint32_t>(id)); };
    const auto w = [this](int band) { return plainOf(static_cast<std::uint32_t>(P::ImgWidthFirst) + static_cast<std::uint32_t>(band)); };
    return {v(P::MatchTilt), v(P::SubGain), v(P::InflHighEffect), v(P::ClipDrive) + v(P::LimGain), 0.5 * (w(2) + w(3)),
            v(P::CompOn) >= 0.5 ? v(P::CompThreshold) : 0.0};
}

std::string OmnisController::applyHabits(const analysis::SpectralSignature& heard, std::vector<std::pair<std::uint32_t, double>>& settings,
                                         std::optional<double>& targetLufs) {
    const auto net = loadHabitNet();
    if (!net || !net->trained()) return {};
    const auto d = net->predict(ai::styleFeatures(heard));
    using P = params::ParamId;
    // Valeur après le plan d'AUTO (le plan, sinon la console), plus l'écart appris.
    const auto planned = [&](P id) {
        for (const auto& [pid, v] : settings)
            if (pid == static_cast<std::uint32_t>(id)) return v;
        return plainOf(static_cast<std::uint32_t>(id));
    };
    const auto put = [&](P id, double v) {
        for (auto& [pid, x] : settings)
            if (pid == static_cast<std::uint32_t>(id)) {
                x = v;
                return;
            }
        settings.emplace_back(static_cast<std::uint32_t>(id), v);
    };
    const auto at = [&](ai::Habit h) { return d[static_cast<std::size_t>(h)]; };
    if (std::fabs(at(ai::Habit::Tilt)) >= 0.1) put(P::MatchTilt, planned(P::MatchTilt) + at(ai::Habit::Tilt));
    if (std::fabs(at(ai::Habit::Sub)) >= 0.1) put(P::SubGain, planned(P::SubGain) + at(ai::Habit::Sub));
    if (std::fabs(at(ai::Habit::Inflate)) >= 1.0) put(P::InflHighEffect, planned(P::InflHighEffect) + at(ai::Habit::Inflate));
    if (std::fabs(at(ai::Habit::Width)) >= 2.0)
        for (int band : {2, 3}) {
            const auto id = static_cast<P>(static_cast<int>(P::ImgWidthFirst) + band);
            put(id, planned(id) + at(ai::Habit::Width));
        }
    if (std::fabs(at(ai::Habit::Comp)) >= 0.5 && planned(P::CompOn) >= 0.5) put(P::CompThreshold, planned(P::CompThreshold) + at(ai::Habit::Comp));
    // Le volume : la cible bouge (le garde-fou du NIVEAU tient toujours).
    if (targetLufs && std::fabs(at(ai::Habit::Loudness)) >= 0.2) *targetLufs += at(ai::Habit::Loudness);
    return ai::describeHabits(d, net->samples());
}

ai::UsageState OmnisController::usageState() {
    using P = params::ParamId;
    const auto v = [this](P id) { return plainOf(static_cast<std::uint32_t>(id)); };
    ai::UsageState s;
    s.habits = habitValues();
    static constexpr P kOn[8] = {P::SubOn, P::MatchOn, P::InflOn, P::ImgOn, P::MbOn, P::ClipOn, P::LimOn, P::CompOn};
    for (int m = 0; m < 8; ++m) s.modules[static_cast<std::size_t>(m)] = v(kOn[m]) >= 0.5;
    s.inflCharacter = static_cast<int>(std::lround(v(P::InflCharacter)));
    s.compCharacter = static_cast<int>(std::lround(v(P::CompCharacter)));
    s.matchCharacter = static_cast<int>(std::lround(v(P::MatchCharacter)));
    s.lowWidth = 0.5 * (plainOf(static_cast<std::uint32_t>(P::ImgWidthFirst)) + plainOf(static_cast<std::uint32_t>(P::ImgWidthFirst) + 1));
    return s;
}

void OmnisController::noteAuto(ai::SessionStart start, const std::string& context) {
    autoProposal_ = habitValues();
    autoPending_ = true;
    session_ = {};
    session_.start = start;
    session_.context = context;
    session_.proposed = usageState();
    lastHabits_ = session_.proposed.habits;
    ai::JournalEvent e;
    e.kind = start == ai::SessionStart::Preset ? ai::JournalKind::Preset : ai::JournalKind::Auto;
    e.time = static_cast<std::int64_t>(std::time(nullptr));
    e.group = recognizedGroup_;
    e.features = autoFeatures_;
    for (int i = 0; i < ai::kHabits; ++i) e.values[static_cast<std::size_t>(i)] = autoProposal_[static_cast<std::size_t>(i)];
    std::error_code ec;
    std::filesystem::create_directories(learningDir(), ec);
    ai::appendJournal(pathText(journalPath()), e);
}

void OmnisController::noteRetouch(bool exported) {
    if (!autoPending_) return;
    autoPending_ = false;
    session_.kept = usageState();
    session_.exported = exported;
    // Le juge : chaque geste (ce qui a changé depuis la proposition) est-il utile ? Le graphe des
    // usages le garde ; le réseau n'apprend que les gestes utiles (rien retouché : écart nul, la
    // proposition convenait, c'est aussi une leçon).
    const auto gestures = ai::judgeSession(session_);
    std::error_code ec;
    std::filesystem::create_directories(learningDir(), ec);
    auto graph = ai::loadUsageGraph(pathText(usageGraphPath()));
    graph.record(session_, gestures);
    ai::saveUsageGraph(pathText(usageGraphPath()), graph);
    ai::JournalEvent e;
    e.kind = ai::JournalKind::Retouch;
    e.time = static_cast<std::int64_t>(std::time(nullptr));
    e.group = recognizedGroup_;
    e.features = autoFeatures_;
    e.values = ai::usefulDeltas(session_, gestures);
    ai::appendJournal(pathText(journalPath()), e);
    // Le réseau réapprend de tout le journal (quelques millisecondes).
    ai::HabitNet net;
    if (net.train(ai::samplesFromJournal(ai::loadJournal(pathText(journalPath())))))
        std::ofstream(habitNetPath(), std::ios::binary) << net.serialize();
}

std::string OmnisController::learnStyle(const std::string& genre) {
    const auto heard = feed_ ? feed_->prod.snapshot() : ProdListener::Snapshot{};
    if (heard.seconds < 8.0) return {};
    std::string group = ai::sonicGroup("", genre);
    if (group.empty()) group = ai::sonicGroup(genre, "");
    if (group.empty()) group = genre; // un style que seule la personne nomme
    auto memory = loadStyleMemory();
    memory.push_back({group, ai::styleFeatures(heard.signature)});
    if (memory.size() > 400) memory.erase(memory.begin(), memory.begin() + static_cast<std::ptrdiff_t>(memory.size() - 400));
    std::error_code ec;
    std::filesystem::create_directories(styleMemoryPath().parent_path(), ec);
    std::ofstream(styleMemoryPath(), std::ios::binary) << ai::serializeStyleExamples(memory);
    char b[200];
    std::snprintf(b, sizeof b, i18n::tr("Noté : cette prod, c'est %s. Je m'en souviendrai pour reconnaître les suivantes (%d exemple(s)).").c_str(),
                  i18n::tr(group).c_str(), static_cast<int>(memory.size()));
    return b;
}

std::string OmnisController::autoFromFile(const std::string& utf8Path) {
    if ((autoJob_ && !autoJob_->done.load()) || fileJob_) return autoStatus();
    if (autoRunning_ && feed_) feed_->prod.cancelAuto(); // le fichier remplace l'écoute en cours
    autoRunning_ = false;
    pursuing_ = false;
    if (fileThread_.joinable()) fileThread_.join();
    const std::filesystem::path path(std::u8string(utf8Path.begin(), utf8Path.end()));
    auto job = std::make_shared<FileJob>();
    {
        const auto name = path.filename().u8string();
        job->name = std::string(name.begin(), name.end());
    }
    fileJob_ = job;
    fileThread_ = std::thread([job, path] {
        // Rien ne doit sortir de ce thread : une exception non rattrapée arrêterait le DAW.
        try {
            tools::AudioFile audio;
            if (!tools::readAudioFile(path, audio, job->error) || audio.numFrames() <= 0 || audio.numChannels() <= 0) {
                job->ok = false;
            } else {
                if (audio.numChannels() == 1) audio.channels.push_back(audio.channels[0]); // mono : les deux côtés
                audio.channels.resize(2);
                const int n = audio.numFrames();
                job->signature = analysis::analyzeSignature(audio.channels[0].data(), audio.channels[1].data(), n, audio.sampleRate);
                job->signature.music = analysis::analyzeMusic(audio.channels[0].data(), audio.channels[1].data(), n, audio.sampleRate);
                const auto loudest = tools::loudestExcerpt(audio, ProdListener::kExcerptSeconds);
                const std::size_t frames = static_cast<std::size_t>(loudest.numFrames());
                job->excerpt.resize(2 * frames);
                for (std::size_t i = 0; i < frames; ++i) {
                    job->excerpt[2 * i] = static_cast<float>(loudest.channels[0][i]);
                    job->excerpt[2 * i + 1] = static_cast<float>(loudest.channels[1][i]);
                }
                job->sampleRate = audio.sampleRate;
                job->ok = true;
            }
        } catch (...) {
            job->ok = false;
        }
        job->done.store(true);
    });
    return i18n::tr("AUTO : j'analyse « ") + job->name + i18n::tr(" » en entier…");
}

std::string OmnisController::pollAutomation() {
    // Un fichier de référence ajouté : sa réponse.
    if (import_ && import_->done.load()) {
        if (import_->ok) libraryChanged_ = true;
        std::string message = import_->message;
        import_.reset();
        if (importThread_.joinable()) importThread_.join();
        return message;
    }
    if (!feed_) return {};
    std::string message;
    // AUTO terminé : la chaîne vers le style.
    analysis::SpectralSignature loud;
    std::vector<float> excerpt;
    if (autoRunning_ && feed_->prod.takeAutoResult(loud, &excerpt)) {
        autoRunning_ = false;
        message = applyAuto(loud, std::move(excerpt), feed_->prod.sampleRate());
    }
    // Un fichier glissé, analysé : le même AUTO.
    if (fileJob_ && fileJob_->done.load()) {
        const auto job = std::move(fileJob_);
        fileJob_.reset();
        if (fileThread_.joinable()) fileThread_.join();
        if (job->ok) {
            message = applyAuto(job->signature, std::move(job->excerpt), job->sampleRate);
        } else {
            message = job->name + " : " + i18n::tr("fichier illisible") + (job->error.empty() ? "" : " (" + job->error + ")") + ".";
        }
    }
    // Essais d'AUTO précis terminés : le meilleur appliqué.
    if (autoJob_ && autoJob_->done.load()) message = finishAutoSearch();
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
    // NIVEAU choisi : une fois la prod entendue, le volume y est mené (« Style » : rien).
    if (levelChanged_ && feed_->prod.snapshot().seconds >= 8.0) {
        levelChanged_ = false;
        if (autoJob_) autoJob_->cancel = true; // essais faits pour l'ancien NIVEAU : abandonnés
        const auto level = static_cast<params::LoudnessLevel>(std::lround(plainOf(static_cast<std::uint32_t>(params::ParamId::LoudnessLevel))));
        if (level != params::LoudnessLevel::Style) {
            loudTarget_ = params::loudnessLevelLufs(level);
            pursuing_ = true;
            pursuitTicks_ = pursuitStable_ = pursuitSteps_ = 0;
        }
    }
    // Le volume mené vers la cible par la mesure (sonie court terme de la sortie).
    if (pursuing_ && !autoJob_ && ++pursuitTicks_ > 0 && pursuitTicks_ % 18 == 0) {
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
                // Garde-fou : le gain de mastering total (clip + limiteur) du NIVEAU choisi.
                const auto level = static_cast<params::LoudnessLevel>(
                    std::clamp(static_cast<int>(std::lround(plainOf(static_cast<std::uint32_t>(params::ParamId::LoudnessLevel)))), 0, 4));
                const double clip = plainOf(static_cast<std::uint32_t>(params::ParamId::ClipDrive));
                // La marge de crête de la prod entendue : ce gain-là ne compresse rien.
                const double ceiling = plainOf(static_cast<std::uint32_t>(params::ParamId::LimCeiling));
                const auto heard = feed_->prod.snapshot().signature;
                const double peak = std::max({heardPeakDb_, heard.truePeakDb, heard.music.valid ? heard.music.samplePeakDb : -200.0});
                const double headroom = peak > -100.0 ? std::max(0.0, ceiling - peak) : 0.0;
                const double allowed = std::max(0.0, headroom + params::masteringGainCapDb(level) - clip);
                const double wanted = std::clamp(plainOf(lim) + std::clamp(0.6 * err, -1.5, 1.5), 0.0, 24.0);
                const double gain = std::min(wanted, std::max(allowed, std::min(plainOf(lim), wanted)));
                if (const auto* spec = params::findParameter(lim)) {
                    OmnisEdit omnis(*this);
                    editParameter(lim, params::toNormalized(*spec, gain));
                }
                if (err < 0.0 && gain <= 1e-6 && clip <= 1e-6) {
                    // Plus forte que la cible sans rien pousser : OMNIS ne baisse pas une prod.
                    pursuing_ = false;
                    char b[320];
                    std::snprintf(b, sizeof b,
                                  i18n::tr("AUTO : ta prod est déjà à %.1f LUFS, plus fort que le NIVEAU (%.1f) : rien n'est poussé. "
                                           "Pour la baisser, descends son volume avant OMNIS ; pour garder ce volume, choisis un NIVEAU plus fort.")
                                      .c_str(),
                                  st, loudTarget_);
                    message = b;
                } else if (err > 0.0 && gain >= allowed - 1e-6 && wanted > gain + 1e-6) {
                    pursuing_ = false;
                    char b[320];
                    std::snprintf(b, sizeof b,
                                  i18n::tr("Volume arrêté à %.1f LUFS : au-delà, ta prod s'écraserait (passages calmes remontés, bas-médium gonflé). "
                                           "Pour aller plus fort, rends-la plus dense dans ton projet (808 et kick compressés ou saturés), "
                                           "ou choisis un NIVEAU plus fort.").c_str(),
                                  st);
                    message = b;
                } else if (++pursuitSteps_ > 40) {
                    pursuing_ = false;
                    message = "Volume : le limiteur est au maximum raisonnable ; pousse l'entrée ou le clip si tu veux plus fort.";
                }
            }
        }
    }
    if (!message.empty()) lastMessage_ = message;
    return message;
}

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
            if (const auto migrated = params::migrateLegacyValue(id, v))
                mem.emplace_back(static_cast<std::uint32_t>(migrated->first), migrated->second);
            else if (params::findParameter(id))
                mem.emplace_back(id, std::clamp(v, 0.0, 1.0));
        }
    }
    // Version 2 : la référence choisie au menu (gardée avec la session).
    int32 kind = 0;
    int32 length = 0;
    if (version >= 2 && s.readInt32(kind) && s.readInt32(length)) {
        // Même format que FStreamer::writeStr8 (longueur, zéro final compris), lu ici
        // à la main : readStr8 rend une mémoire de malloc (pas de delete[]) et, sur
        // une session tronquée ou abîmée, une chaîne sans zéro final.
        std::string name;
        if (length > 0 && length <= 4096) {
            name.resize(static_cast<std::size_t>(length));
            if (s.readRaw(name.data(), length) != length) name.clear();
            name.resize(std::strlen(name.c_str()));
        }
        if (kind > 0 && kind <= static_cast<int32>(RefKind::Title) && !name.empty()) {
            refKind_ = static_cast<RefKind>(kind);
            refName_ = name;
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
    return EditController::notify(message);
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
