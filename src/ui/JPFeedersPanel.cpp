// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeedersPanel.h"

#include "JPUiParts.h"

#include "setup/JPFeederForms.h"

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

} // namespace

JPFeedersPanel::JPFeedersPanel(JSceneGraph& graph, JPConfiguration& config, double split)
    : JContainer(graph), m_config(config), m_model(config) {
    JPUiParts::asPanel(*this);
    const JStyle& st = JStyle::current();
    m_model.onChanged = [this] {
        if (m_form) m_form->refresh();
        changed();
    };
    m_model.partUsed = [this](const std::string& partId) { return !partUsed || partUsed(partId); };

    auto bar = JPUiParts::row(graph);
    auto tool = [&](const char* name, const char* icon, const char* tip) {
        return bar->add(std::make_unique<JPIconButton>(graph, name, icon, tip));
    };
    JPIconButton* newOne = tool("New Feeder...", "general-add", "Create a new feeder.");
    newOne->setLeads(JPIconButton::Leads::Elsewhere);
    newOne->onClicked.connect([this] { newFeeder(""); });
    m_delete = tool("Delete Feeder...", "general-remove", "Delete the selected feeder.");
    m_delete->setLeads(JPIconButton::Leads::Elsewhere);
    m_delete->onClicked.connect([this] { deleteFeeders(); });
    bar->add(toolSeparator(graph));
    m_pick = tool("Pick...", "pick", "Perform a feed and pick on the selected feeder.");
    m_pick->onClicked.connect([this] { pick(); });
    m_feed = tool("Feed...", "feeder-feed", "Command the selected feeder to perform a feed operation.");
    m_feed->onClicked.connect([this] {
        if (JPFeeder* f = selection(); f && feed(*f)) changed();
    });
    m_moveCamera = tool("Move Camera...", "position-camera-on-feeder",
                        "Move the camera to the selected feeder's current pick location.");
    m_moveCamera->onClicked.connect([this] { moveToPick(Tool::Camera); });
    m_moveTool = tool("Move Tool...", "position-nozzle-on-feeder",
                      "Move the tool to the selected feeder's current pick location.");
    m_moveTool->onClicked.connect([this] { moveToPick(Tool::Nozzle); });
    bar->add(std::make_unique<JContainer>(graph, 0.f, 0.f))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    JLabel* searchLabel = bar->add(std::make_unique<JLabel>(graph, "Search"));
    searchLabel->setFixedSize(JTextHelper::measureWidth("Search") + st.spacing, st.controlHeight);
    m_search = bar->add(std::make_unique<JLineEdit>(graph, ""));
    m_search->setFixedSize(JTextHelper::measureWidth("M") * kSearchColumns, st.controlHeight);
    m_search->onTextChanged.connect([this](const std::string& t) { m_table->setFilter(t); });
    add(std::move(bar));

    // The table over the chosen feeder's setup, a divider between to drag.
    m_tablePane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_formPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    for (JContainer* p : { m_tablePane.get(), m_formPane.get() })
        p->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_table = m_tablePane->add(std::make_unique<JPTable>(graph));
    m_table->setModel(&m_model);
    m_table->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_table->onSelectionChanged.connect([this] { selectionChanged(); });
    m_table->onEditRefused = [](const std::string&) {};
    m_form = m_formPane->add(std::make_unique<JPSetupForm>(graph));
    m_form->setOpenPnpPlaceButtons(true);
    m_form->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_form->onChanged = [this](const std::string&) {
        m_table->refresh();
        changed();
    };
    m_form->onAction = [this](const std::string& action) {
        if (!JPFeederForms::act(m_config, m_shown, action)) return;
        m_form->refresh();
        m_table->refresh();
        changed();
    };
    m_form->onCapture = [this](const JPSetupProperties::Row& row, Tool t) { capture(row, t); };
    m_form->onMoveTo = [this](const JPSetupProperties::Row& row, Tool t) { goTo(row, t); };
    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_tablePane.get(), float(split));
    m_split->addPane(m_formPane.get(), float(1 - split));

    buildMenu();
    m_table->setContextMenu(m_menu.get());
    selectionChanged();
}

double JPFeedersPanel::split() const {
    const std::vector<float> f = m_split->fractions();
    return f.empty() ? 0.5 : f.front();
}

