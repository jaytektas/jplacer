// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPIssuesPanel.h"

#include "JPUiParts.h"

#include "setup/JPFormBuilder.h"

#include <j/core/FrameTimer.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>

inline namespace jf {

namespace {

using S = JPSolutions;
constexpr const char* kIssuesWiki = "https://github.com/openpnp/openpnp/wiki/Issues-and-Solutions";

} // namespace

// The issues found: Subject, Severity, Issue, Solution, State.
class JPIssuesPanel::Model : public JPTableModel {
public:
    explicit Model(JPSolutions& s) : m_s(s) {}
    int    columnCount() const override { return 5; }
    Column column(int c) const override {
        static const char* const names[] = { "Subject", "Severity", "Issue", "Solution", "State" };
        Column col;
        col.name = names[c];
        return col;
    }
    int rowCount() const override { return int(m_s.issues().size()); }
    std::string text(int row, int c) const override {
        const S::Issue& i = *m_s.issues()[size_t(row)];
        switch (c) {
            case 0: return i.subject;
            case 1: return S::name(i.severity);
            case 2: return i.issue;
            case 3: return i.solution;
            default: return S::name(i.state);
        }
    }
    std::string rowKey(int row) const override { return m_s.issues()[size_t(row)]->fingerprint(); }
    std::string cellIcon(int, int c) const override { return c == 0 ? "solutions" : std::string(); }
    // OpenPnP's colours: severity behind the Severity, state behind the State.
    const uint8_t* cellTint(int row, int c) const override {
        const S::Issue& i = *m_s.issues()[size_t(row)];
        if (c == 1)
            switch (i.severity) {
                case S::Severity::Suggestion:  return Colors::Success;
                case S::Severity::Warning:     return Colors::Warning;
                case S::Severity::Error:       return Colors::Danger;
                case S::Severity::Fundamental: return Colors::Accent;
                default:                       return nullptr;
            }
        if (c == 4 && i.state == S::State::Solved) return Colors::Success;
        return nullptr;
    }
    bool cellDimmed(int row, int) const override { return m_s.issues()[size_t(row)]->state == S::State::Dismissed; }
    std::string cellTooltip(int row, int c) const override { return c >= 2 && c <= 3 ? text(row, c) : std::string(); }

private:
    JPSolutions& m_s;
};

JPIssuesPanel::JPIssuesPanel(JSceneGraph& graph, JPSolutions& solutions, double split)
    : JContainer(graph), m_solutions(solutions), m_model(std::make_unique<Model>(solutions)) {
    JPUiParts::asPanel(*this);
    const JStyle& st = JStyle::current();
    auto fixedLabel = [&](JContainer& row, const std::string& text) {
        JLabel* l = row.add(std::make_unique<JLabel>(graph, text));
        l->setFixedSize(JTextHelper::measureWidth(text) + st.spacing, st.controlHeight);
        return l;
    };

    // The toolbar.
    auto bar = JPUiParts::row(graph);
    JButton* find = bar->add(JPUiParts::button(graph, "Find Issues & Solutions"));
    find->setTooltip("Find Issues and Solutions for your machine.");
    find->onClicked.connect([this] { findIssuesAndSolutions(); });
    JLabel* milestone = fixedLabel(*bar, "Milestone");
    milestone->setTooltip("The target milestone for the machine configuration.\nThe milestone filters and sometimes "
                          "influences proposed solutions\nto ensure that basic machine operation is achieved, before more "
                          "advanced,\nmore complex and more difficult solutions are targeted.");
    // Any milestone gone to at once: a machine set up long ago, crashed, goes back to Calibration; an imported one
    // set up in OpenPnP goes straight to where it was.
    std::vector<std::string> milestones;
    for (int m = 0; m <= int(S::Milestone::Advanced); ++m) milestones.push_back(S::name(S::Milestone(m)));
    m_milestone = bar->add(std::make_unique<JComboBox>(graph, milestones, 0.f));
    m_milestone->setTooltip(milestone->tooltip());
    float longest = 0;
    for (const std::string& name : milestones) longest = std::max(longest, JTextHelper::measureWidth(name));
    m_milestone->setMinimumSize(longest + st.controlHeight + 2 * st.spacing, st.controlHeight);   // as long as its longest name
    m_milestone->onIndexChanged.connect([this](int i) {
        if (i < 0 || S::Milestone(i) == m_solutions.targetMilestone()) return;
        m_solutions.setTargetMilestone(S::Milestone(i));
        if (m_solutions.onChanged) m_solutions.onChanged();
        findIssuesAndSolutions();
    });
    JLabel* solved = fixedLabel(*bar, "Include Solved?");
    solved->setTooltip("Include already solved solutions, if they can be revisited.\nSome solutions can only be accepted once, "
                       "these will not reappear.");
    m_showSolved = bar->add(std::make_unique<JCheckBox>(graph, "", st.checkHeight));
    m_showSolved->setChecked(m_solutions.showSolved());
    m_showSolved->onStateChanged.connect([this](bool on) {
        m_solutions.setShowSolved(on);
        if (m_solutions.onChanged) m_solutions.onChanged();
        findIssuesAndSolutions();
    });
    JLabel* dismissed = fixedLabel(*bar, "Include Dismissed?");
    dismissed->setTooltip("Include already dismissed solutions.");
    m_showDismissed = bar->add(std::make_unique<JCheckBox>(graph, "", st.checkHeight));
    m_showDismissed->setChecked(m_solutions.showDismissed());
    m_showDismissed->onStateChanged.connect([this](bool on) {
        m_solutions.setShowDismissed(on);
        if (m_solutions.onChanged) m_solutions.onChanged();
        findIssuesAndSolutions();
    });
    // What is left of the bar, after the checkboxes: the info button kept to the right.
    bar->add(std::make_unique<JLabel>(graph, ""))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    JPIconButton* aboutMilestone = bar->add(std::make_unique<JPIconButton>(
        graph, "Milestone Info", "info", "Open the Wiki page about Issues & Solutions and Milestones."));
    aboutMilestone->setLeads(JPIconButton::Leads::Elsewhere);
    aboutMilestone->onClicked.connect([this] {
        if (openUri)
            openUri(std::string(kIssuesWiki) + "#" + S::tag(m_solutions.targetMilestone()) + "-milestone");
    });
    bar->setFixedSize(0.f, st.controlHeight);
    add(std::move(bar));
    m_milestoneText = add(std::make_unique<JLabel>(graph, " - "));
    m_milestoneText->setFixedSize(0.f, st.labelHeight);
    m_milestoneText->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    // An issue's properties changed behind its controls: shown again, on the next frame.
    // An issue changed behind its controls (a value found, as Auto-Detect Next's; its work done or failed):
    // the table, the buttons and the indicator as it now is, and the page's values read again in place (not
    // made again: it stays scrolled where it is), on the next frame.
    m_solutions.onSolutionChanged = [this, alive = std::weak_ptr<bool>(m_alive)] {
        jPostToNextFrame([this, alive] {
            if (!alive.lock()) return;
            m_table->refresh();
            showButtons();
            updateIndicator();
            m_form->refresh();
        });
    };
    m_warn = add(std::make_unique<JLabel>(graph, ""));
    m_warn->setFixedSize(0.f, st.labelHeight);
    m_warn->setHSizePolicy(JSizePolicyMode::Expanding, 1);

    // The issues over the one chosen, a divider between to drag.
    m_tablePane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_issuePane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    for (JContainer* p : { m_tablePane.get(), m_issuePane.get() })
        p->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_table = m_tablePane->add(std::make_unique<JPTable>(graph));
    m_table->setModel(m_model.get());
    m_table->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_table->onSelectionChanged.connect([this] { selectionChanged(); });
    m_form = m_issuePane->add(std::make_unique<JPSetupForm>(graph));
    m_form->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_form->onAction = [this](const std::string& action) {
        const auto chosen = selections();
        if (chosen.size() != 1) return;
        for (const S::Property& p : chosen.front()->properties)
            if (p.kind == S::Property::Kind::Action && p.label + "|" + p.actionLabel == action && p.action) p.action();
    };
    m_form->onChanged = [this](const std::string& property) {
        if (property == "issue.choice") showIssue();
    };
    auto buttons = JPUiParts::row(graph);
    buttons->add(std::make_unique<JContainer>(graph, 0.f, 0.f))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_accept = buttons->add(JPUiParts::button(graph, "Accept"));
    m_accept->setTooltip("Accept the solutions and apply any changes.");
    m_accept->onClicked.connect([this] { setState(S::State::Solved); });
    m_dismiss = buttons->add(JPUiParts::button(graph, "Dismiss"));
    m_dismiss->setTooltip("Dismiss the solutions. If the solution has just applied any changes before (with no Find Solutions "
                          "between), undo them.");
    m_dismiss->onClicked.connect([this] { setState(S::State::Dismissed); });
    m_reopen = buttons->add(JPUiParts::button(graph, "Reopen"));
    m_reopen->setTooltip("Reopen the solution. If the solution has just applied any changes before (with no Find Solutions "
                         "between), undo them.");
    m_reopen->onClicked.connect([this] { setState(S::State::Open); });
    m_info = buttons->add(std::make_unique<JPIconButton>(
        graph, "Info", "info", "Open the Wiki page with instructions related to the issue and possible solutions."));
    m_info->setLeads(JPIconButton::Leads::Elsewhere);
    m_info->onClicked.connect([this] {
        const auto chosen = selections();
        for (const S::Issue* i : chosen)
            if (!i->uri.empty() && openUri) {
                openUri(i->uri);
                return;
            }
    });
    buttons->setFixedSize(0.f, st.controlHeight);
    m_issuePane->add(std::move(buttons));

    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_tablePane.get(), float(split));
    m_split->addPane(m_issuePane.get(), float(1 - split));

    showMilestone();
    selectionChanged();
}

