// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionSettingsPanel.h"

#include "JPVisionPipelineActions.h"

#include "JPUiParts.h"

#include "model/JPOpenPnpIds.h"
#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"
#include "setup/JPVisionForms.h"

#include <j/core/Dialog.h>
#include <j/core/JLabel.h>
#include <j/core/JSeparator.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

inline namespace jf {

namespace {

// OpenPnP's VisionTypeFilter, in its order.
const std::vector<std::string> kTypes { "BottomVision", "FiducialVision" };
// The clipboard's form (OpenPnP's VisionSettingsConfigurationHolder).
constexpr const char* kHolder = "vision-settings-configuration-holder";

std::unique_ptr<JSeparator> toolSeparator(JSceneGraph& graph) {
    return std::make_unique<JSeparator>(graph, JSeparator::JOrientation::Vertical, JPIconButton::size());
}

std::string joined(const std::vector<std::string>& items) {
    std::string s;
    for (const std::string& i : items) s += (s.empty() ? "" : ", ") + i;
    return s;
}

} // namespace

JPVisionSettingsPanel::JPVisionSettingsPanel(JSceneGraph& graph, JPConfiguration& config, double split)
    : JContainer(graph), m_config(config), m_model(config) {
    JPUiParts::asPanel(*this);
    const JStyle& st = JStyle::current();
    m_model.onChanged = [this] {
        m_form->refresh();
        changed();
    };
    m_model.usedIn = [this](const JPVisionSettings& v) { return usedIn(v); };

    auto bar = JPUiParts::row(graph);
    auto tool = [&](const char* name, const char* icon, const char* tip) {
        return bar->add(std::make_unique<JPIconButton>(graph, name, icon, tip));
    };
    tool("New Settings", "general-add", "Create a new Bottom Vision Settings.")->onClicked.connect([this] { newSettings(); });
    m_delete = tool("Delete Settings", "general-remove", "Delete the currently selected settings.");
    m_delete->setLeads(JPIconButton::Leads::Elsewhere);
    m_delete->onClicked.connect([this] { deleteSettings(); });
    bar->add(toolSeparator(graph));
    m_copy = tool("Copy Vision Settings to Clipboard", "copy",
                  "Copy the currently selected vision settings to the clipboard in text format.");
    m_copy->onClicked.connect([this] { copySettings(); });
    tool("Create Vision Settings from Clipboard", "paste", "Create a new vision setting from a definition on the clipboard.")
        ->onClicked.connect([this] { pasteSettings(); });
    bar->add(toolSeparator(graph));
    bar->add(std::make_unique<JContainer>(graph, 0.f, 0.f))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    JLabel* typeLabel = bar->add(std::make_unique<JLabel>(graph, "Type"));
    typeLabel->setFixedSize(JTextHelper::measureWidth("Type") + st.spacing, st.controlHeight);
    m_type = bar->add(std::make_unique<JComboBox>(graph, kTypes));
    m_type->setFixedSize(JTextHelper::measureWidth("FiducialVision") + 2 * st.fieldPadding + st.controlHeight + 2 * st.spacing,
                         st.controlHeight);
    m_type->setCurrentIndex(0);
    m_type->onIndexChanged.connect([this](int i) {
        m_model.setKind(i == 1 ? JPVisionSettings::Kind::Fiducial : JPVisionSettings::Kind::Bottom);
        m_table->clearSelection();
        refresh();
    });
    add(std::move(bar));

    m_tablePane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_formPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    for (JContainer* p : { m_tablePane.get(), m_formPane.get() })
        p->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_table = m_tablePane->add(std::make_unique<JPTable>(graph));
    m_table->setStateKey("vision");
    m_table->setModel(&m_model);
    m_table->onSelectionChanged.connect([this] { showForm(); });
    m_table->onEditRefused = [](const std::string&) {};
    m_form = m_formPane->add(std::make_unique<JPSetupForm>(graph));
    m_form->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_form->onChanged = [this](const std::string& property) {
        if (property.find(":parameter:") != std::string::npos) {
            if (onParameterChanged) onParameterChanged();
            act(m_shown, property);
            return;
        }
        m_table->refresh();
        changed();
    };
    m_form->onAction = [this](const std::string& action) { act(m_shown, action); };
    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_tablePane.get(), float(split));
    m_split->addPane(m_formPane.get(), float(1 - split));
    showForm();
}

