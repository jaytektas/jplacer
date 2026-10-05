// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeedersPanel.h"

#include "JPUiParts.h"

#include "camera/JPImageFile.h"
#include "common/JPlacerLog.h"
#include "setup/JPFeederForms.h"
#include "model/JPBlindsFeeders.h"
#include "model/JPPushPullTemplates.h"

#include <j/core/Dialog.h>
#include <j/core/JLabel.h>
#include <j/core/JSeparator.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/core/Log.h>
#include <j/core/FrameTimer.h>
#include <j/graphics/FontEngine.h>

#include <chrono>
#include <cstdlib>

inline namespace jf {

namespace {

// OpenPnP's search box is fifteen characters wide.
constexpr int kSearchColumns = 15;
// A selection begun afresh on the camera is this many pixels square (OpenPnP's).
constexpr int kFirstSelectionPx = 100;

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
        if (JPFeeder* f = selection()) feedOrPick(*f, false);
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
    m_table->onSelectionChanged.connect([this] {
        selectionChanged();
        if (const JPFeeder* f = selection(); f && onFeederChosen) onFeederChosen(*f);
    });
    m_table->onEditRefused = [](const std::string&) {};
    m_form = m_formPane->add(std::make_unique<JPSetupForm>(graph));
    m_form->setOpenPnpPlaceButtons(true);
    m_form->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_form->onChanged = [this](const std::string& property) {
        // A blinds feeder's pockets counted again, and its holder's settings given to the others on it.
        if (JPFeeder* f = m_config.feeder(m_shown); f && f->typeName() == "BlindsFeeder") {
            JPBlindsFeeders::recalculateGeometry(*f);
            JPBlindsFeeders::propagate(m_config, m_shown);
        }
        m_table->refresh();
        changed();
        // The pin's name decides what its place rows' buttons move; a slot's
        // bank and feeder (and their names), or a heap's drop box (and its
        // name), what its choices list.
        // A push-pull feeder's additive rotation turned on (or its actuator chosen): its rotation counted from where it is, as OpenPnP's.
        if ((property == "additive-rotation" || property == "actuator-name") && machineAction && machineReady && machineReady())
            if (const JPFeeder* f = m_config.feeder(m_shown); f && f->typeName() == "ReferencePushPullFeeder" && f->flag("additive-rotation", true))
                machineAction(m_shown, "resetRotation");
        if (property == "actuator-name" || property.rfind("slot.", 0) == 0 || property == "drop-box-id" || property == "drop-box.name"
            || property == "additive-rotation" || property == "used-as-template" || property == "part" || property == "feeder-group-name"
            || property == "pocket-pitch" || property == "cover-type")
            jPostToNextFrame([this, alive = std::weak_ptr<bool>(m_alive)] {
                if (const auto a = alive.lock(); a && *a) rebuildForm();
            });
    };
    m_form->onAction = [this](const std::string& action) {
        using Selecting = JPFeederForms::Options::Selecting;
        if (action == "selectTemplate" || action == "selectAoi") {
            selectOnCamera(action == "selectTemplate" ? Selecting::Template : Selecting::AreaOfInterest);
            return;
        }
        if (action == "cancelTemplate" || action == "cancelAoi") {
            cancelSelection();
            return;
        }
        if (action == "photonProgram") {
            if (programPhotonSlots) programPhotonSlots();
            return;
        }
        if (action == "autoSetup" || action == "autoSetupCancel") {
            if (autoSetup) autoSetup(m_shown, action);
            return;
        }
        if (action == "blindsGetToolZ") {
            // The chosen nozzle's Z as the part's.
            const Where at = whereIs ? whereIs(Tool::Nozzle) : Where {};
            JPFeeder* f = m_config.feeder(m_shown);
            if (!f || !at[2]) {
                JDialog::message("Error", "Nothing captured: the machine is not connected.");
                return;
            }
            f->setLocation(f->location().derive(std::nullopt, std::nullopt, *at[2], std::nullopt));
            rebuild();
            changed();
            return;
        }
        if (action == "blindsOcrToAll" || action == "blindsPipelineToAll") {
            const std::string id = m_shown;
            JDialog::confirm("Warning",
                             action == "blindsOcrToAll"
                                 ? "This will replace the OCR settings of all the other BlindsFeeders on the machine with those of this BlindsFeeder. Are you sure?"
                                 : "This will replace the pipeline of all the other BlindsFeeders on the machine with the pipeline of this BlindsFeeder. Are you sure?",
                             [this, id, action] {
                                 std::string why;
                                 JPFeederForms::act(m_config, id, action, why);
                                 changed();
                             });
            return;
        }
        if (action == "blindsExtract") {
            if (extractBlindsFiles) extractBlindsFiles();
            return;
        }
        if (action == "setupOcrRegion" || action == "ocrRegionNext" || action == "ocrRegionCancel") {
            if (ocrRegion) ocrRegion(m_shown, action);
            return;
        }
        if (action == "cloneFromTemplate") {
            cloneFromTemplate(m_shown);
            return;
        }
        if (action == "cloneToFeeders") {
            cloneToFeeders(m_shown);
            return;
        }
        if (action == "plusOne") {
            plusOne(m_shown);
            return;
        }
        // A Bamboo feeder's, as OpenPnP's asks: its count reset, or its settings overwritten by Auto-Setup.
        if (const JPFeeder* f = m_config.feeder(m_shown); f && f->isVisionTape()) {
            const std::string id = m_shown;
            if (action == "resetFeedCount") {
                JDialog::confirm("Warning", "This will reset the recorded feed count of this feeder. Are you sure?", [this, id] {
                    std::string why;
                    if (!JPFeederForms::act(m_config, id, "resetFeedCount", why)) return;
                    rebuild();
                    m_table->refresh();
                    changed();
                });
                return;
            }
            const JPLocation at = f->location();
            if (action == "autoSetupTape" && (at.x() != 0 || at.y() != 0)) {
                std::string ask = "This may overwrite all your current settings. Are you sure?";
                if (f->flag("used-as-template", false)) ask += "\n\nThis feeder is marked as template. Are you really, really sure?";
                JDialog::confirm("Warning", ask, [this, id] {
                    if (machineAction) machineAction(id, "autoSetupTape");
                });
                return;
            }
        }
        if (action == "editPipeline" || action == "resetPipeline" || action == "editTrainingPipeline"
            || action == "resetTrainingPipeline" || action == "editDropBoxPipeline" || action == "resetDropBoxPipeline") {
            if (pipelineAction) pipelineAction(m_shown, action);
            return;
        }
        // A search shows its strip from the start, every address not yet asked.
        if (action == "photonSearch") {
            m_searchStates.assign(size_t(std::max(1, m_config.photon().maxFeederAddress())), 0);
            jPostToNextFrame([this, alive = std::weak_ptr<bool>(m_alive)] {
                if (const auto a = alive.lock(); a && *a) rebuildForm();
            });
        }
        // Done on the machine: on its thread.
        if (JPFeederForms::isMachineAction(action)) {
            if (machineAction) machineAction(m_shown, action);
            return;
        }
        std::string why;
        if (!JPFeederForms::act(m_config, m_shown, action, why, [this](const std::string& key) { return reading(key); })) {
            if (!why.empty()) JDialog::message("Error", why);
            return;
        }
        // Not while the button clicked is still in its page: the page is made again.
        jPostToNextFrame([this, alive = std::weak_ptr<bool>(m_alive)] {
            if (const auto a = alive.lock(); a && *a) rebuildForm();
        });
        m_table->refresh();
        changed();
    };
    m_form->onCapture = [this](const JPSetupProperties::Row& row, Tool t) { capture(row, t); };
    m_form->onMoveTo = [this](const JPSetupProperties::Row& row, Tool t) { goTo(row, t); };
    m_form->onMoveToStraight = [this](const JPSetupProperties::Row& row, Tool t) { goTo(row, t, true); };
    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_tablePane.get(), float(split));
    m_split->addPane(m_formPane.get(), float(1 - split));

    buildMenu();
    m_table->setContextMenu(m_menu.get());
    selectionChanged();
}