JPIssuesPanel::~JPIssuesPanel() {
    *m_alive = false;
    m_solutions.onSolutionChanged = nullptr;
}

double JPIssuesPanel::split() const {
    const std::vector<float> f = m_split->fractions();
    return f.empty() ? 0.5 : f.front();
}

void JPIssuesPanel::findIssuesAndSolutions() {
    m_solutions.find();
    m_solutions.publish();
    m_warn->setText("");
    m_table->refresh();
    if (m_model->rowCount() > 0) m_table->selectRow(m_table->modelRowAt(0));
    showMilestone();
    selectionChanged();
    updateIndicator();
}

void JPIssuesPanel::updateIndicator() {
    if (!onIndicator) return;
    using S = JPSolutions;
    S::Severity most = S::Severity::None;
    for (const auto& i : m_solutions.issues())
        if (i->state == S::State::Open && int(i->severity) >= int(most)) most = i->severity;
    if (int(most) <= int(S::Severity::Information)) {
        onIndicator(std::nullopt);
        return;
    }
    // OpenPnP's severity colours, saturated as its indicator draws them (the weakest channel to 0, the strongest to 200).
    static const std::array<int, 3> kColors[] = { { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 157 },
                                                  { 255, 220, 157 }, { 255, 157, 157 }, { 200, 220, 255 } };
    const std::array<int, 3>& c = kColors[int(most)];
    const int lo = std::min({ c[0], c[1], c[2] }), hi = std::max({ c[0], c[1], c[2] });
    constexpr double kSaturated = 200;
    const double f = kSaturated / std::max(1, hi - lo);
    onIndicator(std::array<uint8_t, 4> { uint8_t((c[0] - lo) * f), uint8_t((c[1] - lo) * f), uint8_t((c[2] - lo) * f), 255 });
}