double JPVisionSettingsPanel::split() const {
    const std::vector<float> f = m_split->fractions();
    return f.empty() ? 0.5 : f.front();
}

void JPVisionSettingsPanel::refresh() {
    m_table->refresh();
    m_shown.clear();
    showForm();
}

void JPVisionSettingsPanel::changed() {
    if (onChanged) onChanged();
}

void JPVisionSettingsPanel::selectFor(const JPPart& part) {
    const bool bottom = m_type->currentIndex() != 1;
    const auto defaults = machineDefaults ? machineDefaults() : std::pair<std::string, std::string> {};
    const JPVisionSettings* v = m_config.inheritedVision(part, bottom ? JPVisionSettings::Kind::Bottom : JPVisionSettings::Kind::Fiducial,
                                                         bottom ? defaults.first : defaults.second);
    if (!v) return;
    const auto chosen = selections();
    if (chosen.size() == 1 && chosen.front() == v) return;
    m_table->selectRow(m_model.rowOf(v->id));
}

std::vector<JPVisionSettings*> JPVisionSettingsPanel::selections() const {
    std::vector<JPVisionSettings*> out;
    for (const int r : m_table->selectedRows())
        if (JPVisionSettings* v = m_model.settings(r)) out.push_back(v);
    return out;
}

std::string JPVisionSettingsPanel::usedIn(const JPVisionSettings& v) const {
    const auto defaults = machineDefaults ? machineDefaults() : std::pair<std::string, std::string> {};
    const bool bottom = v.kind == JPVisionSettings::Kind::Bottom;
    return joined(m_config.visionUsedIn(v, bottom ? defaults.first : defaults.second,
                                        bottom ? "Bottom Vision" : "Fiducal Locator"));
}

void JPVisionSettingsPanel::act(const std::string& settingsId, const std::string& action) {
    const size_t colon = action.find(':');
    if (colon == std::string::npos || settingsId.empty()) return;
    if (action.find("Pipeline") != std::string::npos || action.find(":test") != std::string::npos
        || action.find(":detectOffsets") != std::string::npos || action.find(":parameter:") != std::string::npos) {
        JPVisionPipelineActions::Hooks hooks;
        hooks.edit = [this](const std::string& id) {
            if (editPipeline) editPipeline(id, JPVisionForms::Holder {});
        };
        hooks.preview = [this](const std::string& id, const std::string& parameter) {
            if (previewParameter) previewParameter(id, JPVisionForms::Holder {}, parameter);
        };
        hooks.machineDefault = [this](JPVisionSettings::Kind kind) {
            const auto defaults = machineDefaults ? machineDefaults() : std::pair<std::string, std::string> {};
            return m_config.visionSettings(kind == JPVisionSettings::Kind::Bottom ? defaults.first : defaults.second);
        };
        hooks.test = [this](const std::string& id, const std::string& test) {
            if (visionTest) visionTest(id, JPVisionForms::Holder {}, test);
        };
        hooks.changed = [this] {
            // Its sliders follow the pipeline.
            m_shown.clear();
            showForm();
            m_table->refresh();
            changed();
        };
        JPVisionPipelineActions::act(m_config, settingsId, action.substr(colon + 1), hooks);
        return;
    }
    if (action.rfind(":reset") == std::string::npos) return;
    JDialogOptions opts;
    opts.okLabel = "Yes";
    opts.cancelLabel = "No";
    JDialog::confirm("Reset to Default", "This will reset the vision settings to the default settings. Are you sure?",
                     [this, settingsId] {
                         std::string why;
                         const JPVisionSettings* v = m_config.visionSettings(settingsId);
                         const auto defaults = machineDefaults ? machineDefaults() : std::pair<std::string, std::string> {};
                         const bool bottom = !v || v->kind == JPVisionSettings::Kind::Bottom;
                         if (!JPVisionForms::act(m_config, settingsId, "reset", JPVisionForms::Holder {}, bottom ? defaults.first : defaults.second,
                                                 why))
                             return;
                         m_shown.clear();
                         showForm();
                         m_table->refresh();
                         changed();
                     },
                     nullptr, opts);
}

