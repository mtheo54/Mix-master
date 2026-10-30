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
//  Chef d'orchestre : l'interface demande les propositions au processeur
//  (requestProposals), qui répond par message (notify) ; les réponses de la
//  personne partent par answerProposal.
//
//  Nom de la piste : l'hôte le donne par ChannelContext::IInfoListener (la
//  norme VST3, quel que soit le DAW qui la suit) ; le contrôleur le passe au
//  processeur par message, qui s'en sert pour reconnaître l'élément d'un stem.
// =============================================================================
#pragma once

#include "analysis/SpectralSignature.h"

#include "pluginterfaces/vst/ivstchannelcontextinfo.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
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
    /// « Link Role » : un stem n'a pas de latence, un master si ; l'hôte doit
    /// relire la latence et réactiver l'instance.
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) override;

    /// Nom (et couleur…) de la piste où le plug-in est inséré, à chaque changement.
    Steinberg::tresult PLUGIN_API setChannelContextInfos(Steinberg::Vst::IAttributeList* list) override;

    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    /// Messages du processeur : propositions du chef d'orchestre.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    struct ProposalLine {
        std::uint32_t key = 0;
        std::string text;        ///< proposition : la question ; retouche en place : sa description
        std::string description;
    };
    void requestProposals();
    /// 0 accepter, 1 refuser, 2 annuler (une retouche en place).
    void answerProposal(std::uint32_t key, int answer);
    [[nodiscard]] const std::vector<ProposalLine>& pendingProposals() const noexcept { return pending_; }
    [[nodiscard]] const std::vector<ProposalLine>& appliedCorrections() const noexcept { return applied_; }
    /// Stem en bus de drums : les éléments reconnus et ce qui en est fait.
    [[nodiscard]] const std::vector<std::string>& drumElements() const noexcept { return drumLines_; }

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
    /// AUTO : écoute du passage le plus fort, puis réglage de la chaîne.
    void startAuto();
    /// Tutoriel : relancer depuis le bouton « ? ».
    void restartTutorial();
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
    [[nodiscard]] ReferenceTree referenceTree() const;
    /// La référence choisie, de quoi la montrer (vide : aucune).
    struct ReferenceCard {
        std::string name, credit, photo, cover, detail; ///< detail : « 111 BPM · Mi · -3,7 LUFS »
        std::string coverCredit, link;                  ///< d'où vient la pochette ; la musique en ligne
        analysis::SpectralSignature signature;           ///< moyenne des titres (pochette dessinée)
        int titles = 0;
    };
    [[nodiscard]] ReferenceCard referenceCard() const;
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

    OBJ_METHODS(OmnisController, EditController)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::ChannelContext::IInfoListener)
    END_DEFINE_INTERFACES(EditController)
    REFCOUNT_METHODS(EditController)

private:
    std::vector<ProposalLine> pending_, applied_;
    std::vector<std::string> drumLines_;
    bool loadingState_ = false; ///< dans setComponentState
    std::shared_ptr<UiFeed> feed_;
    // Barre IA / AUTO.
    std::string pendingAsk_, autoStyle_ = "rap fr", lastMessage_;
    RefKind refKind_ = RefKind::None;
    std::string refName_;
    bool pendingRef_ = false;
    std::string runReference();
    bool autoRunning_ = false;
    double loudTarget_ = 0.0;
    bool pursuing_ = false;
    int pursuitTicks_ = 0, pursuitStable_ = 0, pursuitSteps_ = 0;
    std::string runRequest(const std::string& text);
    void applyPlan(const std::vector<std::pair<std::uint32_t, double>>& settings);
    [[nodiscard]] double plainOf(std::uint32_t id);
    std::array<std::vector<std::pair<std::uint32_t, double>>, 2> ab_;
    int abSlot_ = 0;
    [[nodiscard]] std::vector<std::pair<std::uint32_t, double>> snapshot();
    void apply(const std::vector<std::pair<std::uint32_t, double>>& values);
};

} // namespace omnis::vst3