std::vector<JPSolutions::Issue*> JPIssuesPanel::selections() const {
    std::vector<S::Issue*> out;
    for (const int r : m_table->selectedRows())
        if (r >= 0 && size_t(r) < m_solutions.issues().size()) out.push_back(m_solutions.issues()[size_t(r)].get());
    return out;
}

void JPIssuesPanel::showMilestone() {
    const S::Milestone m = m_solutions.targetMilestone();
    m_milestone->setCurrentIndex(int(m));
    m_milestoneText->setText(S::description(m));
}

void JPIssuesPanel::selectionChanged() {
    showButtons();
    const auto chosen = selections();
    if (chosen.size() == 1 && chosen.front()->activate) chosen.front()->activate();
    showIssue();
}

void JPIssuesPanel::showButtons() {
    // As OpenPnP's selectionActions: what the chosen issues can do.
    const auto chosen = selections();
    bool accept = false, dismiss = false, reopen = false, info = false;
    for (const S::Issue* i : chosen) {
        if (i->state != S::State::Dismissed) dismiss = true;
        if ((i->state == S::State::Dismissed || i->canBeUndone) && i->state != S::State::Open) reopen = true;
        if (i->canBeAccepted && i->state == S::State::Open) accept = true;
        if (!i->uri.empty()) info = true;
    }
    m_accept->setEnabled(accept && chosen.size() == 1);
    m_dismiss->setEnabled(dismiss);
    m_reopen->setEnabled(reopen);
    m_info->setEnabled(info);
}