void JPFeedersPanel::buildMenu() {
    JSceneGraph& g = m_graph;
    m_menu = std::make_unique<JMenu>("Feeders");
    auto sub = [&](const std::string& title) {
        m_subMenus.push_back(std::make_unique<JMenu>(title));
        JMenuItem* item = m_menu->add(g, title, {}, m_subMenus.back().get());
        return std::pair { m_subMenus.back().get(), item };
    };
    auto [enabled, enabledItem] = sub("Set Enabled");
    m_setEnabled = enabledItem;
    m_setEnabled->setTooltip("Set feeder(s) enabled to... ");
    for (bool on : { true, false }) {
        JMenuItem* item = enabled->add(g, on ? "Enabled" : "Disabled");
        item->setTooltip(std::string("Set feeder(s) enabled to ") + (on ? "Enabled" : "Disabled"));
        item->onTriggered.connect([this, on] {
            for (JPFeeder* f : selections()) f->setEnabled(on);
            m_table->refresh();
            changed();
        });
    }
    auto [options, optionsItem] = sub("Set Feed option");
    m_setFeedOptions = optionsItem;
    m_setFeedOptions->setTooltip("Enables skipping nozzle transition to pick again from the last location which is handy "
                                 "for feeder tuning or fixing abnormal situation as e.g. manual part replacement on tray "
                                 "when lost from nozzle tip.");
    for (const auto o : { JPFeeder::FeedOptions::Normal, JPFeeder::FeedOptions::SkipNext, JPFeeder::FeedOptions::Disable }) {
        JMenuItem* item = options->add(g, JPFeeder::feedOptionsName(o));
        item->setTooltip(std::string("Set feeder(s) feed option to ") + JPFeeder::feedOptionsName(o));
        item->onTriggered.connect([this, o] {
            for (JPFeeder* f : selections())
                if (f->supportsFeedOptions()) f->setFeedOptions(o);
            m_table->refresh();
            changed();
        });
    }
}

void JPFeedersPanel::refresh() {
    m_table->refresh();
    selectionChanged();
}

std::vector<JPFeeder*> JPFeedersPanel::selections() const {
    std::vector<JPFeeder*> out;
    for (const int r : m_table->selectedRows())
        if (JPFeeder* f = m_model.feeder(r)) out.push_back(f);
    return out;
}

JPFeeder* JPFeedersPanel::selection() const {
    const auto s = selections();
    return s.size() == 1 ? s.front() : nullptr;
}

void JPFeedersPanel::selectFeeder(const std::string& id) {
    m_table->selectRow(m_model.rowOf(id));
}

void JPFeedersPanel::selectionChanged() {
    // As OpenPnP's action groups: one feeder for all, several for Delete and the menu.
    const size_t n = selections().size();
    m_delete->setEnabled(n > 0);
    for (JPIconButton* b : { m_pick, m_feed, m_moveCamera, m_moveTool }) b->setEnabled(n == 1);
    m_setEnabled->setEnabled(n > 0);
    m_setFeedOptions->setEnabled(n > 0);
    showForm();
}

void JPFeedersPanel::showForm() {
    const JPFeeder* f = selection();
    const std::string id = f ? f->id() : std::string();
    if (id == m_shown) {
        m_form->refresh();
        return;
    }
    m_shown = id;
    m_form->setForm(id.empty() ? JPSetupProperties::Form {}
                               : JPFeederForms::forFeeder(m_config, id, [](const std::string& why) {
                                     JDialog::message("Error", why);
                                 }));
}

void JPFeedersPanel::changed() {
    if (onChanged) onChanged();
}

void JPFeedersPanel::showFeederForPart(const std::string& partId) {
    m_search->setText("");
    m_table->setFilter("");
    // An enabled feeder first, else a disabled one, else a new one.
    for (const bool enabled : { true, false })
        for (const JPFeeder& f : m_config.feeders())
            if (f.partId() == partId && f.enabled() == enabled) {
                selectFeeder(f.id());
                return;
            }
    newFeeder(partId);
}

