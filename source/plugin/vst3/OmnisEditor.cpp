// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisEditor.cpp
// =============================================================================
#include "OmnisEditor.h"

#include "OmnisController.h"

#include "params/Parameters.h"

#include "public.sdk/source/vst/utility/stringconvert.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cviewcontainer.h"

#include <algorithm>
#include <cmath>

using namespace VSTGUI;
using namespace Steinberg;
using omnis::params::ParamId;

namespace omnis::vst3 {

namespace {

// Palette de la console (sombre, ambre).
const CColor kBack(18, 18, 20);
const CColor kPanel(28, 28, 32);
const CColor kGroove(12, 12, 14);
const CColor kText(225, 222, 214);
const CColor kDim(140, 138, 132);
const CColor kAmber(236, 168, 52);
const CColor kGreen(110, 196, 120);
const CColor kRed(214, 92, 80);

// Étiquettes des boutons (tags hors des paramètres).
constexpr int32_t kTagYes = 100000, kTagNo = 100001, kTagPrev = 100002, kTagNext = 100003, kTagUndo = 100004;

std::uint32_t id(ParamId p) { return static_cast<std::uint32_t>(p); }

CTextLabel* label(CViewContainer* parent, const CRect& r, const char* text, const CColor& color, CCoord size = 11, bool bold = false,
                  CHoriTxtAlign align = kLeftText) {
    auto* l = new CTextLabel(r, text);
    l->setTransparency(true);
    l->setFontColor(color);
    l->setFont(makeOwned<CFontDesc>("Arial", size, bold ? kBoldFace : kNormalFace));
    l->setHoriAlign(align);
    parent->addView(l);
    return l;
}

CViewContainer* panel(CViewContainer* parent, const CRect& r, const char* title) {
    auto* p = new CViewContainer(r);
    p->setBackgroundColor(kPanel);
    parent->addView(p);
    label(p, CRect(12, 6, r.getWidth() - 12, 24), title, kAmber, 12, true);
    return p;
}

CTextButton* button(CViewContainer* parent, const CRect& r, IControlListener* listener, int32_t tag, const char* title, const CColor& color) {
    auto* b = new CTextButton(r, listener, tag, title);
    b->setTextColor(color);
    b->setTextColorHighlighted(kBack);
    b->setFrameColor(color);
    b->setFrameColorHighlighted(color);
    b->setRoundRadius(4);
    b->setGradient(CGradient::create(0.0, 1.0, kGroove, kGroove));
    b->setGradientHighlighted(CGradient::create(0.0, 1.0, color, color));
    b->setFont(makeOwned<CFontDesc>("Arial", 12, kBoldFace));
    parent->addView(b);
    return b;
}

} // namespace

OmnisEditor::OmnisEditor(OmnisController* controller) : VSTGUIEditor(controller) {
    ViewRect r(0, 0, kWidth, kHeight);
    setRect(r);
}

OmnisController* OmnisEditor::omnis() const { return static_cast<OmnisController*>(getController()); }

bool PLUGIN_API OmnisEditor::open(void* parent, const PlatformType& platformType) {
    if (frame) return false;
    frame = new CFrame(CRect(0, 0, kWidth, kHeight), this);
    frame->setBackgroundColor(kBack);
    bound_.clear();
    meters_.clear();

    const auto menu = [&](CViewContainer* p, const CRect& r, ParamId param) {
        const auto* spec = params::findParameter(param);
        auto* m = new COptionMenu(r, this, static_cast<int32_t>(param));
        m->setFontColor(kText);
        m->setBackColor(kGroove);
        m->setFrameColor(kDim);
        m->setFont(makeOwned<CFontDesc>("Arial", 12, kNormalFace));
        for (const auto& c : spec->choices) m->addEntry(UTF8String(std::string(c)));
        p->addView(m);
        bound_.push_back({m, id(param), spec->stepCount, nullptr});
    };
    const auto knob = [&](CViewContainer* p, CCoord x, CCoord y, ParamId param, const char* name) {
        auto* k = new CKnob(CRect(x + 12, y, x + 60, y + 48), this, static_cast<int32_t>(param), nullptr, nullptr, CPoint(0, 0),
                            CKnob::kCoronaDrawing | CKnob::kCoronaOutline | CKnob::kHandleCircleDrawing | CKnob::kCoronaLineCapButt);
        k->setCoronaColor(kAmber);
        k->setColorShadowHandle(kGroove);
        k->setColorHandle(kText);
        k->setCoronaInset(2);
        p->addView(k);
        label(p, CRect(x, y + 50, x + 72, y + 64), name, kDim, 10, false, kCenterText);
        auto* v = label(p, CRect(x, y + 64, x + 72, y + 78), "", kText, 11, false, kCenterText);
        bound_.push_back({k, id(param), 0, v});
    };
    const auto meter = [&](CViewContainer* p, CCoord x, CCoord y, ParamId param, const char* name) {
        label(p, CRect(x, y, x + 120, y + 14), name, kDim, 10);
        meters_.push_back({label(p, CRect(x, y + 14, x + 130, y + 34), "—", kText, 16, true), id(param)});
    };

    // ---- En-tête -------------------------------------------------------------
    label(frame, CRect(20, 12, 200, 40), "OMNIS", kAmber, 24, true);
    label(frame, CRect(130, 22, 300, 38), "Abrasion Labs", kDim, 11);
    label(frame, CRect(330, 18, 380, 36), "Rôle", kDim, 11);
    menu(frame, CRect(372, 14, 472, 38), ParamId::LinkRole);
    label(frame, CRect(490, 18, 550, 36), "Groupe", kDim, 11);
    menu(frame, CRect(548, 14, 600, 38), ParamId::LinkGroup);
    {
        auto* b = new CCheckBox(CRect(790, 16, 880, 36), this, static_cast<int32_t>(ParamId::Bypass), "BYPASS");
        b->setFontColor(kText);
        b->setBoxFrameColor(kDim);
        b->setBoxFillColor(kGroove);
        b->setCheckMarkColor(kRed);
        b->setFont(makeOwned<CFontDesc>("Arial", 11, kBoldFace));
        frame->addView(b);
        bound_.push_back({b, id(ParamId::Bypass), 0, nullptr});
    }

    // ---- STEM ----------------------------------------------------------------
    auto* stem = panel(frame, CRect(20, 52, 440, 290), "STEM — la piste");
    label(stem, CRect(12, 34, 70, 52), "Mode", kDim, 11);
    menu(stem, CRect(64, 30, 164, 54), ParamId::StemMode);
    label(stem, CRect(180, 34, 250, 52), "Élément", kDim, 11);
    menu(stem, CRect(244, 30, 404, 54), ParamId::LinkStemKind);
    label(stem, CRect(12, 64, 130, 80), "Reconnu à l'écoute :", kDim, 11);
    meters_.push_back({label(stem, CRect(130, 60, 404, 82), "—", kGreen, 15, true), id(ParamId::MeterStemElement)});
    label(stem, CRect(12, 86, 404, 102), "Doses du mode Manuel (Auto : OMNIS règle seul ; Trim : les deux)", kDim, 10);
    knob(stem, 0, 108, ParamId::StemHpf, "PASSE-HAUT");
    knob(stem, 68, 108, ParamId::StemComp, "COMPRESSION");
    knob(stem, 136, 108, ParamId::StemSat, "SATURATION");
    knob(stem, 204, 108, ParamId::StemClean, "EQ DYNAMIQUE");
    knob(stem, 272, 108, ParamId::StemWidth, "LARGEUR");
    knob(stem, 340, 108, ParamId::StemTrim, "TRIM");

    // ---- MASTER --------------------------------------------------------------
    auto* master = panel(frame, CRect(460, 52, 880, 290), "MASTER — la prod");
    meter(master, 12, 30, ParamId::MeterShortTerm, "LUFS court terme");
    meter(master, 150, 30, ParamId::MeterIntegrated, "LUFS intégré");
    meter(master, 288, 30, ParamId::MeterTruePeak, "True peak");
    meter(master, 12, 70, ParamId::MeterLoudnessRange, "LRA");
    meter(master, 150, 70, ParamId::MeterCorrelation, "Corrélation");
    meter(master, 288, 70, ParamId::MeterLinkStems, "Stems liés");
    knob(master, 0, 120, ParamId::InputGain, "ENTRÉE");
    knob(master, 68, 120, ParamId::InflHighEffect, "INFLATOR");
    knob(master, 136, 120, ParamId::ClipDrive, "CLIP");
    knob(master, 204, 120, ParamId::LimGain, "LIMITEUR");
    knob(master, 272, 120, ParamId::LimCeiling, "PLAFOND");
    knob(master, 340, 120, ParamId::OutputGain, "SORTIE");

    // ---- Chef d'orchestre ----------------------------------------------------
    auto* conductor = panel(frame, CRect(20, 304, 880, 540), "CHEF D'ORCHESTRE — propositions du master");
    proposalCount_ = label(conductor, CRect(560, 6, 848, 24), "", kDim, 11, false, kRightText);
    proposalText_ = new CMultiLineTextLabel(CRect(12, 32, 848, 112));
    proposalText_->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
    proposalText_->setTransparency(true);
    proposalText_->setFontColor(kText);
    proposalText_->setFont(makeOwned<CFontDesc>("Arial", 14, kNormalFace));
    proposalText_->setHoriAlign(kLeftText);
    conductor->addView(proposalText_);
    button(conductor, CRect(12, 120, 112, 150), this, kTagYes, "OUI", kGreen);
    button(conductor, CRect(122, 120, 222, 150), this, kTagNo, "NON", kRed);
    button(conductor, CRect(740, 120, 790, 150), this, kTagPrev, "<", kDim);
    button(conductor, CRect(798, 120, 848, 150), this, kTagNext, ">", kDim);
    label(conductor, CRect(12, 160, 300, 176), "Retouches en place :", kDim, 11);
    appliedText_ = new CMultiLineTextLabel(CRect(12, 178, 730, 230));
    appliedText_->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
    appliedText_->setTransparency(true);
    appliedText_->setFontColor(kText);
    appliedText_->setFont(makeOwned<CFontDesc>("Arial", 11, kNormalFace));
    appliedText_->setHoriAlign(kLeftText);
    conductor->addView(appliedText_);
    button(conductor, CRect(740, 190, 848, 220), this, kTagUndo, "ANNULER", kAmber);

    frame->open(parent, platformType);
    timer_ = new CVSTGUITimer([this](CVSTGUITimer*) { refresh(); }, 100, true);
    omnis()->requestProposals();
    refresh();
    return true;
}

void PLUGIN_API OmnisEditor::close() {
    if (timer_) {
        timer_->forget();
        timer_ = nullptr;
    }
    bound_.clear();
    meters_.clear();
    proposalText_ = nullptr;
    proposalCount_ = nullptr;
    appliedText_ = nullptr;
    if (frame) {
        frame->forget();
        frame = nullptr;
    }
}

void OmnisEditor::controlBeginEdit(CControl* control) {
    for (const auto& b : bound_)
        if (b.control == control) omnis()->beginEdit(b.param);
}

void OmnisEditor::controlEndEdit(CControl* control) {
    for (const auto& b : bound_)
        if (b.control == control) omnis()->endEdit(b.param);
}

void OmnisEditor::valueChanged(CControl* control) {
    const int32_t tag = control->getTag();
    OmnisController* c = omnis();
    if (tag >= kTagYes) {
        if (control->getValue() < 0.5f) return; // relâchement du bouton
        const auto& pending = c->pendingProposals();
        if ((tag == kTagYes || tag == kTagNo) && !pending.empty()) {
            const auto& p = pending[static_cast<std::size_t>(std::clamp(proposalIndex_, 0, static_cast<int>(pending.size()) - 1))];
            c->answerProposal(p.key, tag == kTagYes ? 0 : 1);
        } else if (tag == kTagPrev) {
            --proposalIndex_;
        } else if (tag == kTagNext) {
            ++proposalIndex_;
        } else if (tag == kTagUndo && !c->appliedCorrections().empty()) {
            c->answerProposal(c->appliedCorrections().back().key, 2);
        }
        c->requestProposals();
        showProposal();
        return;
    }
    for (const auto& b : bound_) {
        if (b.control != control) continue;
        double normalized = control->getValue();
        if (b.steps > 0) normalized = std::clamp(static_cast<double>(control->getValue()), 0.0, static_cast<double>(b.steps)) / b.steps;
        const bool menuOrSwitch = b.steps > 0 || b.param == id(ParamId::Bypass);
        if (menuOrSwitch) c->beginEdit(b.param);
        c->setParamNormalized(b.param, normalized);
        c->performEdit(b.param, normalized);
        if (menuOrSwitch) c->endEdit(b.param);
    }
}

void OmnisEditor::refresh() {
    if (!frame) return;
    OmnisController* c = omnis();
    for (const auto& b : bound_) {
        const double n = c->getParamNormalized(b.param);
        const float v = b.steps > 0 ? static_cast<float>(std::lround(n * b.steps)) : static_cast<float>(n);
        if (b.control->getValue() != v && !b.control->isEditing()) {
            b.control->setValue(v);
            b.control->invalid();
        }
        if (b.value) {
            Vst::String128 text{};
            c->getParamStringByValue(b.param, n, text);
            const UTF8String s(Vst::StringConvert::convert(text));
            if (b.value->getText() != s) b.value->setText(s);
        }
    }
    for (const auto& m : meters_) {
        Vst::String128 text{};
        c->getParamStringByValue(m.param, c->getParamNormalized(m.param), text);
        std::string s = Vst::StringConvert::convert(text);
        if (const auto* spec = params::findParameter(m.param); spec && !spec->unit.empty() && spec->stepCount == 0) s += " " + std::string(spec->unit);
        if (m.label->getText() != s.c_str()) m.label->setText(UTF8String(s));
    }
    if (++ticks_ % 5 == 0) c->requestProposals();
    showProposal();
}

void OmnisEditor::showProposal() {
    if (!proposalText_) return;
    OmnisController* c = omnis();
    const auto& pending = c->pendingProposals();
    const bool isMaster = std::lround(c->getParamNormalized(id(ParamId::LinkRole)) * 2.0) == 2;
    std::string text, count;
    if (!isMaster) {
        text = "Les propositions apparaissent sur l'instance réglée en Master (même groupe que les stems).";
    } else if (pending.empty()) {
        text = "Aucune proposition : aucun stem n'en gêne un autre pour l'instant. Lance la lecture, OMNIS écoute.";
    } else {
        proposalIndex_ = std::clamp(proposalIndex_, 0, static_cast<int>(pending.size()) - 1);
        text = pending[static_cast<std::size_t>(proposalIndex_)].text;
        count = "Proposition " + std::to_string(proposalIndex_ + 1) + " / " + std::to_string(pending.size());
    }
    if (proposalText_->getText() != text.c_str()) proposalText_->setText(UTF8String(text));
    if (proposalCount_ && proposalCount_->getText() != count.c_str()) proposalCount_->setText(UTF8String(count));
    std::string applied;
    for (const auto& a : c->appliedCorrections()) applied += "• " + a.description + "\n";
    if (applied.empty()) applied = "Aucune.";
    if (appliedText_ && appliedText_->getText() != applied.c_str()) appliedText_->setText(UTF8String(applied));
}

} // namespace omnis::vst3