void JPFeedersPanel::cloneFromTemplate(const std::string& feederId) {
    const JPFeeder* f = m_config.feeder(feederId);
    if (!f) return;
    auto& c = m_cloneChoices;
    if (f->flag("used-as-template", false)) {
        JDialog::message("Error", "This feeder is used as a template and cannot be overwritten.");
        return;
    }
    if (!(c["tape"] || c["pushPull"] || c["vision"])) {
        JDialog::message("Error", "Please select some feeder settings to clone.");
        return;
    }
    if (JPPushPullTemplates::templateFeeder(m_config, feederId).empty()) {
        JDialog::message("Error", "No suitable template feeder found.");
        return;
    }
    JDialog::confirm("Warning",
                     "This will overwrite the selected settings with those from the template:\n\n"
                         + JPPushPullTemplates::cloneTemplateStatus(m_config, feederId) + "\n\nAre you sure?",
                     [this, feederId] {
                         auto& c = m_cloneChoices;
                         std::string why;
                         if (!JPPushPullTemplates::smartClone(m_config, feederId, {}, { c["location"], c["tape"], c["pushPull"], c["vision"], c["vision"] },
                                                              why))
                             JDialog::message("Error", why);
                         rebuild();
                         m_table->refresh();
                         changed();
                     });
}