void JPIssuesPanel::showIssue() {
    const auto chosen = selections();
    JPSetupProperties::Form form;
    if (chosen.size() == 1) {
        S::Issue* issue = chosen.front();
        JPFormBuilder add(form);
        add.tab("Issue");
        add.group("Subject");
        add.note(issue->subject);
        add.group("Issue");
        add.note(issue->issue);
        add.group("Solution");
        add.note(issue->solution);
        if (!issue->extendedDescription.empty() || !issue->properties.empty() || !issue->choices.empty()) add.group("");
        if (!issue->extendedDescription.empty()) add.note(issue->extendedDescription);
        for (const S::Property& p : issue->properties) {
            const std::string name = "issue.property." + p.label;
            switch (p.kind) {
                case S::Property::Kind::Text:
                    add.text(name, p.label, p.getText, p.setText);
                    break;
                case S::Property::Kind::Flag:
                    add.flag(name, p.label, p.getFlag, p.setFlag);
                    break;
                case S::Property::Kind::Integer:
                    add.integer(name, p.label, [g = p.getNumber] { return int(g()); }, [s = p.setNumber](int v) { s(v); }, p.min,
                                p.max);
                    break;
                case S::Property::Kind::Number:
                case S::Property::Kind::Length:
                    add.number(name, p.label, p.getNumber, p.setNumber);
                    break;
                case S::Property::Kind::Action:
                    add.button(p.label + "|" + p.actionLabel, p.actionLabel, p.tooltip);
                    break;
            }
            if (!p.tooltip.empty() && p.kind != S::Property::Kind::Action) add.tip(p.tooltip);
        }
        if (!issue->choices.empty()) {
            JPFormBuilder::Named named;
            for (const S::Choice& c : issue->choices) named.add(c.value, c.value);
            add.byName("issue.choice", "Choice", named, [issue] { return issue->choice; },
                       [issue](const std::string& v) { issue->choice = v; });
            for (const S::Choice& c : issue->choices)
                if (c.value == issue->choice) add.note(c.description);
        }
    }
    // Not while a control of the page shown is still at work: made again next frame.
    jPostToNextFrame([this, alive = std::weak_ptr<bool>(m_alive), form = std::move(form)]() mutable {
        if (const auto a = alive.lock(); a && *a) m_form->setForm(std::move(form));
    });
}

void JPIssuesPanel::setState(JPSolutions::State state) {
    std::string why;
    for (S::Issue* i : selections()) {
        if (!m_solutions.setState(*i, state, why)) {
            if (!why.empty() && showError) showError(why);
            break;
        }
    }
    m_warn->setText(" After each round of solving issues, please run Find Issues & Solutions again to catch dependent issues.");
    m_table->refresh();
    updateIndicator();
    showMilestone();
    selectionChanged();
}

} // inline namespace jf
