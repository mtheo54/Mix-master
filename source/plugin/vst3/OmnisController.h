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
//  createView() : l'interface OMNIS (OmnisEditor, VSTGUI) quand le plug-in est
//  compilé avec (OMNIS_HAS_GUI), sinon l'éditeur générique de l'hôte.
//
//  Nom de la piste : l'hôte le donne par ChannelContext::IInfoListener ; il est
//  passé au processeur (rôle Bus : publié aux autres instances avec ce que le
//  bus contient). composition() lit la région partagée (link::Observer) : chaque
//  bus du projet et ce qu'OMNIS y entend (page COMPOSITION).
// =============================================================================
#pragma once

#include "analysis/SpectralSignature.h"
#include "link/LinkBus.h"
#include "params/Parameters.h"
#include "ai/UsageGraph.h"
#include "stems/Composition.h"

#include "pluginterfaces/vst/ivstchannelcontextinfo.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace omnis::vst3 {

class UiFeed;

class OmnisController : public Steinberg::Vst::EditController, public Steinberg::Vst::ChannelContext::IInfoListener {
public:
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new OmnisController());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    /// Mode de latence : l'hôte doit relire la latence et réactiver l'instance.
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) override;
    /// Nom (et couleur) de la piste où l'instance est posée.
    Steinberg::tresult PLUGIN_API setChannelContextInfos(Steinberg::Vst::IAttributeList* list) override;
    [[nodiscard]] const std::string& trackName() const noexcept { return trackName_; }
    /// Rôle choisi (Master / Bus).
    [[nodiscard]] params::InstanceRole instanceRole();
    /// Les bus OMNIS du projet et ce qu'ils contiennent (relu au plus une fois par seconde). Vide sans bus.
    [[nodiscard]] const stems::Composition& composition();
    /// Ce que la barre IA dit (« BUS « DRUMS » (DRUMS) : Kick (24), … »).
    [[nodiscard]] std::string compositionText();
    /// Tous les bus oublient ce qu'ils ont entendu et réécoutent le morceau (après un changement du mix).
    std::string relistenBuses();

    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    /// Messages du processeur : le flux de l'interface (UiFeed).
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    /// Interface : demande le flux du processeur (mesures des modules, sortie).
    void requestFeed();
    [[nodiscard]] std::shared_ptr<UiFeed> feed() const { return feed_; }

    /// Réglage d'un paramètre par l'interface (beginEdit / performEdit / endEdit).
    void editParameter(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue normalized);
    void beginGesture(Steinberg::Vst::ParamID id) { beginEdit(id); }
    void performGesture(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue normalized);
    void endGesture(Steinberg::Vst::ParamID id) { endEdit(id); }

    /// Barre IA : la demande en texte libre ; renvoie ce qu'OMNIS a compris et fait.
    std::string ask(const std::string& request);
    /// Avant d'exporter : ce qui fausserait le fichier (vide = prêt). Le premier
    /// élément est le plus grave.
    [[nodiscard]] std::vector<std::string> exportProblems();
    /// Diagnostic du mix (ai/MixDiagnosis) sur ce qu'OMNIS a entendu de la prod.
    std::string mixDiagnosis();
    /// AUTO : écoute du morceau entier (jusqu'à l'arrêt de la lecture), réglage de la
    /// chaîne, puis essais de répartition clip / limiteur sur le passage le plus fort
    /// (en arrière-plan). Pendant l'écoute, un second appel la termine.
    /// Renvoie ce qu'AUTO fait maintenant (pour la barre IA).
    std::string startAuto();
    /// AUTO sur un fichier (glissé sur la barre IA) : tout le morceau analysé, puis le
    /// même réglage et les mêmes essais qu'à l'écoute. En arrière-plan : la réponse
    /// arrive par pollAutomation().
    std::string autoFromFile(const std::string& utf8Path);
    /// Ce que fait AUTO, pour la barre IA (vide : rien en cours).
    [[nodiscard]] std::string autoStatus() const;
    /// Tutoriel : relancer (« tuto », « aide » dans la barre IA). L'éditeur le prend
    /// à sa minuterie avec takeTutorialRequest().
    void restartTutorial() { tutorialRequested_ = true; }
    [[nodiscard]] bool takeTutorialRequest() { return std::exchange(tutorialRequested_, false); }
    /// Interface, à chaque minuterie : AUTO qui termine, demande en attente
    /// d'écoute, volume mené vers la cible. Renvoie un message nouveau (ou vide).
    std::string pollAutomation();
    /// AUTO en cours : 0 … 1 (négatif : pas d'AUTO).
    [[nodiscard]] double autoProgress() const;

    /// Menu RÉFÉRENCE : ce que la base propose (seulement ce qui est mesuré),
    /// base fournie + references_utilisateur.txt : style -> artiste -> ses titres,
    /// et les artistes de A à Z. Les images sont des chemins de fichiers présents
    /// (vides sinon : la console dessine une pochette tirée de la fiche).
    struct ReferenceTitle {
        std::string name;  ///< « Artiste - Titre » (ce que chooseReference attend)
        std::string title; ///< le titre seul (menu)
        std::string cover; ///< pochette : chemin du fichier
        analysis::SpectralSignature signature;
    };
    struct ReferenceArtist {
        std::string name;
        std::string photo, credit; ///< photo libre et son crédit (jamais l'une sans l'autre)
        std::vector<ReferenceTitle> titles;
    };
    struct ReferenceStyle {
        std::string name;
        std::vector<ReferenceArtist> artists;
        [[nodiscard]] std::size_t titleCount() const;
    };
    struct ReferenceTree {
        std::vector<ReferenceStyle> styles;
        std::vector<ReferenceArtist> artists; ///< A – Z, tous styles
    };
    /// `family` : une famille de data/genres_classement.txt (« Rap Français & New Wave ») ;
    /// vide : tous les styles.
    /// `style` : un sous-genre précis de cette famille (« drill fr ») ; vide : toute la famille.
    [[nodiscard]] ReferenceTree referenceTree(const std::string& family = {}, const std::string& style = {}) const;
    /// Le filtre du menu RÉF : les familles qui ont des titres mesurés, et dans chacune
    /// ses sous-genres (avec leur nombre de titres), les plus fournis d'abord.
    struct FamilyFilter {
        std::string name;
        int titles = 0;
        std::vector<std::pair<std::string, int>> styles;
    };
    [[nodiscard]] std::vector<FamilyFilter> referenceFilters() const;
    /// La fenêtre RÉF : chaque famille et TOUS ses sous-genres (data/styles_artistes.txt et les
    /// genres de la base), avec le profil mesuré quand il existe ; sans profil, la famille sert de
    /// repère (`target`). Familles dans l'ordre de genres_classement, styles mesurés d'abord.
    struct GenreCard {
        std::string family;               ///< « Rap Français & New Wave »
        std::string style;                ///< « rage fr » ; vide : toute la famille
        std::string target;               ///< ce que chooseReference(Style, …) reçoit
        int titles = 0;                   ///< titres derrière son profil (0 : pas encore mesuré)
        std::vector<std::string> artists; ///< artistes connus de ce style
        analysis::SpectralSignature signature; ///< son profil (ou celui de la famille)
        [[nodiscard]] bool measured() const noexcept { return titles > 0; }
    };
    [[nodiscard]] std::vector<GenreCard> genreCatalog() const;
    [[nodiscard]] std::vector<std::string> referenceFamilies() const;
    /// La référence choisie, de quoi la montrer (vide : aucune).
    struct ReferenceCard {
        std::string name, credit, photo, cover, detail; ///< detail : « 111 BPM · Mi · -3,7 LUFS »
        std::string coverCredit, link;                  ///< d'où vient la pochette ; la musique en ligne
        analysis::SpectralSignature signature;           ///< moyenne des titres (pochette dessinée)
        int titles = 0;
    };
    [[nodiscard]] ReferenceCard referenceCard() const;

    /// Mode promo (« /fbi » dans la barre IA, ou Maj + Alt + 5 clics sur le logo) :
    /// des monogrammes à la place des avatars, pour filmer la console sans montrer
    /// d'image. Gardé d'une session à l'autre (dossier de l'utilisateur).
    [[nodiscard]] bool promoMode() const;
    void setPromoMode(bool on);

    /// Ajouter un fichier de référence (menu RÉF) : mesuré, reconnu (étiquettes,
    /// nom « Artiste - Titre » ou « Titre - Artiste », MusicBrainz si permis) et
    /// rangé dans la base de la personne, sur son poste. En arrière-plan : la
    /// réponse arrive par pollAutomation() ; ce qui est renvoyé tout de suite dit
    /// que l'écoute a commencé.
    std::string importReferenceFile(const std::string& utf8Path);
    [[nodiscard]] bool importRunning() const;
    /// La base a changé depuis le dernier appel (menu RÉF à refaire).
    [[nodiscard]] bool takeLibraryChanged();
    /// MusicBrainz (une seule requête, quand un fichier est ajouté ; rien d'autre
    /// ne va sur internet) : permis par défaut, « non » gardé dans musicbrainz.txt.
    [[nodiscard]] bool onlineRecognition() const;
    void setOnlineRecognition(bool on);
    ~OmnisController() override;
    enum class RefKind : int { None = 0, Style, Artist, Title };
    /// Choisit la référence : appliquée tout de suite (ou dès qu'OMNIS a assez
    /// écouté) ; AUTO la vise ensuite. None : AUTO revient au style des mots.
    std::string chooseReference(RefKind kind, const std::string& name);
    [[nodiscard]] RefKind referenceKind() const noexcept { return refKind_; }
    [[nodiscard]] const std::string& referenceName() const noexcept { return refName_; }

    /// Mémoires A / B : deux réglages complets, gardés avec la session.
    [[nodiscard]] int abSlot() const noexcept { return abSlot_; }
    void selectAB(int slot);
    void copyABToOther();

    /// Contrôleur seul (A / B) : gardé par l'hôte avec la session.
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;