void JPFeedersPanel::cloneToFeeders(const std::string& feederId) {
    const JPFeeder* f = m_config.feeder(feederId);
    if (!f) return;
    auto& c = m_cloneChoices;
    if (!f->flag("used-as-template", false)) {
        JDialog::message("Error", "This feeder is not used as a template.");
        return;
    }
    if (!(c["tape"] || c["pushPull"] || c["vision"])) {
        JDialog::message("Error", "Please select some feeder settings to clone.");
        return;
    }
    if (JPPushPullTemplates::compatibleFeeders(m_config, feederId).empty()) {
        JDialog::message("Error", "No suitable feeders found to clone to.");
        return;
    }
    JDialog::confirm("Warning",
                     "This will overwrite the selected settings in all the target feeders:\n\n"
                         + JPPushPullTemplates::cloneTemplateStatus(m_config, feederId) + "\n\nAre you sure?",
                     [this, feederId] {
                         auto& c = m_cloneChoices;
                         for (const std::string& target : JPPushPullTemplates::compatibleFeeders(m_config, feederId))
                             JPPushPullTemplates::cloneSettings(m_config, target, feederId,
                                                                { c["location"], c["tape"], c["pushPull"], c["vision"], c["vision"] });
                         rebuild();
                         m_table->refresh();
                         changed();
                     });
}

