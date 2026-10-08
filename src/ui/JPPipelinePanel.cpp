// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPipelinePanel.h"

#include "JPUiParts.h"

#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"
#include "pipeline/JPStageRegistry.h"
#include "setup/JPFormBuilder.h"

#include <j/core/JSeparator.h>
#include <j/core/JStyle.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>

inline namespace jf {

namespace {

// The splits, as OpenPnP's: the stages over the settings, half each; the
// stages' table over their description, four fifths.
constexpr float kMainSplit = 0.5f, kStagesSplit = 0.8f;
// The property a stage is controlled by, by convention (VisionUtils.PIPELINE_CONTROL_PROPERTY_NAME).
constexpr const char* kControlProperty = "property-name";

// A setting's XML name as its Java property shows it ("kernel-size": "kernelSize").
std::string javaName(const std::string& attribute) {
    std::string out;
    bool up = false;
    for (const char c : attribute) {
        if (c == '-') {
            up = true;
            continue;
        }
        out += up ? char(std::toupper(static_cast<unsigned char>(c))) : c;
        up = false;
    }
    return out;
}

// OpenPnP's HTML descriptions as lines of text.
std::string plain(const std::string& html) {
    std::string out;
    for (size_t i = 0; i < html.size();) {
        if (html[i] != '<') {
            out += html[i++];
            continue;
        }
        const size_t end = html.find('>', i);
        if (end == std::string::npos) break;
        std::string tag = html.substr(i + 1, end - i - 1);
        for (char& c : tag) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (tag.rfind("br", 0) == 0 || tag == "/p" || tag == "/ul" || tag == "/li") out += '\n';
        else if (tag == "li") out += "• ";
        i = end + 1;
    }
    for (const auto& [entity, text] : { std::pair { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&nbsp;", " " } })
        for (size_t at; (at = out.find(entity)) != std::string::npos;) out.replace(at, std::string(entity).size(), text);
    return out;
}

std::string hexOf(const cv::Scalar& bgr) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", int(bgr[2]), int(bgr[1]), int(bgr[0]));
    return buf;
}

} // namespace

// The stages: Enabled, Name, Stage (OpenPnP's StagesTableModel), dragged to reorder.
class JPPipelinePanel::Model : public JPTableModel {
public:
    explicit Model(JPPipeline& p) : m_p(p) {}
    int    columnCount() const override { return 3; }
    Column column(int c) const override {
        static const char* const names[] = { "Enabled", "Name", "Stage" };
        Column col;
        col.name = names[c];
        col.kind = c == 0 ? Kind::Boolean : Kind::Text;
        return col;
    }
    int         rowCount() const override { return int(m_p.stages().size()); }
    std::string text(int row, int c) const override {
        const JPPipelineStage& s = m_p.stages()[size_t(row)];
        return c == 1 ? s.name() : c == 2 ? s.typeName() : std::string();
    }
    bool checked(int row, int) const override { return m_p.stages()[size_t(row)].enabled(); }
    bool editable(int, int c) const override { return c == 0 || c == 1; }
    void setChecked(int row, int, bool on) override {
        m_p.stages()[size_t(row)].setEnabled(on);
        if (changed) changed();
    }
    bool setText(int row, int, const std::string& t, std::string&) override {
        m_p.stages()[size_t(row)].setName(t);
        if (changed) changed();
        return true;
    }
    bool reorderable() const override { return true; }
    void reorder(int from, int to) override {
        auto& stages = m_p.stages();
        JPPipelineStage moved = stages[size_t(from)];
        stages.erase(stages.begin() + from);
        if (from < to) --to;
        stages.insert(stages.begin() + to, std::move(moved));
        if (changed) changed();
    }

    std::function<void()> changed;

private:
    JPPipeline& m_p;
};

JPPipelinePanel::JPPipelinePanel(JSceneGraph& graph, JPPipeline& pipeline)
    : JContainer(graph, 0.f, 0.f), m_pipeline(pipeline), m_model(std::make_unique<Model>(pipeline)) {
    JPUiParts::asPanel(*this);
    const JStyle& st = JStyle::current();
    m_model->changed = [this] {
        if (onChanged) onChanged();
    };

    // The tools.
    auto bar = JPUiParts::row(graph);
    auto tool = [&](const char* name, const char* icon, const char* tip) {
        return bar->add(std::make_unique<JPIconButton>(graph, name, icon, tip));
    };
    tool("Update picture from current view.", "refresh", "Update picture from current view.")->onClicked.connect([this] {
        if (onChanged) onChanged();
    });
    JPIconButton* add = tool("New stage...", "general-add", "Create a new stage.");
    add->setLeads(JPIconButton::Leads::Elsewhere);
    add->onClicked.connect([this] { newStage(); });
    tool("Delete Stage...", "general-remove", "Delete the selected stage.")->onClicked.connect([this] { deleteStage(); });
    bar->add(std::make_unique<JSeparator>(graph, JSeparator::JOrientation::Vertical, JPIconButton::size()));
    tool("Copy pipeline to clipboard", "copy", "Copy the pipeline to the clipboard in text format.")->onClicked.connect([this] {
        copyPipeline();
    });
    tool("Create pipeline from clipboard", "paste", "Create a new pipeline from a definition on the clipboard.")
        ->onClicked.connect([this] { pastePipeline(); });
    bar->setFixedSize(0.f, JPIconButton::size());
    JContainer::add(std::move(bar));

    // The stages over their description, over the chosen one's settings.
    auto column = [&] {
        auto c = std::make_unique<JContainer>(graph, 0.f, 0.f);
        c->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
        return c;
    };
    m_tablePane = column();
    m_table = m_tablePane->add(std::make_unique<JPTable>(graph));
    m_table->setModel(m_model.get());
    m_table->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_table->setColumnWidth(0, 4 * st.controlHeight);
    m_table->onSelectionChanged.connect([this] { selectionChanged(); });
    m_descriptionPane = column();
    m_description = m_descriptionPane->add(std::make_unique<JPTextView>(graph));
    m_description->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_stagesPane = column();
    m_stagesSplit = m_stagesPane->add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_stagesSplit->setHostsPanes(true);
    m_stagesSplit->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_stagesSplit->addPane(m_tablePane.get(), kStagesSplit);
    m_stagesSplit->addPane(m_descriptionPane.get(), 1 - kStagesSplit);
    m_propertiesPane = column();
    m_properties = m_propertiesPane->add(std::make_unique<JPSetupForm>(graph));
    m_properties->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split = JContainer::add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_stagesPane.get(), kMainSplit);
    m_split->addPane(m_propertiesPane.get(), 1 - kMainSplit);

    refresh(true);
}

JPPipelinePanel::~JPPipelinePanel() = default;

int JPPipelinePanel::selectedStage() const {
    const int r = m_table->selectedRow();
    return r >= 0 && r < int(m_pipeline.stages().size()) ? r : -1;
}

void JPPipelinePanel::refresh(bool last) {
    m_table->refresh();
    if (last && !m_pipeline.stages().empty()) m_table->selectRow(int(m_pipeline.stages().size()) - 1);
    selectionChanged();
}

void JPPipelinePanel::selectionChanged() {
    if (onStageSelected) onStageSelected(selectedStage());
    refreshDescription();
    refreshProperties();
}

void JPPipelinePanel::refreshDescription() {
    const int i = selectedStage();
    const JPStageType* type = i < 0 ? nullptr : JPStageRegistry::instance().find(m_pipeline.stages()[size_t(i)].className());
    m_description->setText(type ? plain(type->description) : std::string());
}

void JPPipelinePanel::refreshProperties() {
    JPSetupProperties::Form form;
    const int i = selectedStage();
    const JPStageType* type = i < 0 ? nullptr : JPStageRegistry::instance().find(m_pipeline.stages()[size_t(i)].className());
    if (type) {
        JPFormBuilder add(form);
        add.tab(type->typeName());
        add.group("");
        const std::string name = m_pipeline.stages()[size_t(i)].name();
        const auto overrides = m_pipeline.overrides(name);
        // The stage by its place, looked up each time (the stages may move).
        auto stage = [this, i]() -> JPPipelineStage& { return m_pipeline.stages()[size_t(i)]; };
        auto changed = [this](const std::string& attribute) {
            if (onChanged) onChanged();
            // Controlled or not by the caller now: every setting shown again.
            if (attribute == kControlProperty) refreshProperties();
        };
        for (const JPStageType::Property& p : type->properties) {
            using Kind = JPStageType::Kind;
            const std::string label = javaName(p.attribute);
            const std::string id = "stage:" + p.attribute;
            std::string tip = plain(p.description);
            if (const auto o = overrides.find(p.attribute); o != overrides.end()) {
                // Shown, not edited: the caller sets it.
                add.text(id, label, [v = o->second] { return v; }, nullptr);
                add.tip("Controlled by pipeline caller: " + label + "=" + o->second + "\n\n" + tip);
                continue;
            }
            const std::string a = p.attribute;
            switch (p.kind) {
                case Kind::Integer:
                    add.integer(id, label, [stage, a] { return stage().integer(a); },
                                [stage, a, changed](int v) {
                                    if (v == stage().integer(a)) return;   // the same: not a change
                                    stage().set(a, std::to_string(v));
                                    changed(a);
                                },
                                INT_MIN, INT_MAX);
                    break;
                case Kind::Number:
                    add.number(id, label, [stage, a] { return stage().number(a); },
                               [stage, a, changed](double v) {
                                   if (v == stage().number(a)) return;   // the same: not a change
                                   stage().set(a, JPXmlWriter::number(v));
                                   changed(a);
                               },
                               6);
                    break;
                case Kind::Flag:
                    add.flag(id, label, [stage, a] { return stage().flag(a); }, [stage, a, changed](bool on) {
                        if (on == stage().flag(a)) return;   // the same: not a change
                        stage().set(a, on ? "true" : "false");
                        changed(a);
                    });
                    break;
                case Kind::Choice:
                    add.choice(id, label, p.choices, [stage, a] { return stage().text(a); }, [stage, a, changed](const std::string& v) {
                        if (v == stage().text(a)) return;   // the same: not a change
                        stage().set(a, v);
                        changed(a);
                    });
                    break;
                case Kind::Color:
                    add.color(id, label, [stage, a] { return hexOf(stage().color(a)); }, [stage, a, changed](const std::string& hex) {
                        unsigned r = 0, g = 0, b = 0;
                        if (std::sscanf(hex.c_str(), "#%02x%02x%02x", &r, &g, &b) != 3) return;
                        if (hex == hexOf(stage().color(a))) return;   // the same: not a change
                        // Its transparency kept.
                        stage().setColor(a, int(r), int(g), int(b), int(stage().color(a)[3]));
                        changed(a);
                    });
                    break;
                case Kind::Text:
                case Kind::StageName:
                    add.text(id, label, [stage, a] { return stage().text(a); }, [stage, a, changed](const std::string& v) {
                        if (v == stage().text(a)) return;   // the same: not a change
                        stage().set(a, v);
                        changed(a);
                    });
                    break;
            }
            if (!tip.empty()) add.tip(tip);
        }
    }
    m_properties->setForm(std::move(form));
}

void JPPipelinePanel::newStage() {
    if (!chooseClass) return;
    // Every stage the editor knows, by name.
    std::vector<std::string> classes;
    for (const JPStageType& t : JPStageRegistry::instance().types()) classes.push_back(t.className);
    std::sort(classes.begin(), classes.end(), [](const std::string& a, const std::string& b) {
        auto simple = [](const std::string& c) {
            std::string s = c.substr(c.rfind('.') + 1);
            for (char& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            return s;
        };
        return simple(a) < simple(b);
    });
    chooseClass("New stage", "Please select a stage implemention from the list below.", std::move(classes),
                [this](const std::string& className) {
                    m_pipeline.stages().push_back(JPPipelineStage::create(className, m_pipeline.uniqueName()));
                    refresh(true);
                    if (onChanged) onChanged();
                });
}

void JPPipelinePanel::deleteStage() {
    const int i = selectedStage();
    if (i < 0) return;
    m_pipeline.stages().erase(m_pipeline.stages().begin() + i);
    m_table->clearSelection();
    refresh(false);
    if (onChanged) onChanged();
}

void JPPipelinePanel::copyPipeline() { JWidget::clipboardSet(JPXmlWriter::text(m_pipeline.toXml())); }

void JPPipelinePanel::pastePipeline() {
    JPXmlElement root;
    std::string error;
    if (!JPXmlReader::parse(JWidget::clipboardGet(), root, error) || root.name != "cv-pipeline") {
        if (showError) showError("Paste failed", error.empty() ? "The clipboard holds no pipeline." : error);
        return;
    }
    m_pipeline.stages() = JPPipeline::fromXml(root).stages();
    refresh(true);
    if (onChanged) onChanged();
}

} // inline namespace jf
