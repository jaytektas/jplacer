// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFeedersTableModel.h"
#include "JPIconButton.h"
#include "JPSetupForm.h"
#include "JPTable.h"

#include "JPCameraView.h"

#include "model/JPConfiguration.h"
#include "setup/JPFeederForms.h"

#include <j/core/JContainer.h>
#include <j/core/JLineEdit.h>
#include <j/core/MenuSystem.h>
#include <j/core/Splitter.h>

#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// The Feeders tab, as OpenPnP's FeedersPanel: a toolbar (New Feeder…,
// Delete Feeder…, Pick, Feed, Move Camera and Move Tool to the pick
// location), a search box, the feeders table (JPFeedersTableModel) with its
// right-click Set Enabled and Set Feed option, and under it the chosen
// feeder's setup (JPFeederForms), its location buttons taking the camera or
// the chosen nozzle there or taking where it is.
class JPFeedersPanel : public JContainer {
public:
    using Tool  = JPSetupForm::Tool;
    using Where = std::array<std::optional<double>, 4>;

    JPFeedersPanel(JSceneGraph& graph, JPConfiguration& config, double split);
    ~JPFeedersPanel() override { *m_alive = false; }

    // A feeder added, changed or deleted (to be saved, other views told).
    std::function<void()> onChanged;
    std::function<void(JMenu*, float x, float y)> openMenu;
    // OpenPnP's ClassSelectionDialog: one of `classes` (their simple names shown) chosen, or none.
    std::function<void(const std::string& title, const std::string& description, const std::vector<std::string>& classes,
                       std::function<void(std::string)> chosen)>
        chooseClass;
    // The machine: where the camera or chosen nozzle is, taking it somewhere
    // at safe Z, and the chosen nozzle's pick at a place.
    std::function<Where(Tool)> whereIs;
    // `straight`: not by way of safe Z (Position Tool (Without Safe Z)).
    std::function<void(Tool, const Where&, bool straight)> moveTo;
    // The same for an actuator on the head, by its name (a drag feeder's pin).
    std::function<Where(const std::string& actuator)> whereIsActuator;
    std::function<void(const std::string& actuator, const Where&, bool straight)> moveActuatorTo;
    // The head camera's live picture, brought to the front, for a drag
    // feeder's template image and area of interest to be selected on; none
    // without a camera on the head.
    std::function<JPCameraView*()> cameraView;
    // OpenPnP's feedFeeder (and with `pick`, pickFeeder): a feed, and the
    // chosen nozzle's pick, on the machine's thread; its outcome said there.
    std::function<void(const std::string& feederId, bool pick)> machineFeed;
    // A button of a feeder's page done on the machine (JPFeederForms::isMachineAction),
    // on its thread; what it reads comes back to showReading.
    std::function<void(const std::string& feederId, const std::string& action)> machineAction;
    // Whether the machine is on (connected), for a page to read from it when shown.
    std::function<bool()> machineReady;
    // The machine's actuators by name.
    std::function<std::vector<std::string>()> actuatorNames;
    // OpenPnP's Z probe: the head's Z probe read over (x, y) on the machine, its Z to `done`; false when there is
    // no probe (nothing is done).
    std::function<bool(double x, double y, std::function<void(double z)> done)> probeZ;
    // Whether the job uses a part (an enabled placement on an enabled board).
    std::function<bool(const std::string& partId)> partUsed;

    // What uploads the pictures its pages show (a drag feeder's template image).
    void setHal(JGpuHal* hal) { m_form->setHal(hal); }
    // A Photon search going on: an address asked (its JPSearchStrip state),
    // shown on the page's strip; and the search ended.
    void showSearchState(int address, int state);
    void searchEnded();
    // OpenPnP's Program Feeder Slot Wizard asked for (Global Config's Start Wizard).
    std::function<void()> programPhotonSlots;
    // A feeder's pipeline: "editPipeline" (the pipeline editor) or "resetPipeline" (its default back);
    // an advanced loose part feeder's training one: "editTrainingPipeline", "resetTrainingPipeline".
    std::function<void(const std::string& feederId, const std::string& action)> pipelineAction;
    // A strip feeder's Auto Setup: started ("autoSetup") or cancelled ("autoSetupCancel"); whether one is under way.
    std::function<void(const std::string& feederId, const std::string& action)> autoSetup;
    std::function<bool()> autoSetupRunning;
    // A push-pull feeder's Setup OCR Region: started ("setupOcrRegion"), gone on ("ocrRegionNext"), cancelled
    // ("ocrRegionCancel"); what its going-on button says while under way (empty: not under way).
    std::function<void(const std::string& feederId, const std::string& action)> ocrRegion;
    std::function<std::string()> ocrRegionStep;
    // A blinds feeder's Extract 3D-Printing Files: the OpenSCAD files written to a folder chosen.
    std::function<void()> extractBlindsFiles;
    // The page made again (Auto Setup started or ended).
    void rebuild();
    // What a page's button read from the machine (by its action), shown on the feeder's page.
    void showReading(const std::string& feederId, const std::string& action, const std::string& value);
    // The feeders changed elsewhere (imported, a job's part): shown again.
    void refresh();
    // OpenPnP's showFeederForPart: the search cleared and the part's feeder
    // chosen (an enabled one first), else a new feeder made for it.
    void showFeederForPart(const std::string& partId);
    double split() const;
    // A feeder chosen (and shown), by id.
    void selectFeeder(const std::string& id);
    // OpenPnP's selectFeederForPart: unless the feeder chosen has the part, its
    // feeder chosen (an enabled one first); none made when it has none.
    void selectFeederForPart(const std::string& partId);
    // One feeder chosen in the table (for the tables linked to it, View > Selections in Tables), and
    // again when the chosen one's part is changed (what is linked to it follows its part now).
    std::function<void(const JPFeeder&)> onFeederChosen;
    // OpenPnP's pickFeeder: a feed, then the chosen nozzle's pick at its pick location.
    void pickFrom(JPFeeder& f);