void JPFeedersPanel::plusOne(const std::string& feederId) {
    std::string newId, why;
    if (!JPPushPullTemplates::createInRow(m_config, feederId, newId, why)) {
        if (!why.empty()) JDialog::message("Error", why);
        return;
    }
    m_table->refresh();
    changed();
    selectFeeder(newId);
    // The camera over its place, and it set up from there.
    if (machineAction) machineAction(newId, "autoSetupInRow");
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

void JPFeedersPanel::rebuild() {
    jPostToNextFrame([this, alive = std::weak_ptr<bool>(m_alive)] {
        if (const auto a = alive.lock(); a && *a) rebuildForm();
    });
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
    if (m_selecting != JPFeederForms::Options::Selecting::None) cancelSelection();
    m_shown = id;
    if (id.empty()) {
        m_form->setForm(JPSetupProperties::Form {});
        return;
    }
    m_form->setForm(formFor());
    // What its page shows from the machine, read afresh when it can be.
    if (machineAction && machineReady && machineReady())
        for (const std::string& action : JPFeederForms::readsOnShow(*f)) machineAction(id, action);
}

void JPFeedersPanel::showSearchState(int address, int state) {
    if (address < 1 || m_searchStates.empty()) return;
    if (size_t(address) > m_searchStates.size()) m_searchStates.resize(size_t(address), 0);
    m_searchStates[size_t(address - 1)] = state;
    m_form->refresh();
}

void JPFeedersPanel::searchEnded() {
    m_searchStates.clear();
    m_table->refresh();
    rebuildForm();
}

std::string JPFeedersPanel::reading(const std::string& action) const {
    const auto f = m_readings.find(m_shown);
    if (f == m_readings.end()) return {};
    const auto r = f->second.find(action);
    return r == f->second.end() ? std::string() : r->second;
}

void JPFeedersPanel::showReading(const std::string& feederId, const std::string& action, const std::string& value) {
    m_readings[feederId][action] = value;
    if (feederId == m_shown) m_form->refresh();
}

void JPFeedersPanel::rebuildForm() {
    if (m_config.feeder(m_shown)) m_form->remake(formFor());
}

JPSetupProperties::Form JPFeedersPanel::formFor() {
    JPFeederForms::Options options;
    if (actuatorNames) options.actuators = actuatorNames();
    options.selecting = m_selecting;
    options.templateImage = [this] { return templateImage(); };
    options.reading = [this](const std::string& action) { return reading(action); };
    options.autoSetupRunning = autoSetupRunning && autoSetupRunning();
    options.searchStates = [this] { return m_searchStates; };
    if (m_fonts.empty()) {
        // OpenPnP's createFontSelectionList: the barcode reader first, then the system's fonts.
        m_fonts.push_back("[Barcode]");
        for (const JSystemFont& f : jListSystemFonts())
            if (std::find(m_fonts.begin(), m_fonts.end(), f.name) == m_fonts.end()) m_fonts.push_back(f.name);
    }
    options.fonts = m_fonts;
    options.ocrRegionStep = ocrRegionStep ? ocrRegionStep() : std::string();
    options.cloneChoices = &m_cloneChoices;
    return JPFeederForms::forFeeder(m_config, m_shown, [](const std::string& why) { JDialog::message("Error", why); },
                                    options);
}

std::shared_ptr<const JPFrame> JPFeedersPanel::templateImage() {
    const JPFeeder* f = m_config.feeder(m_shown);
    const std::string path = f ? f->templatePath(m_config.directory()) : std::string();
    std::error_code ec;
    const auto when = path.empty() ? std::filesystem::file_time_type {} : std::filesystem::last_write_time(path, ec);
    if (path == m_templatePath && when == m_templateTime) return m_template;
    m_templatePath = path;
    m_templateTime = when;
    m_template.reset();
    if (!path.empty() && !ec) {
        auto frame = std::make_shared<JPFrame>();
        std::string error;
        if (JPImageFile::readPng(path, *frame, error)) m_template = std::move(frame);
        else JLOGC(JPlacerLog::kUi, JLogLevel::Warn) << error;
    }
    return m_template;
}

void JPFeedersPanel::selectOnCamera(JPFeederForms::Options::Selecting what) {
    using Selecting = JPFeederForms::Options::Selecting;
    JPFeeder* f = m_config.feeder(m_shown);
    JPCameraView* view = cameraView ? cameraView() : nullptr;
    if (!f) return;
    if (!view) {
        JDialog::message("Error", "No camera on the head to select with.");
        return;
    }
    if (m_selecting == what) {
        // Confirm: the selection taken.
        if (what == Selecting::Template) {
            if (!confirmTemplate(*f, *view)) return;
        } else {
            JPCameraView::Selection r = view->selection();
            // A Neoden 4 feeder's is from the picture's middle.
            if (f->typeName() == "Neoden4Feeder") {
                r.x -= view->pictureWidth() / 2;
                r.y -= view->pictureHeight() / 2;
            }
            f->setAttributeAt("vision/area-of-interest", "x", std::to_string(r.x));
            f->setAttributeAt("vision/area-of-interest", "y", std::to_string(r.y));
            f->setAttributeAt("vision/area-of-interest", "width", std::to_string(r.width));
            f->setAttributeAt("vision/area-of-interest", "height", std::to_string(r.height));
            changed();
        }
        view->setSelectionEnabled(false);
        m_selecting = Selecting::None;
    } else {
        // Select: from the area of interest as it is (else a start), the template afresh.
        JPCameraView::Selection r { 0, 0, kFirstSelectionPx, kFirstSelectionPx };
        if (what == Selecting::AreaOfInterest) {
            const JPCameraView::Selection aoi { std::atoi(f->attributeAt("vision/area-of-interest", "x", "0").c_str()),
                                                std::atoi(f->attributeAt("vision/area-of-interest", "y", "0").c_str()),
                                                std::atoi(f->attributeAt("vision/area-of-interest", "width", "0").c_str()),
                                                std::atoi(f->attributeAt("vision/area-of-interest", "height", "0").c_str()) };
            if (aoi.width > 0 && aoi.height > 0) r = aoi;
            if (aoi.width > 0 && aoi.height > 0 && f->typeName() == "Neoden4Feeder") {
                r.x += view->pictureWidth() / 2;
                r.y += view->pictureHeight() / 2;
            }
        }
        view->setSelection(r);
        view->setSelectionEnabled(true);
        m_selecting = what;
    }
    jPostToNextFrame([this, alive = std::weak_ptr<bool>(m_alive)] {
        if (const auto a = alive.lock(); a && *a) rebuildForm();
    });
}

bool JPFeedersPanel::confirmTemplate(JPFeeder& f, JPCameraView& view) {
    const std::shared_ptr<JPFrame> image = view.captureSelection();
    if (!image) {
        JDialog::message("No Image Selected", "Please select an area of the camera image using the mouse.");
        return false;
    }
    // Into OpenPnP's resource file for it: the one it has, else a new one.
    std::string name = f.attributeAt("vision", "template-image-name");
    if (name.empty())
        name = "tmpl_" + std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count()) + ".png";
    const std::string dir = JPFeeder::templateDirectory(m_config.directory(), f.className());
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string error;
    if (!JPImageFile::writePng(dir + "/" + name, *image, error)) {
        JDialog::message("Error", error);
        return false;
    }
    f.setAttributeAt("vision", "template-image-name", name);
    changed();
    return true;
}