void JPFeedersPanel::newFeeder(const std::string& partId) {
    if (m_config.parts().empty()) {
        JDialog::message("Error", "There are currently no parts defined in the system. Please create at least one part "
                                  "before creating a feeder.");
        return;
    }
    if (!chooseClass) return;
    const std::string title = partId.empty() ? "Select Feeder..." : "Select Feeder for " + partId + "...";
    chooseClass(title, "Please select a Feeder implementation from the list below.", JPFeeder::classNames(),
                [this, partId](std::string className) {
                    if (className.empty()) return;
                    const std::string part = partId.empty() ? m_config.parts().front()->id : partId;
                    const std::string id = m_config.addFeeder(JPFeeder::create(className, part)).id();
                    m_search->setText("");
                    m_table->setFilter("");
                    m_table->refresh();
                    selectFeeder(id);
                    changed();
                });
}

void JPFeedersPanel::deleteFeeders() {
    const auto chosen = selections();
    if (chosen.empty()) return;
    std::string names;
    for (size_t i = 0; i < chosen.size() && i < 3; ++i) names += (i ? ", " : "") + chosen[i]->name();
    if (chosen.size() > 3) names += ", and " + std::to_string(chosen.size() - 3) + " others";
    std::vector<std::string> doomed;
    for (const JPFeeder* f : chosen) doomed.push_back(f->id());
    JDialogOptions opts;
    opts.okLabel = "Yes";
    opts.cancelLabel = "No";
    JDialog::confirm("Delete " + std::to_string(chosen.size()) + " feeders?", "Are you sure you want to delete " + names + "?",
        [this, doomed] {
            for (const std::string& id : doomed) m_config.removeFeeder(id);
            m_table->refresh();
            selectionChanged();
            changed();
        },
        nullptr, opts);
}

bool JPFeedersPanel::feed(JPFeeder& f) {
    if (f.partId().empty()) {
        JDialog::message("Error", "Feeder " + f.name() + " has no part.");
        return false;
    }
    std::string why;
    if (!f.feed(why)) {
        m_table->refresh();
        m_form->refresh();
        JDialog::message("Error", why);
        return false;
    }
    m_table->refresh();
    m_form->refresh();
    return true;
}

void JPFeedersPanel::pick() {
    if (JPFeeder* f = selection()) pickFrom(*f);
}

void JPFeedersPanel::pickFrom(JPFeeder& f) {
    if (!feed(f)) return;
    changed();
    const auto at = f.pickLocation();
    if (!at) {
        JDialog::message("Error", "jplacer does not work out where a " + f.typeName() + " picks yet.");
        return;
    }
    if (pickAt) pickAt(*at);
}

void JPFeedersPanel::moveToPick(Tool tool) {
    const JPFeeder* f = selection();
    if (!f) return;
    const auto at = f->pickLocation();
    if (!at) {
        JDialog::message("Error", "jplacer does not work out where a " + f->typeName() + " picks yet.");
        return;
    }
    const JPLocation l = at->convertToUnits(JPLengthUnit::Millimeters);
    // The camera over it at safe Z; the tool down to it, as OpenPnP moves them.
    Where to { l.x(), l.y(), l.z(), l.rotation() };
    if (tool == Tool::Camera) to[2].reset();
    if (moveTo) moveTo(tool, to);
}

void JPFeedersPanel::capture(const JPSetupProperties::Row& row, Tool tool) {
    if (!whereIs) return;
    Where now = whereIs(tool);
    // As OpenPnP: the camera's coordinates are X, Y and rotation (its Z is what a Z probe finds).
    if (tool == Tool::Camera) now[2].reset();
    bool any = false;
    for (size_t i = 0; i < row.cells.size() && i < now.size(); ++i)
        if (!row.cells[i].property.empty() && now[i]) any = m_form->set(row.cells[i].property, JVariant(*now[i])) || any;
    if (!any) {
        JDialog::message("Error", "Nothing captured: the machine is not connected.");
        return;
    }
    m_table->refresh();
    changed();
}

void JPFeedersPanel::goTo(const JPSetupProperties::Row& row, Tool tool) {
    Where to;
    for (size_t i = 0; i < row.cells.size() && i < to.size(); ++i) {
        if (row.cells[i].property.empty()) continue;
        const JVariant v = m_form->get(row.cells[i].property);
        if (v.isDouble() || v.isInt()) to[i] = v.toDouble();
    }
    // A camera stays at safe Z; a place without Z leaves the tool there too.
    if (tool == Tool::Camera) to[2].reset();
    if (moveTo) moveTo(tool, to);
}

} // inline namespace jf