    // A feeder's page shown elsewhere too (Machine Setup's Feeders, as OpenPnP's): the feeder chosen here,
    // its page as this tab makes it; then what is done on the other page done as on this one.
    JPSetupProperties::Form pageFor(const std::string& feederId);
    bool showFeeder(const std::string& feederId);   // chosen and shown here; false when there is no such feeder
    void edited(const std::string& property);   // a setting on the shown feeder's page changed
    void act(const std::string& action);        // a button on it
    void captureFor(const JPSetupProperties::Row& row, Tool tool) { capture(row, tool); }
    void goToFor(const JPSetupProperties::Row& row, Tool tool, bool straight) { goTo(row, tool, straight); }
    // The shown feeder's page made again (its buttons changed), or only shown again (a reading came).
    std::function<void()> onPageRemade, onPageRefreshed;

private:
    std::vector<JPFeeder*> selections() const;
    JPFeeder* selection() const;
    void selectionChanged();
    void showForm();
    void buildMenu();
    void newFeeder(const std::string& partId);
    void deleteFeeders();
    void feedOrPick(JPFeeder& f, bool pick);
    void pick();
    void moveToPick(Tool tool);
    void capture(const JPSetupProperties::Row& row, Tool tool);
    // A place captured (X, Y, Z, rotation; one not known left as it is) put in the row, from its base when it has one.
    void applyCapture(const JPSetupProperties::Row& row, Where now);
    void goTo(const JPSetupProperties::Row& row, Tool tool, bool straight = false);
    void changed();
    void followPickRotation();
    // The form made again for the feeder shown (its buttons changed), scrolled as it was.
    void rebuildForm();
    // The shown feeder's form, as it is to be shown now.
    JPSetupProperties::Form formFor();
    // The Stock tab: the stock lot the feeder carries, chosen among its part's, and what it holds.
    void stockTab(JPSetupProperties::Form& form);
    // A drag feeder's Select (Confirm while selecting) and Cancel, for its
    // template image or its area of interest (OpenPnP's select, confirm and
    // cancel actions); the camera's selection ended.
    void selectOnCamera(JPFeederForms::Options::Selecting what);
    void cancelSelection();
    bool confirmTemplate(JPFeeder& f, JPCameraView& view);
    // What the shown feeder's page last read from the machine, by its action.
    std::string reading(const std::string& action) const;
    // The shown drag feeder's template image, read from its file again when the file changed.
    std::shared_ptr<const JPFrame> templateImage();

    // A push-pull feeder's Clone … Settings? ticks (the page's own, all on at first, as OpenPnP's).
    std::map<std::string, bool>         m_cloneChoices { { "location", true }, { "tape", true }, { "vision", true }, { "pushPull", true } };
    // The fonts OCR can read, listed once.
    std::vector<std::string>            m_fonts;
    // A push-pull feeder's Clone from Template, Clone to Feeders and + (one more in its row).
    void cloneFromTemplate(const std::string& feederId);
    void cloneToFeeders(const std::string& feederId);
    void plusOne(const std::string& feederId);
    JPConfiguration&                    m_config;
    JPFeedersTableModel                 m_model;
    JPTable*                            m_table = nullptr;
    JLineEdit*                          m_search = nullptr;
    JSplitter*                          m_split = nullptr;
    std::unique_ptr<JContainer>         m_tablePane, m_formPane;
    JPSetupForm*                        m_form = nullptr;
    std::string                         m_shown;   // the feeder whose setup is shown
    std::string                         m_chosenPart;   // the chosen feeder's part, as onFeederChosen was told
    std::string                         m_cameraAtPickOf;   // the feeder whose pick location the camera was last moved to
    Where                               m_cameraAtPick;     // where it was moved to
    JPIconButton*                       m_delete = nullptr;
    JPIconButton*                       m_pick = nullptr;
    JPIconButton*                       m_feed = nullptr;
    JPIconButton*                       m_moveCamera = nullptr;
    JPIconButton*                       m_moveTool = nullptr;
    std::unique_ptr<JMenu>              m_menu;
    std::vector<std::unique_ptr<JMenu>> m_subMenus;
    JMenuItem*                          m_setEnabled = nullptr;
    JMenuItem*                          m_setFeedOptions = nullptr;
    JPFeederForms::Options::Selecting   m_selecting = JPFeederForms::Options::Selecting::None;
    std::string                         m_templatePath;   // whose picture m_template is
    std::filesystem::file_time_type     m_templateTime;
    std::shared_ptr<const JPFrame>      m_template;
    // What the machine was last read as on each feeder's page: by feeder, then action.
    std::map<std::string, std::map<std::string, std::string>> m_readings;
    std::vector<int>                    m_searchStates;   // a Photon search's, by address less one
    std::shared_ptr<bool>               m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
