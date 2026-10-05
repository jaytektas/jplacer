// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPackagesPanel.h"

#include "JPVisionPipelineActions.h"
#include "setup/JPVisionForms.h"
#include "setup/JPFormBuilder.h"
#include "JPSetupForm.h"

#include "JPFieldGrid.h"
#include "JPGroupFrame.h"
#include "JPUiParts.h"

#include "model/JPKicadModImporter.h"
#include "model/JPLengthUnits.h"
#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"

#include <j/core/Dialog.h>
#include <j/core/FrameTimer.h>
#include <j/core/JButton.h>
#include <j/core/JLabel.h>
#include <j/core/JSeparator.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <cstdio>
#include <cstdlib>

inline namespace jf {

namespace {

constexpr int kSearchColumns = 15;
constexpr const char* kLengthFormat = "%.3f";   // OpenPnP's length display format
constexpr const char* kLevelFormat = "%.1f";    // its vacuum levels'

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string format(const char* f, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

bool toDouble(const std::string& t, double& out) {
    const std::string s = trimmed(t);
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return !s.empty() && end == s.c_str() + s.size();
}

bool toInt(const std::string& t, int& out) {
    const std::string s = trimmed(t);
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    out = int(v);
    return !s.empty() && end == s.c_str() + s.size();
}

const std::vector<JPLengthUnit>& units() {
    static const std::vector<JPLengthUnit> u = { JPLengthUnit::Meters, JPLengthUnit::Centimeters, JPLengthUnit::Millimeters,
                                                 JPLengthUnit::Feet, JPLengthUnit::Inches, JPLengthUnit::Mils,
                                                 JPLengthUnit::Microns };
    return u;
}

std::unique_ptr<JSeparator> toolSeparator(JSceneGraph& graph) {
    return std::make_unique<JSeparator>(graph, JSeparator::JOrientation::Vertical, JPIconButton::size());
}

} // namespace

JPPackagesPanel::JPPackagesPanel(JSceneGraph& graph, JPConfiguration& config, double split)
    : JContainer(graph), m_config(config), m_model(config) {
    JPUiParts::asPanel(*this);
    const JStyle& st = JStyle::current();
    m_model.onChanged = [this] { changed(); };
    m_padsModel.onChanged = [this] { changed(); };
    m_tipsModel.onChanged = [this] { changed(); };

    auto bar = JPUiParts::row(graph);
    auto tool = [&](const char* name, const char* icon, const char* tip) {
        return bar->add(std::make_unique<JPIconButton>(graph, name, icon, tip));
    };
    JPIconButton* newOne = tool("New Package...", "general-add", "Create a new package, specifying it's ID.");
    newOne->setLeads(JPIconButton::Leads::Elsewhere);
    newOne->onClicked.connect([this] { newPackage(); });
    m_delete = tool("Delete Package", "general-remove", "Delete the currently selected package.");
    m_delete->onClicked.connect([this] { deletePackages(); });
    bar->add(toolSeparator(graph));
    m_copy = tool("Copy Package to Clipboard", "copy", "Copy the currently selected package to the clipboard in text format.");
    m_copy->onClicked.connect([this] { copyPackage(); });
    tool("Create Package from Clipboard", "paste", "Create a new package from a definition on the clipboard.")
        ->onClicked.connect([this] { pastePackage(); });
    bar->add(std::make_unique<JContainer>(graph, 0.f, 0.f))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    JLabel* searchLabel = bar->add(std::make_unique<JLabel>(graph, "Search"));
    searchLabel->setFixedSize(JTextHelper::measureWidth("Search") + st.spacing, st.controlHeight);
    m_search = bar->add(std::make_unique<JLineEdit>(graph, ""));
    m_search->setFixedSize(JTextHelper::measureWidth("M") * kSearchColumns, st.controlHeight);
    m_search->onTextChanged.connect([this](const std::string& t) { m_table->setFilter(t); });
    add(std::move(bar));

    m_tablePane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_tabsPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    for (JContainer* p : { m_tablePane.get(), m_tabsPane.get() })
        p->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_table = m_tablePane->add(std::make_unique<JPTable>(graph));
    m_table->setModel(&m_model);
    m_table->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_table->onSelectionChanged.connect([this] { updateWizards(); });
    m_tabs = m_tabsPane->add(std::make_unique<JTabWidget>(graph, 0.f, 0.f));
    m_tabs->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_tablePane.get(), float(split));
    m_split->addPane(m_tabsPane.get(), float(1 - split));
    updateWizards(true);
    onVisibilityChanged.connect([this](bool) { showFootprint(); });
}

void JPPackagesPanel::showFootprint() {
    if (!onShowFootprint) return;
    const JPPackage* p = isVisible() ? selectedPackage() : nullptr;
    onShowFootprint(p ? &p->footprint : nullptr);
}

double JPPackagesPanel::split() const {
    const std::vector<float> f = m_split->fractions();
    return f.empty() ? 0.5 : f.front();
}

void JPPackagesPanel::refresh() {
    m_table->refresh();
    updateWizards(true);
}

std::vector<JPPackage*> JPPackagesPanel::selections() const {
    std::vector<JPPackage*> out;
    for (const int r : m_table->selectedRows())
        if (JPPackage* p = m_model.package(r)) out.push_back(p);
    return out;
}

const JPPackage* JPPackagesPanel::selectedPackage() const {
    const auto s = selections();
    return s.size() == 1 ? s.front() : nullptr;
}

void JPPackagesPanel::selectPackage(const JPPackage* p) {
    if (selectedPackage() != p) m_table->selectRow(m_model.rowOf(p));
}

void JPPackagesPanel::changed() {
    showFootprint();
    if (m_computeComposite) m_computeComposite();
    if (onChanged) onChanged();
}

void JPPackagesPanel::updateWizards(bool force) {
    const auto chosen = selections();
    m_delete->setEnabled(!chosen.empty());
    m_copy->setEnabled(chosen.size() == 1);
    showFootprint();
    JPPackage* p = chosen.size() == 1 ? chosen.front() : nullptr;
    const std::string id = p ? p->id : std::string();
    if (!force && id == m_shown && (p != nullptr) == !m_pages.empty()) return;
    if (m_tabs->tabCount() > 0) m_lastTab = m_tabs->activeTab();
    while (m_tabs->tabCount() > 0) m_tabs->removeTab(0);
    m_computeComposite = nullptr;   // its tab goes with the pages
    m_pages.clear();
    m_pads = nullptr;
    m_padDelete = m_padMark = nullptr;
    m_padsModel.setFootprint(nullptr);
    m_tipsModel.setPackage(nullptr);
    m_shown = id;
    if (!p) return;
    m_pages.push_back(nozzleTipsTab(*p));
    m_tabs->addTab("Nozzle Tips", m_pages.back().get());
    m_pages.push_back(settingsTab(*p));
    m_tabs->addTab("Settings", m_pages.back().get());
    m_pages.push_back(footprintTab(*p));
    m_tabs->addTab("Footprint", m_pages.back().get());
    m_pages.push_back(compositingTab(*p));
    m_tabs->addTab("Vision Compositing", m_pages.back().get());
    // As OpenPnP's: the bottom vision's and the fiducial locator's pages for the package.
    if (auto page = visionTab(*p, JPVisionSettings::Kind::Bottom)) {
        m_pages.push_back(std::move(page));
        m_tabs->addTab("Bottom Vision Settings", m_pages.back().get());
    }
    if (auto page = visionTab(*p, JPVisionSettings::Kind::Fiducial)) {
        m_pages.push_back(std::move(page));
        m_tabs->addTab("Fiducial Vision Settings", m_pages.back().get());
    }
    if (m_lastTab >= 0 && m_lastTab < m_tabs->tabCount()) m_tabs->setActiveTab(m_lastTab);
}

std::unique_ptr<JContainer> JPPackagesPanel::visionTab(JPPackage& p, JPVisionSettings::Kind kind) {
    const bool bottom = kind == JPVisionSettings::Kind::Bottom;
    const auto defaults = machineDefaults ? machineDefaults() : std::pair<std::string, std::string> {};
    const std::string machineId = bottom ? defaults.first : defaults.second;
    const JPVisionSettings* v = m_config.visionSettings(bottom ? p.bottomVisionId : p.fiducialVisionId);
    if (!v) v = m_config.visionSettings(machineId);
    if (!v) return nullptr;
    std::string used;
    for (const std::string& u : m_config.visionUsedIn(*v, machineId, bottom ? "Bottom Vision" : "Fiducal Locator"))
        used += (used.empty() ? "" : ", ") + u;
    JPSetupProperties::Form form;
    JPFormBuilder add(form);
    JPVisionForms::addPage(add, m_config, v->id, used, { JPVisionForms::Holder::Kind::Package, p.id }, tests());
    auto page = std::make_unique<JContainer>(m_graph, 0.f, 0.f);
    page->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    page->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    JPSetupForm* f = page->add(std::make_unique<JPSetupForm>(m_graph));
    f->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    f->setSingleTabBar(false);
    f->setForm(std::move(form));
    f->onChanged = [this](const std::string& property) {
        if (property.find(":parameter:") != std::string::npos) {
            if (onParameterChanged) onParameterChanged();
            visionAct(property);
            return;
        }
        m_table->refresh();
        changed();
    };
    f->onAction = [this](const std::string& action) { visionAct(action); };
    return page;
}

bool JPPackagesPanel::pipelineAct(const std::string& settingsId, const JPVisionForms::Holder& holder, const std::string& what) {
    JPVisionPipelineActions::Hooks hooks;
    hooks.edit = [this, holder](const std::string& id) {
        if (editPipeline) editPipeline(id, holder);
    };
    hooks.preview = [this, holder](const std::string& id, const std::string& parameter) {
        if (previewParameter) previewParameter(id, holder, parameter);
    };
    hooks.machineDefault = [this](JPVisionSettings::Kind kind) {
        const auto defaults = machineDefaults ? machineDefaults() : std::pair<std::string, std::string> {};
        return m_config.visionSettings(kind == JPVisionSettings::Kind::Bottom ? defaults.first : defaults.second);
    };
    hooks.test = [this, holder](const std::string& id, const std::string& test) {
        if (visionTest) visionTest(id, holder, test);
    };
    hooks.changed = [this] {
        // Its sliders follow the pipeline.
        updateWizards(true);
        m_table->refresh();
        changed();
    };
    return JPVisionPipelineActions::act(m_config, settingsId, what, hooks);
}

void JPPackagesPanel::visionAct(const std::string& action) {
    const auto chosen = selections();
    if (chosen.size() != 1) return;
    JPPackage* p = chosen.front();
    const size_t colon = action.find(':');
    if (colon == std::string::npos) return;
    const bool bottom = action.substr(0, colon) == "bottom";
    const std::string what = action.substr(colon + 1);
    const auto defaults = machineDefaults ? machineDefaults() : std::pair<std::string, std::string> {};
    const JPVisionSettings* v = m_config.visionSettings(bottom ? p->bottomVisionId : p->fiducialVisionId);
    if (!v) v = m_config.visionSettings(bottom ? defaults.first : defaults.second);
    if (!v) return;
    const std::string id = v->id;
    const JPVisionForms::Holder holder { JPVisionForms::Holder::Kind::Package, p->id };
    if (pipelineAct(id, holder, what)) return;
    auto run = [this, id, what, holder] {
        std::string why;
        if (JPVisionForms::act(m_config, id, what, holder, why)) {
            updateWizards(true);
            m_table->refresh();
            changed();
        } else if (!why.empty()) {
            JDialog::message("Error", why);
        }
    };
    JDialogOptions opts;
    opts.okLabel = "Yes";
    opts.cancelLabel = "No";
    if (what == "reset") {
        JDialog::confirm("Reset to Default", std::string("This will reset the ") + (bottom ? "bottom" : "fiducial") +
                                                 " vision settings with to the default settings. Are you sure??",
                         run, nullptr, opts);
        return;
    }
    if (what == "generalize") {
        // As OpenPnP: what goes, said first; nothing to take away, said so.
        const auto list = JPVisionForms::specializedIn(m_config, holder,
                                                       bottom ? JPVisionSettings::Kind::Bottom : JPVisionSettings::Kind::Fiducial);
        if (list.empty()) {
            JDialog::message("Error", "There are no specializations on Parts with the Package " + p->id + ".");
            return;
        }
        std::string names;
        for (const std::string& n : list) names += (names.empty() ? "" : ", ") + n;
        JDialog::confirm("Generalize", "This will remove the specialized vision settings in:\n\n" + names + "\n\nAre you sure?",
                         run, nullptr, opts);
        return;
    }
    // Not while the button clicked is still in its page: the page is made again.
    jPostToNextFrame(run);
}

std::unique_ptr<JContainer> JPPackagesPanel::nozzleTipsTab(JPPackage& p) {
    JSceneGraph& g = m_graph;
    auto page = std::make_unique<JContainer>(g, 0.f, 0.f);
    JPUiParts::asPanel(*page);
    m_tipsModel.setTips(nozzleTips ? nozzleTips() : std::vector<std::pair<std::string, std::string>> {});
    m_tipsModel.setPackage(&p);
    JPTable* t = page->add(std::make_unique<JPTable>(g));
    t->setModel(&m_tipsModel);
    return page;
}

std::unique_ptr<JContainer> JPPackagesPanel::settingsTab(JPPackage& p) {
    JSceneGraph& g = m_graph;
    auto page = std::make_unique<JContainer>(g, 0.f, 0.f);
    JPUiParts::asPanel(*page);
    auto grid = std::make_unique<JPFieldGrid>(g, 1);
    const std::string id = p.id;
    grid->text("Vacuum Level", "", format(kLevelFormat, p.pickVacuumLevel), [this, id](const std::string& t) {
        double v;
        JPPackage* k = m_config.package(id);
        if (!k || !toDouble(t, v)) return false;
        k->pickVacuumLevel = v;
        changed();
        return true;
    });
    grid->text("Blow Off Level", "", format(kLevelFormat, p.placeBlowOffLevel), [this, id](const std::string& t) {
        double v;
        JPPackage* k = m_config.package(id);
        if (!k || !toDouble(t, v)) return false;
        k->placeBlowOffLevel = v;
        changed();
        return true;
    });
    page->add(JPFieldGrid::grouped(g, "Vacuum & Blow Off", std::move(grid)));
    return page;
}

std::unique_ptr<JContainer> JPPackagesPanel::footprintTab(JPPackage& p) {
    JSceneGraph& g = m_graph;
    auto page = std::make_unique<JContainer>(g, 0.f, 0.f);
    JPUiParts::asPanel(*page);
    JPFootprint& f = p.footprint;
    const std::string id = p.id;
    // A length field of the footprint's, "%.3f".
    auto number = [this, id](JPFieldGrid& grid, const char* label, const char* tip, double JPFootprint::*field) {
        JPPackage* k = m_config.package(id);
        grid.text(label, tip, format(kLengthFormat, k->footprint.*field), [this, id, field](const std::string& t) {
            double v;
            JPPackage* k = m_config.package(id);
            if (!k || !toDouble(t, v)) return false;
            k->footprint.*field = v;
            changed();
            return true;
        });
    };

    // Settings: units, the generators, the body and the generators' numbers.
    auto grid = std::make_unique<JPFieldGrid>(g, 3);
    std::vector<std::string> unitNames;
    int now = 0;
    for (size_t i = 0; i < units().size(); ++i) {
        unitNames.push_back(JPLengthUnits::name(units()[i]));
        if (units()[i] == f.units) now = int(i);
    }
    grid->choice("Units", "", unitNames, now, [this, id](int i) {
        if (JPPackage* k = m_config.package(id)) {
            k->footprint.units = units()[size_t(i)];
            m_pads->refresh();
            changed();
        }
    });
    auto gens = JPUiParts::row(g);
    for (const auto& [icon, tip, type] :
         { std::tuple { "footprint-dual", "Generate a Dual form factor package.", JPFootprint::Generator::Dual },
           std::tuple { "footprint-quad", "Generate a square Quad form factor package.", JPFootprint::Generator::Quad },
           std::tuple { "footprint-bga", "Generate a square BGA package.", JPFootprint::Generator::Bga },
           std::tuple { "kicad-logo", "Import a footprint from KiCad module.", JPFootprint::Generator::Kicad } }) {
        auto b = std::make_unique<JPIconButton>(g, tip, icon, tip);
        b->setFramed(true);
        // The KiCad one asks for a file; the others make the pads at once.
        if (type == JPFootprint::Generator::Kicad) b->setLeads(JPIconButton::Leads::Elsewhere);
        const JPFootprint::Generator t = type;
        b->onClicked.connect([this, t] { generatePads(t); });
        gens->add(std::move(b));
    }
    grid->widget("Generate", std::move(gens));
    grid->skip();
    number(*grid, "Body Width", "", &JPFootprint::bodyWidth);
    number(*grid, "Outside dimension",
           "Overall outside dimension of the footprint\nFor Dual this is the width, for Quad width and height (it is "
           "calculated for BGE).",
           &JPFootprint::outerDimension);
    // Inside dimension shows as Java shows a double (OpenPnP binds it without a format).
    grid->text("Inside dimension", "The inner dimension between the pads. For BGA this the area where balls are left out.",
               JPXmlWriter::number(f.innerDimension), [this, id](const std::string& t) {
                   double v;
                   JPPackage* k = m_config.package(id);
                   if (!k || !toDouble(t, v)) return false;
                   k->footprint.innerDimension = v;
                   changed();
                   return true;
               });
    number(*grid, "Body Length", "", &JPFootprint::bodyHeight);
    grid->text("Pad count",
               "Overall count of the pads.\n - Dual: Multiples of 2.\n - Quad: Multiples of 4.\n - BGA: Square number "
               "(including those left out on the inside).",
               std::to_string(f.padCount), [this, id](const std::string& t) {
                   int v;
                   JPPackage* k = m_config.package(id);
                   if (!k || !toInt(t, v)) return false;
                   k->footprint.padCount = v;
                   changed();
                   return true;
               });
    number(*grid, "Pad pitch", "", &JPFootprint::padPitch);
    grid->skip();
    number(*grid, "Pad Across", "The size of pads, in the direction of the pitch.", &JPFootprint::padAcross);
    number(*grid, "% Roundness",
           "Roundness of the pad in percent of its smaller dimension. If negative, only the inner side will be rounded.",
           &JPFootprint::padRoundness);
    page->add(JPFieldGrid::grouped(g, "Settings", std::move(grid)));

    // Pads: a toolbar and the table.
    auto padsFrame = std::make_unique<JPGroupFrame>(g, "Pads");
    padsFrame->setAlignItems(JAlignItems::Stretch);
    padsFrame->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    auto bar = JPUiParts::row(g);
    JPIconButton* newPad =
        bar->add(std::make_unique<JPIconButton>(g, "New Pad...", "general-add", "Create a new pad, specifying it's ID."));
    newPad->setLeads(JPIconButton::Leads::Elsewhere);
    newPad->onClicked.connect([this, id] {
            JDialog::input("New Pad", "Please enter a name for the new pad.", [this, id](std::string text) {
                const std::string name = trimmed(text);
                JPPackage* k = m_config.package(id);
                if (name.empty() || !k) return;
                JPFootprint::Pad pad;
                pad.name = name;
                k->footprint.pads.push_back(pad);
                m_pads->refresh();
                m_pads->selectRow(int(k->footprint.pads.size()) - 1);
                changed();
            });
        });
    m_padDelete = bar->add(std::make_unique<JPIconButton>(g, "Delete Pad", "general-remove", "Delete the currently selected pad."));
    m_padDelete->onClicked.connect([this, id] {
        JPPackage* k = m_config.package(id);
        const int r = m_pads->selectedRow();
        if (!k || r < 0 || size_t(r) >= k->footprint.pads.size()) return;
        const std::string name = k->footprint.pads[size_t(r)].name;
        JDialogOptions opts;
        opts.okLabel = "Yes";
        opts.cancelLabel = "No";
        JDialog::confirm("Delete " + name + "?", "Are you sure you want to delete " + name + "?", [this, id, r] {
            JPPackage* k = m_config.package(id);
            if (!k || size_t(r) >= k->footprint.pads.size()) return;
            k->footprint.pads.erase(k->footprint.pads.begin() + r);
            m_pads->clearSelection();
            m_pads->refresh();
            changed();
        }, nullptr, opts);
    });
    m_padMark = bar->add(std::make_unique<JPIconButton>(
        g, "Toggle pad mark", "footprint-mark",
        "Toggle mark for the currently selected pad, i.e. the indicate pin #1 or cathode."));
    m_padMark->onClicked.connect([this, id] {
        JPPackage* k = m_config.package(id);
        const int r = m_pads->selectedRow();
        if (!k || r < 0) return;
        k->footprint.toggleMark(size_t(r));
        m_pads->refresh();
        changed();
    });
    m_padDelete->setEnabled(false);
    m_padMark->setEnabled(false);
    padsFrame->add(std::move(bar));
    m_padsModel.setFootprint(&f);
    m_pads = padsFrame->add(std::make_unique<JPTable>(g));
    m_pads->setModel(&m_padsModel);
    m_pads->onSelectionChanged.connect([this] {
        const bool one = m_pads->selectedRow() >= 0;
        m_padDelete->setEnabled(one);
        m_padMark->setEnabled(one);
    });
    page->add(std::move(padsFrame));
    return page;
}

void JPPackagesPanel::generatePads(JPFootprint::Generator type) {
    const std::string id = m_shown;
    auto run = [this, id, type](std::vector<JPFootprint::Pad> imported) {
        JPPackage* k = m_config.package(id);
        if (!k) return;
        k->footprint.pads.clear();
        std::string error;
        if (type == JPFootprint::Generator::Kicad) k->footprint.pads = std::move(imported);
        else if (!k->footprint.generate(type, error)) JDialog::message("Error", error);
        refresh();
        changed();
    };
    // The pads there are go first, once asked; KiCad's come from a file chosen.
    auto start = [run, type] {
        if (type != JPFootprint::Generator::Kicad) {
            run({});
            return;
        }
        JDialog::openFile("Import KiCad Footprint", { "kicad_mod" }, [run](std::string path) {
            std::vector<JPFootprint::Pad> pads;
            std::string error;
            if (!JPKicadModImporter::read(path, pads, error)) {
                JDialog::message("Error", error);
                return;
            }
            run(std::move(pads));
        });
    };
    const JPPackage* k = m_config.package(id);
    if (k && !k->footprint.pads.empty()) {
        JDialogOptions opts;
        opts.okLabel = "Yes";
        opts.cancelLabel = "No";
        JDialog::confirm("Delete all pads?", "Are you sure you want to delete all existing pads?", start, nullptr, opts);
    } else {
        start();
    }
}

std::unique_ptr<JContainer> JPPackagesPanel::compositingTab(JPPackage& p) {
    JSceneGraph& g = m_graph;
    auto page = std::make_unique<JContainer>(g, 0.f, 0.f);
    JPUiParts::asPanel(*page);
    if (!p.visionCompositing) p.visionCompositing = JPVisionCompositing();   // as OpenPnP makes it when shown
    const JPVisionCompositing& v = *p.visionCompositing;
    const std::string id = p.id;
    auto vc = [this, id]() -> JPVisionCompositing* {
        JPPackage* k = m_config.package(id);
        return k && k->visionCompositing ? &*k->visionCompositing : nullptr;
    };
    auto grid = std::make_unique<JPFieldGrid>(g, 2);
    using M = JPVisionCompositing::Method;
    const std::vector<M> methods = { M::None, M::Restricted, M::Body, M::Automatic, M::SingleCorners };
    std::vector<std::string> names;
    int now = 0;
    for (size_t i = 0; i < methods.size(); ++i) {
        names.push_back(JPVisionCompositing::methodName(methods[i]));
        if (methods[i] == v.compositingMethod) now = int(i);
    }
    grid->choice("Method",
                 "Compositing method:\n - None: no compositing is performed.\n - Restricted: compositing is only "
                 "performed, when the footprint size is too large, or if it is asymmetric.\n - Automatic: compositing is "
                 "always performed. It can still result in a one-shot solution, if the corners are all convex and "
                 "symmetric.\n - SingleCorner: compositing is always performed. Each corner is individually shot. "
                 "Reduces parallax errors, as each corner is centered in the camera.",
                 names, now, [this, vc, methods](int i) {
                     if (JPVisionCompositing* c = vc()) {
                         c->compositingMethod = methods[size_t(i)];
                         changed();
                     }
                 });
    grid->skip();
    grid->text("Extra Shots",
               "Vision Compositing determines the optimal minimum set of required shot to determine the part position "
               "and angle. On top of that, it suggests extra shots, that can be used to improve positional and angular "
               "accuracy.\nExtra Shots indicates how man of these extra shots should be used.",
               std::to_string(v.extraShots), [this, vc](const std::string& t) {
                   int n;
                   JPVisionCompositing* c = vc();
                   if (!c || !toInt(t, n)) return false;
                   c->extraShots = n;
                   changed();
                   return true;
               });
    grid->text("Max. Pick Tolerance",
               "If zero, the Max. Pick Tolerance on the nozzle tip is taken.\nIf non-zero, this Max. Pick Tolerance "
               "overrides it for this package.",
               v.maxPickTolerance.text("%.3f%s"), [this, vc](const std::string& t) {
                   JPVisionCompositing* c = vc();
                   const auto l = JPLength::parseWithDefaultUnits(t, JPLengthUnit::Millimeters);
                   if (!c || !l) return false;
                   c->maxPickTolerance = *l;
                   changed();
                   return true;
               });
    grid->text("Min. Angle Leverage",
               "Minimum leverage required to derive angular information from two corners, relative to the footprint "
               "width and height (the lesser of the two).",
               format(kLengthFormat, v.minLeverageFactor), [this, vc](const std::string& t) {
                   double d;
                   JPVisionCompositing* c = vc();
                   if (!c || !toDouble(t, d)) return false;
                   c->minLeverageFactor = d;
                   changed();
                   return true;
               });
    grid->tick("Allow inside corner?",
               "Allows the inclusion of corners facing inside, towards the center of the part. This can be used to "
               "align parts that are still too large, i.e., where the Roaming Radius of the camera is too small. It "
               "typically requires large, well-spaced pads.",
               v.allowInside, [this, vc](bool on) {
                   if (JPVisionCompositing* c = vc()) {
                       c->allowInside = on;
                       changed();
                   }
               });
    // Compute and its result, and the preview under them.
    auto compute = std::make_unique<JButton>(g, "Compute", 0.f);
    compute->setTooltip("Compute the vision compositing, and preview the result.");
    JButton* computeButton = grid->widget("", std::move(compute));
    auto solution = std::make_unique<JPTextField>(g);
    solution->setEnabled(false);
    JPTextField* status = grid->widget("", std::move(solution));
    page->add(JPFieldGrid::grouped(g, "Compositing", std::move(grid)));
    auto preview = std::make_unique<JPCompositingPreview>(g);
    preview->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    JPCompositingPreview* shown = page->add(std::move(preview));
    // As OpenPnP's PackageCompositingWizard: computed when shown, and on Compute.
    auto run = [this, id, status, shown] {
        const JPPackage* k = m_config.package(id);
        CompositePreview made;
        std::string why;
        if (!k || !computeComposite || !computeComposite(*k, made, why)) {
            shown->clear();
            status->setText("Error: " + (why.empty() ? std::string("no machine to compute it with") : why));
            return;
        }
        shown->setComposite(made.composite, made.footprintMm, made.cameraWidthMm, made.cameraHeightMm, made.roamingRadiusMm);
        const JPVisionComposite& c = *made.composite;
        if (JPVisionComposite::isInvalid(c.solution())) {
            status->setText(std::string("Error: ") + JPVisionComposite::solutionName(c.solution()) + " | " + c.diagnostics());
            return;
        }
        size_t minShots = 0;
        for (const auto& s : c.shots())
            if (!s.optional) ++minShots;
        char text[160];
        std::snprintf(text, sizeof text, "Solution: %s | Min. shots: %zu | Max. shots: %zu | Computation: %.2fms",
                      JPVisionComposite::solutionName(c.solution()), minShots, c.shots().size(), c.computeSeconds() * 1000);
        status->setText(text);
    };
    computeButton->onClicked.connect(run);
    m_computeComposite = run;
    run();
    return page;
}

void JPPackagesPanel::newPackage() {
    JDialog::input("New Package", "Please enter an ID for the new package.", [this](std::string text) {
        const std::string id = trimmed(text);
        if (id.empty()) return;
        if (m_config.package(id)) {
            JDialog::message("Error", "Package ID " + id + " already exists.");
            newPackage();
            return;
        }
        auto k = std::make_shared<JPPackage>();
        k->id = id;
        m_config.addPackage(k);
        m_table->refresh();
        selectPackage(k.get());
        changed();
    });
}

void JPPackagesPanel::deletePackages() {
    const auto chosen = selections();
    if (chosen.empty()) return;
    // A package a part uses cannot go, as in OpenPnP.
    for (const JPPackage* k : chosen)
        for (const auto& part : m_config.parts())
            if (part->packageId == k->id) {
                JDialog::message("Error", k->id + " cannot be deleted. It is used by " + part->id);
                return;
            }
    std::string ids;
    for (size_t i = 0; i < chosen.size() && i < 3; ++i) ids += (i ? ", " : "") + chosen[i]->id;
    if (chosen.size() > 3) ids += ", and " + std::to_string(chosen.size() - 3) + " others";
    std::vector<std::string> doomed;
    for (const JPPackage* k : chosen) doomed.push_back(k->id);
    JDialogOptions opts;
    opts.okLabel = "Yes";
    opts.cancelLabel = "No";
    JDialog::confirm("Delete " + std::to_string(chosen.size()) + " packages?", "Are you sure you want to delete " + ids + "?",
        [this, doomed] {
            for (const std::string& id : doomed) m_config.removePackage(id);
            refresh();
            changed();
        },
        nullptr, opts);
}

void JPPackagesPanel::copyPackage() {
    const JPPackage* k = selectedPackage();
    if (!k) return;
    std::string text = JPXmlWriter::text(k->toXml());
    if (!text.empty() && text.back() == '\n') text.pop_back();
    JWidget::clipboardSet(text);
}

void JPPackagesPanel::pastePackage() {
    JDialog::input("Create Package from Clipboard", "Please enter an ID for the pasted package.", [this](std::string text) {
        const std::string id = trimmed(text);
        if (id.empty()) return;
        if (m_config.package(id)) {
            JDialog::message("Error", "Package ID " + id + " already exists.");
            pastePackage();
            return;
        }
        JPXmlElement root;
        std::string error;
        if (!JPXmlReader::parse(JWidget::clipboardGet(), root, error) || root.name != "package") {
            JDialog::message("Paste Failed", error.empty() ? std::string("The clipboard does not hold a package.") : error);
            return;
        }
        auto k = std::make_shared<JPPackage>(JPPackage::fromXml(root));
        k->id = id;
        m_config.addPackage(k);
        m_table->refresh();
        selectPackage(k.get());
        changed();
    });
}

} // inline namespace jf