private:
    struct ImportJob {
        std::atomic<bool> done{false};
        std::atomic<bool> cancel{false}; ///< fermeture : plus de requête MusicBrainz
        bool ok = false;
        std::string message;
    };
    std::shared_ptr<ImportJob> import_;
    std::thread importThread_;
    // AUTO précis : les essais sur le passage le plus fort, hors du thread de l'interface.
    struct AutoJob {
        std::atomic<bool> done{false};
        std::atomic<bool> cancel{false};
        std::atomic<int> step{0}, total{3};
        bool ok = false;
        double clipDrive = 0.0, limGain = 0.0, distortionDb = 0.0;
        double reachedLufs = 0.0, targetLufs = 0.0; ///< sonie du meilleur essai, et la visée
        int trials = 0;
        std::string name; ///< la répartition retenue (« clip 70 / 30 »)
    };
    std::shared_ptr<AutoJob> autoJob_;
    std::thread autoThread_;
    void startAutoSearch(std::vector<float> excerpt, double sampleRate, double targetLufs);
    /// Le morceau entendu (ou lu) -> la chaîne vers le style, puis les essais.
    std::string applyAuto(const analysis::SpectralSignature& loud, std::vector<float> excerpt, double sampleRate);
    struct FileJob {
        std::atomic<bool> done{false};
        bool ok = false;
        std::string name, error;
        analysis::SpectralSignature signature;
        std::vector<float> excerpt; ///< passage le plus fort, entrelacé L R
        double sampleRate = 0.0;
    };
    std::shared_ptr<FileJob> fileJob_;
    std::thread fileThread_;
    std::string finishAutoSearch();
    bool libraryChanged_ = false;
    bool tutorialRequested_ = false;
    mutable int online_ = -1;
    bool loadingState_ = false; ///< dans setComponentState
    std::shared_ptr<UiFeed> feed_;
    // Barre IA / AUTO.
    std::string pendingAsk_, autoStyle_ = "rap fr", lastMessage_;
    mutable int promo_ = -1; ///< -1 : pas encore lu
    RefKind refKind_ = RefKind::None;
    std::string refName_;
    bool pendingRef_ = false;
    std::string runReference();
    bool autoRunning_ = false;
    double loudTarget_ = 0.0;
    bool pursuing_ = false;
    bool levelChanged_ = false; ///< NIVEAU changé : le volume y est mené une fois
    double heardPeakDb_ = -200.0; ///< crête de la prod au passage écouté par AUTO (garde-fou du volume)
    // Reconnaissance du style : AUTO la fait quand aucun style n'est nommé ; un style nommé
    // apprend la prod entendue (mémoire personnelle, styles_appris.txt sur le poste).
    bool styleNamed_ = false;
    std::string recognizeHeard(const analysis::SpectralSignature& heard, std::string* style);
    std::string learnStyle(const std::string& genre);
    // Habitudes (étape 5) : AUTO applique ce que le réseau a appris ; la retouche qui suit est notée.
    std::string recognizedGroup_;
    bool autoPending_ = false;            ///< un AUTO dont on n'a pas encore noté la suite
    std::vector<double> autoFeatures_;
    std::array<double, 6> autoProposal_{};
    [[nodiscard]] std::array<double, 6> habitValues();
    std::string applyHabits(const analysis::SpectralSignature& heard, std::vector<std::pair<std::uint32_t, double>>& settings,
                            std::optional<double>& targetLufs);
    /// Une séance d'apprentissage commence : AUTO, ou un preset (référence / style du menu RÉF).
    void noteAuto(ai::SessionStart start = ai::SessionStart::Auto, const std::string& context = {}, std::vector<std::string> styles = {});
    /// Elle finit (proposition suivante, export, fermeture) : ce que la personne a gardé, jugé
    /// (ai/UsageGraph), noté dans le graphe ; seuls les gestes utiles nourrissent le réseau.
    void noteRetouch(bool exported = false);
    [[nodiscard]] ai::UsageState usageState();
    ai::UsageSession session_;
    ai::HabitVector lastHabits_{};
    bool omnisEditing_ = false; ///< OMNIS règle lui-même (volume mené, essais) : pas une retouche de la personne
    struct OmnisEdit {
        explicit OmnisEdit(OmnisController& c) : c_(c) { c_.omnisEditing_ = true; }
        ~OmnisEdit() { c_.omnisEditing_ = false; }
        OmnisController& c_;
    };
    int pursuitTicks_ = 0, pursuitStable_ = 0, pursuitSteps_ = 0;
    static constexpr int kPursuitSettleTicks = 130; ///< ~4 s de minuterie (30 Hz) : la mesure suit le réglage
    std::string runRequest(const std::string& text);
    void applyPlan(const std::vector<std::pair<std::uint32_t, double>>& settings);
    /// Garde l'état de la console avant une action de l'IA (« annule » y revient).
    void rememberForUndo(const std::string& label);
    std::string undoLast();
    struct UndoStep {
        std::string label;
        std::vector<std::pair<std::uint32_t, double>> values;
    };
    std::vector<UndoStep> undo_; ///< 20 étapes au plus
    [[nodiscard]] double plainOf(std::uint32_t id);
    std::array<std::vector<std::pair<std::uint32_t, double>>, 2> ab_;
    int abSlot_ = 0;
    [[nodiscard]] std::vector<std::pair<std::uint32_t, double>> snapshot();
    void apply(const std::vector<std::pair<std::uint32_t, double>>& values);

    std::string trackName_;
    link::Observer observer_;
    stems::Composition composition_;
    std::uint64_t compositionAtMs_ = 0;

public:
    OBJ_METHODS(OmnisController, EditController)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::ChannelContext::IInfoListener)
    END_DEFINE_INTERFACES(EditController)
    REFCOUNT_METHODS(EditController)
};

} // namespace omnis::vst3
