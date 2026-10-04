// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPartsPanel.h"

#include "JPGroupFrame.h"
#include "JPTextField.h"
#include "JPUiParts.h"

#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"

#include <j/core/Dialog.h>
#include <j/core/JLabel.h>
#include <j/core/JSeparator.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <cstdlib>

inline namespace jf {

namespace {

// OpenPnP's search box is fifteen characters wide.
constexpr int kSearchColumns = 15;

std::unique_ptr<JSeparator> toolSeparator(JSceneGraph& graph) {
    return std::make_unique<JSeparator>(graph, JSeparator::JOrientation::Vertical, JPIconButton::size());
}

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

} // namespace

JPPartsPanel::JPPartsPanel(JSceneGraph& graph, JPConfiguration& config, double split)
    : JContainer(graph), m_config(config), m_model(config) {
    JPUiParts::asPanel(*this);
    const JStyle& st = JStyle::current();
    m_model.onChanged = [this] { changed(); };

    // The toolbar, and the search box at its right.
    auto bar = JPUiParts::row(graph);
    auto tool = [&](const char* name, const char* icon, const char* tip) {
        return bar->add(std::make_unique<JPIconButton>(graph, name, icon, tip));
    };
    tool("New Part...", "general-add", "Create a new part, specifying it's ID.")->onClicked.connect([this] { newPart(); });
    m_delete = tool("Delete Part", "general-remove", "Delete the currently selected part.");
    m_delete->onClicked.connect([this] { deleteParts(); });
    bar->add(toolSeparator(graph));
    m_pick = tool("Pick Part", "pick", "Perform a feed and pick of the selected part from the first available feeder.");
    m_pick->onClicked.connect([this] {
        if (const JPPart* p = selectedPart(); p && onPickPart) onPickPart(*p);
    });
    bar->add(toolSeparator(graph));
    m_copy = tool("Copy Part to Clipboard", "copy", "Copy the currently selected part to the clipboard in text format.");
    m_copy->onClicked.connect([this] { copyPart(); });
    tool("Create Part from Clipboard", "paste", "Create a new part from a definition on the clipboard.")
        ->onClicked.connect([this] { pastePart(); });
    bar->add(std::make_unique<JContainer>(graph, 0.f, 0.f))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    JLabel* searchLabel = bar->add(std::make_unique<JLabel>(graph, "Search"));
    searchLabel->setFixedSize(JTextHelper::measureWidth("Search") + st.spacing, st.controlHeight);
    m_search = bar->add(std::make_unique<JLineEdit>(graph, ""));
    m_search->setFixedSize(JTextHelper::measureWidth("M") * kSearchColumns, st.controlHeight);
    m_search->onTextChanged.connect([this](const std::string& t) { m_table->setFilter(t); });
    add(std::move(bar));

    // The table over the chosen part's tabs, a divider between to drag.
    m_tablePane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_tabsPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    for (JContainer* p : { m_tablePane.get(), m_tabsPane.get() })
        p->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_table = m_tablePane->add(std::make_unique<JPTable>(graph));
    m_table->setModel(&m_model);
    m_table->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_table->onSelectionChanged.connect([this] { updateWizards(); });
    m_table->onEditRefused = [](const std::string&) {};
    m_tabs = m_tabsPane->add(std::make_unique<JTabWidget>(graph, 0.f, 0.f));
    m_tabs->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_tablePane.get(), float(split));
    m_split->addPane(m_tabsPane.get(), float(1 - split));
    updateWizards();
}

double JPPartsPanel::split() const {
    const std::vector<float> f = m_split->fractions();
    return f.empty() ? 0.5 : f.front();
}

void JPPartsPanel::refresh() {
    m_table->refresh();
    updateWizards();
}

std::vector<JPPart*> JPPartsPanel::selections() const {
    std::vector<JPPart*> out;
    for (const int r : m_table->selectedRows())
        if (JPPart* p = m_model.part(r)) out.push_back(p);
    return out;
}

const JPPart* JPPartsPanel::selectedPart() const {
    const auto s = selections();
    return s.size() == 1 ? s.front() : nullptr;
}

void JPPartsPanel::selectPart(const JPPart* part) {
    if (selectedPart() != part) m_table->selectRow(m_model.rowOf(part));
}

void JPPartsPanel::changed() {
    if (onChanged) onChanged();
}