void JPVisionSettingsPanel::showForm() {
    const auto chosen = selections();
    m_delete->setEnabled(!chosen.empty());
    m_copy->setEnabled(!chosen.empty());
    const std::string id = chosen.size() == 1 ? chosen.front()->id : std::string();
    if (id == m_shown) {
        m_form->refresh();
        return;
    }
    m_shown = id;
    m_form->setForm(id.empty() ? JPSetupProperties::Form {}
                               : JPVisionForms::forSettings(m_config, id, usedIn(*chosen.front()), tests()));
}

void JPVisionSettingsPanel::newSettings() {
    // Of the type shown, as OpenPnP makes it, chosen.
    const bool bottom = m_type->currentIndex() != 1;
    const std::string id = JPOpenPnpIds::create(bottom ? "BVS" : "FVS");
    m_config.addVisionSettings(JPVisionSettings::create(bottom ? JPVisionSettings::Kind::Bottom : JPVisionSettings::Kind::Fiducial, id));
    m_table->refresh();
    m_table->selectRow(m_model.rowOf(id));
    changed();
}

void JPVisionSettingsPanel::deleteSettings() {
    const auto chosen = selections();
    if (chosen.empty()) return;
    // What is used cannot go.
    std::vector<std::string> used;
    for (const JPVisionSettings* v : chosen) {
        const std::string u = usedIn(*v);
        if (!u.empty()) used.push_back(u);
    }
    if (!used.empty()) {
        JDialog::message("Error", "The selection cannot be deleted. It is used by " + joined(used) + ".");
        return;
    }
    std::vector<std::string> names, ids;
    for (const JPVisionSettings* v : chosen) {
        names.push_back(v->name);
        ids.push_back(v->id);
    }
    std::string formatted;
    if (names.size() <= 10) formatted = joined(names);
    else formatted = joined({ names.begin(), names.begin() + 5 }) + ", and " + std::to_string(names.size() - 5) + " others";
    JDialogOptions opts;
    opts.okLabel = "Yes";
    opts.cancelLabel = "No";
    JDialog::confirm("Delete " + std::to_string(chosen.size()) + " vision settings?",
                     "Are you sure you want to delete " + formatted + "?",
                     [this, ids] {
                         for (const std::string& id : ids) m_config.removeVisionSettings(id);
                         m_table->clearSelection();
                         refresh();
                         changed();
                     },
                     nullptr, opts);
}

void JPVisionSettingsPanel::copySettings() {
    const auto chosen = selections();
    if (chosen.empty()) return;
    JPXmlNode holder(kHolder);
    for (const JPVisionSettings* v : chosen) holder.add(v->toXml());
    std::string text = JPXmlWriter::text(holder);
    if (!text.empty() && text.back() == '\n') text.pop_back();
    JWidget::clipboardSet(text);
}

void JPVisionSettingsPanel::pasteSettings() {
    JPXmlElement root;
    std::string error;
    if (!JPXmlReader::parse(JWidget::clipboardGet(), root, error)) {
        JDialog::message("Paste Failed", error);
        return;
    }
    std::vector<std::string> added;
    for (const JPXmlElement& e : root.children) {
        if (e.name != "vision-settings" && e.name != "visionSettings") continue;
        JPVisionSettings v = JPVisionSettings::fromXml(e);
        if (v.kind == JPVisionSettings::Kind::Other) continue;
        // A new id of its kind; a name already used gets " (Copy)".
        v.id = JPOpenPnpIds::create(v.id.substr(0, 3));
        for (const JPVisionSettings& other : m_config.visionSettings())
            if (other.name == v.name) {
                v.name += " (Copy)";
                break;
            }
        added.push_back(v.id);
        m_config.addVisionSettings(v);
    }
    if (added.empty()) {
        JDialog::message("Paste Failed", "The clipboard does not hold vision settings.");
        return;
    }
    m_table->refresh();
    std::vector<int> rows;
    for (const std::string& id : added) rows.push_back(m_model.rowOf(id));
    m_table->selectRows(rows);
    changed();
}

} // inline namespace jf