void JPFeedersPanel::cancelSelection() {
    if (JPCameraView* view = cameraView ? cameraView() : nullptr) view->setSelectionEnabled(false);
    m_selecting = JPFeederForms::Options::Selecting::None;
    jPostToNextFrame([this, alive = std::weak_ptr<bool>(m_alive)] {
        if (const auto a = alive.lock(); a && *a) rebuildForm();
    });
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

void JPFeedersPanel::selectFeederForPart(const std::string& partId) {
    if (const JPFeeder* f = selection(); f && f->partId() == partId) return;
    for (const bool enabled : { true, false })
        for (const JPFeeder& f : m_config.feeders())
            if (f.partId() == partId && f.enabled() == enabled) {
                selectFeeder(f.id());
                return;
            }
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

void JPFeedersPanel::feedOrPick(JPFeeder& f, bool pick) {
    if (f.partId().empty()) {
        JDialog::message("Error", "Feeder " + f.name() + " has no part.");
        return;
    }
    // The feed (a strip's vision too) and the pick move the machine: done on its thread.
    if (machineFeed) machineFeed(f.id(), pick);
}

void JPFeedersPanel::pick() {
    if (JPFeeder* f = selection()) pickFrom(*f);
}

void JPFeedersPanel::pickFrom(JPFeeder& f) {
    feedOrPick(f, true);
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
    if (moveTo) moveTo(tool, to, false);
}

void JPFeedersPanel::capture(const JPSetupProperties::Row& row, Tool tool) {
    Where now;
    if (tool == Tool::Actuator) {
        if (!whereIsActuator || !row.actuator) return;
        now = whereIsActuator(row.actuator());
    } else {
        if (!whereIs) return;
        now = whereIs(tool);
    }
    // As OpenPnP: the camera's coordinates are X, Y and rotation; its Z is what the head's Z probe finds there,
    // filled in when the probe has read.
    if (tool == Tool::Camera) {
        now[2].reset();
        if (probeZ && now[0] && now[1]) {
            const Where at = now;
            const JPSetupProperties::Row r = row;
            if (probeZ(*now[0], *now[1], [this, at, r, alive = std::weak_ptr<bool>(m_alive)](double z) {
                    if (const auto a = alive.lock(); !a || !*a) return;
                    Where probed = at;
                    probed[2] = z;
                    applyCapture(r, probed);
                }))
                return;
        }
    }
    applyCapture(row, now);
}

void JPFeedersPanel::applyCapture(const JPSetupProperties::Row& row, Where now) {
    // Offsets from a base: less it, turned back by its rotation.
    if (const auto base = row.base ? row.base() : std::nullopt; base && now[0] && now[1]) {
        const JPLocation at(JPLengthUnit::Millimeters, *now[0], *now[1], now[2].value_or(0), now[3].value_or(0));
        const JPLocation b = base->convertToUnits(JPLengthUnit::Millimeters);
        const JPLocation off = at.subtractWithRotation(b).rotateXy(-b.rotation());
        now = { off.x(), off.y(), now[2] ? std::optional(off.z()) : std::nullopt, now[3] ? std::optional(off.rotation()) : std::nullopt };
    }
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

void JPFeedersPanel::goTo(const JPSetupProperties::Row& row, Tool tool, bool straight) {
    Where to;
    for (size_t i = 0; i < row.cells.size() && i < to.size(); ++i) {
        if (row.cells[i].property.empty()) continue;
        const JVariant v = m_form->get(row.cells[i].property);
        if (v.isDouble() || v.isInt()) to[i] = v.toDouble();
    }
    // Offsets from a base: turned by its rotation, added to it (OpenPnP's
    // positioning leaves the offsets' own rotation out).
    if (const auto base = row.base ? row.base() : std::nullopt) {
        const JPLocation b = base->convertToUnits(JPLengthUnit::Millimeters);
        const JPLocation at = JPLocation(JPLengthUnit::Millimeters, to[0].value_or(0), to[1].value_or(0), to[2].value_or(0), 0)
                                  .rotateXy(b.rotation())
                                  .addWithRotation(b);
        to = { at.x(), at.y(), to[2] ? std::optional(at.z()) : std::nullopt, at.rotation() };
    }
    // A camera stays at safe Z; a place without Z leaves the tool there too.
    if (tool == Tool::Camera) to[2].reset();
    if (tool == Tool::Actuator) {
        if (moveActuatorTo && row.actuator) moveActuatorTo(row.actuator(), to, straight);
        return;
    }
    if (moveTo) moveTo(tool, to, straight);
}

} // inline namespace jf