void JPPartsPanel::updateWizards() {
    const auto chosen = selections();
    // As OpenPnP's action groups: one part for Delete, Pick and Copy; several for Delete.
    m_delete->setEnabled(!chosen.empty());
    m_pick->setEnabled(chosen.size() == 1);
    m_copy->setEnabled(chosen.size() == 1);

    const JPPart* p = selectedPart();
    const std::string id = p ? p->id : std::string();
    if (id == m_shownPart && (p != nullptr) == (m_settings != nullptr)) return;
    if (m_tabs->tabCount() > 0) m_lastTab = m_tabs->activeTab();
    while (m_tabs->tabCount() > 0) m_tabs->removeTab(0);
    m_settings.reset();
    m_shownPart = id;
    if (!p) return;

    // Settings: the pick conditions.
    JSceneGraph& g = m_graph;
    m_settings = std::make_unique<JContainer>(g, 0.f, 0.f);
    JPUiParts::asPanel(*m_settings);
    auto frame = std::make_unique<JPGroupFrame>(g, "Pick Conditions");
    frame->setAlignItems(JAlignItems::Stretch);
    auto row = JPUiParts::row(g);
    auto label = std::make_unique<JLabel>(g, "Feed & Pick Retry Count");
    label->setTooltip("The number of retries for the feed and pick process for each placement. The nozzle is cleared "
                      "(and part is discarded) after a failed attempt. Each retry is consecutive.");
    row->add(std::move(label));
    JPTextField* retries = row->add(std::make_unique<JPTextField>(g));
    retries->setValue(std::to_string(p->pickRetryCount));
    const std::string partId = p->id;
    retries->onCommitted.connect([this, retries, partId](std::string text) {
        JPPart* part = m_config.part(partId);
        if (!part) return;
        char* end = nullptr;
        const long v = std::strtol(trimmed(text).c_str(), &end, 10);
        if (end == trimmed(text).c_str() || v < 0) {
            retries->setValue(std::to_string(part->pickRetryCount));
            return;
        }
        part->pickRetryCount = int(v);
        changed();
    });
    const float inner = JStyle::current().controlHeight;
    frame->add(std::move(row));
    frame->setVSizePolicy(JSizePolicyMode::Fixed);
    frame->setSize(0.f, inner + JPGroupFrame::extraHeight());
    m_settings->add(std::move(frame));
    m_tabs->addTab("Settings", m_settings.get());
    if (m_lastTab >= 0 && m_lastTab < m_tabs->tabCount()) m_tabs->setActiveTab(m_lastTab);
}

void JPPartsPanel::newPart() {
    if (m_config.packages().empty()) {
        JDialog::message("Error", "There are currently no packages defined in the system. Please create at least one "
                                  "package before creating a part.");
        return;
    }
    JDialog::input("New Part", "Please enter an ID for the new part.", [this](std::string text) {
        const std::string id = trimmed(text);
        if (id.empty()) return;
        if (m_config.part(id)) {
            JDialog::message("Error", "Part ID " + id + " already exists.");
            newPart();   // and asked again, as OpenPnP asks
            return;
        }
        auto part = std::make_shared<JPPart>();
        part->id = id;
        part->packageId = m_config.packages().front()->id;
        m_config.addPart(part);
        m_table->refresh();
        selectPart(part.get());
        changed();
    });
}

void JPPartsPanel::deleteParts() {
    const auto chosen = selections();
    if (chosen.empty()) return;
    std::string ids;
    for (size_t i = 0; i < chosen.size() && i < 3; ++i) ids += (i ? ", " : "") + chosen[i]->id;
    if (chosen.size() > 3) ids += ", and " + std::to_string(chosen.size() - 3) + " others";
    std::vector<std::string> doomed;
    for (const JPPart* p : chosen) doomed.push_back(p->id);
    JDialogOptions opts;
    opts.okLabel = "Yes";
    opts.cancelLabel = "No";
    JDialog::confirm("Delete " + std::to_string(chosen.size()) + " parts?", "Are you sure you want to delete " + ids + "?",
        [this, doomed] {
            for (const std::string& id : doomed) m_config.removePart(id);
            refresh();
            changed();
        },
        nullptr, opts);
}

void JPPartsPanel::copyPart() {
    const JPPart* p = selectedPart();
    if (!p) return;
    std::string text = JPXmlWriter::text(p->toXml());
    if (!text.empty() && text.back() == '\n') text.pop_back();
    JWidget::clipboardSet(text);
}

void JPPartsPanel::pastePart() {
    JDialog::input("Create Part from Clipboard", "Please enter an ID for the pasted part.", [this](std::string text) {
        const std::string id = trimmed(text);
        if (id.empty()) return;
        if (m_config.part(id)) {
            JDialog::message("Error", "Part ID " + id + " already exists.");
            pastePart();
            return;
        }
        JPXmlElement root;
        std::string error;
        if (!JPXmlReader::parse(JWidget::clipboardGet(), root, error) || root.name != "part") {
            JDialog::message("Paste Failed", error.empty() ? std::string("The clipboard does not hold a part.") : error);
            return;
        }
        auto part = std::make_shared<JPPart>(JPPart::fromXml(root));
        part->id = id;
        m_config.addPart(part);
        m_table->refresh();
        selectPart(part.get());
        changed();
    });
}

} // inline namespace jf
