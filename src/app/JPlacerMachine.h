// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerCameraTasks.h"
#include "JPlacerEstimateZ.h"
#include "JPlacerLayout.h"
#include "JPlacerTestMotion.h"
#include "JPlacerTipChanges.h"

#include "machine/JPCell.h"
#include "machine/JPScripting.h"
#include "ui/JPCameraPanel.h"
#include "ui/JPConnectIcon.h"
#include "ui/JPHomeIcon.h"
#include "ui/JPIconButton.h"
#include "ui/JPJogPanel.h"
#include "ui/JPMachineSetupPanel.h"
#include "model/JPConfiguration.h"
#include "ui/JPPositionReadout.h"

#include <j/app/JAppWindow.h>
#include <j/core/DockWidget.h>
#include <j/core/JContainer.h>
#include <j/core/MenuSystem.h>

#include "model/JPLocation.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// The machine jplacer is working with: the open cell (cells/<name>.json),
// its panels, each in a dock of its own where JPlacerLayout puts it (a
// camera each; Jog, Actuators; Board, Machine Setup, Machine; Console), the
// chosen tool's position in the status bar, the strip across the window
// saying what state it is in, and the Machine menu's actions on it.
//
// The cell opened last is opened again at start (JPlacerSettings::kMachineCell).
class JPlacerMachine {
public:
    JPlacerMachine(JAppWindow& window, JSceneGraph& graph);
    ~JPlacerMachine();

    JPlacerMachine(const JPlacerMachine&)            = delete;
    JPlacerMachine& operator=(const JPlacerMachine&) = delete;

    // The Machine menu's entries this class enables and disables.
    void setMenuItems(JMenuItem* connect, JMenuItem* disconnect, JMenuItem* home, JMenuItem* park);
    // Edit's Undo and Redo, which step through Machine Setup's changes.
    void setEditItems(JMenuItem* undo, JMenuItem* redo);
    void undo();
    void redo();
    // The Jog panel's steps, as Preferences > Jog keeps them (the defaults
    // until set): distances in mm or degrees, speeds as shares of full speed.
    static std::vector<double> jogDistances();
    static std::vector<double> jogSpeeds();
    // Preferences changed the steps, or a key: the Jog panel follows.
    void jogStepsChanged();
    void keysChanged();
    // The key an action has now ("" none), for tooltips (JPKeyMap::keyText).
    std::function<std::string(const std::string& action)> keyFor;
    // What a jog distance step may be, mm or degrees.
    static constexpr double kLeastJogDistance = 0.001, kMostJogDistance = 1000;
    // A Jog panel action (JPJogPanel::act), from Machine > Jog's keys.
    void jogAction(const std::string& action);

    void chooseCell();          // Machine > Open Cell…
    // Machine > Import OpenPnP Machine…: offers OpenPnP's usual machine.xml
    // when there is one, else (or on request) a file to choose.
    void importOpenPnp();
    void connect();
    void disconnect();
    void home();                // Machine > Home All Axes
    void park();                // Machine > Park Head
    // A nozzle's Z homed alone, from the park place (JPCell::homeNozzle).
    void homeNozzle(const std::string& nozzleId);
    // Machine > Stop (the move held and dropped, the position kept) and
    // Emergency Stop (every controller reset at once; home again after).
    void stop(bool emergency);
    // Bring the dock titled `title` to the front of its tab group (shown
    // again if it was closed). False when there is no such dock.
    bool showDock(const std::string& title);
    // Machine Setup in front, showing the node at `path` (JPSetupTree).
    void showSetup(const std::string& path);
    // Drawn over every camera's picture (and the cameras made later) until
    // set again by the same key; null takes it away.
    void setCameraOverlay(const std::string& key, JPCameraView::Overlay overlay);
    // The open cell's nozzle tips, id and name; none without a cell.
    std::vector<std::pair<std::string, std::string>> nozzleTips() const;

    // Whether a job is running (no other cell is opened meanwhile).
    std::function<bool()> jobRunning;
    // The head's Z probe read over (x, y), its Z to `done`; false without one (JPFeedersPanel::probeZ).
    std::function<bool(double x, double y, std::function<void(double z)> done)> probeZ;
    // The machine connected or not (a job's Start, Step and Stop follow it, as OpenPnP's do).
    std::function<void(bool connected)> onConnectedChanged;
    // The machine no longer homed (or homed again: it is unhomed first), on the main thread.
    std::function<void()> onUnhomed;
    bool isConnected() const { return m_cell && m_cell->isConnected(); }
    // An OpenPnP machine.xml imported (its feeders are the configuration's, and taken from it there).
    std::function<void(const std::string& machineXml)> onImported;
    // Where the head's camera, or the Jog panel's chosen nozzle, is now, in
    // the machine's millimetres: X, Y, Z, rotation, each where it has that
    // axis; none when the machine is not connected.
    using Where = std::array<std::optional<double>, 4>;
    Where whereIs(JPSetupForm::Tool tool) const;
    // The same as a location (Z and rotation 0 where it has no such axis);
    // none without X and Y.
    std::optional<JPLocation> toolLocation(JPSetupForm::Tool tool) const;
    // Takes it to `at` as OpenPnP's moveToLocationAtSafeZ: up to safe Z,
    // across (and turned) to X, Y and the rotation, then down to `at`'s Z (a
    // coordinate not given stays as it is). False, the reason shown, when
    // the machine cannot move.
    bool moveToolTo(JPSetupForm::Tool tool, const Where& at);
    bool moveToolTo(JPSetupForm::Tool tool, const JPLocation& at);
    // The same for an actuator on the head, by its OpenPnP name (a drag
    // feeder's pin): where it is, and taken to `at` at safe Z.
    Where whereIsActuator(const std::string& name) const;
    bool  moveActuatorTo(const std::string& name, const Where& at);
    // The head camera's live picture, brought to the front (a selection is
    // made on it); none when there is no camera on the head.
    JPCameraView* headCameraView();
    // A tip's runout on a nozzle kept (none: forgotten), and a background calibration's result, through
    // Machine Setup (a step to undo each).
    void keepRunout(const std::string& tipId, const std::string& nozzleId, const std::optional<JPRunout>& r);
    void keepBackground(const std::string& tipId, const JPBackgroundCalibration::Result& b);
    // What a nozzle holds (OpenPnP's Nozzle.getPart): the part last picked
    // with it, "" when none (placed or discarded since).
    std::string nozzlePart(const std::string& nozzleId) const;
    // The Jog panel's Recycle: the part on a nozzle put back into a feeder
    // (set by the tabs, which run it as a machine task), and whether some
    // feeder can take part `partId` back.
    std::function<void(const std::string& nozzleId)> recycle;
    std::function<bool(const std::string& partId)>   canRecycle;
    // Recycle offered again (feeders changed).
    void refreshRecycle() {
        if (m_jog) m_jog->refreshRecycle();
    }
    void        setNozzlePart(const std::string& nozzleId, const std::string& partId);
    // The parts' and vision settings' configuration (Machine Setup's Vision
    // nodes choose its vision settings).
    void setConfiguration(JPConfiguration* config);
    // Machine Setup's vision nodes' default settings' page: what its tests
    // work with, its settings changed, its buttons and sliders (for the
    // settings shown); shown again when the settings change elsewhere.
    void setSetupVisionTests(JPVisionTests tests);
    std::function<void()> onSetupConfigurationChanged;
    // Calibrate a camera, or an X or Y axis's backlash (as Machine Setup's
    // buttons do); `finished`: whether it was done.
    void calibrateCamera(const std::string& cameraId, std::function<void(bool ok)> finished);
    void calibrateBacklash(const std::string& axisId, std::function<void(bool ok)> finished);
    // A nozzle tip calibrated (its runout, and its background as its method
    // says) on the nozzle it is loaded on; `finished`: whether it was.
    void calibrateTip(const std::string& tipId, std::function<void(bool ok)> finished);
    // OpenPnP's Enable Visual Homing: the mark under the head's camera made
    // its homing mark (where it is, how wide) and visual homing turned on.
    void enableVisualHoming(const std::string& headId, std::function<void(bool ok)> finished);
    // A camera's own device settings as it last started (none while it has not).
    JJson cameraDeviceControls(const std::string& cameraId) const;
    // A camera's picture drawn smoothed (Rendering Quality High or Highest);
    // set to High, or back to Low, and kept (Issues & Solutions).
    bool cameraRenderingSmooth(const std::string& cameraId) const;
    void setCameraRenderingSmooth(const std::string& cameraId, bool smooth);
    // Every board in the job given this Z (mm): OpenPnP's Set Machine Table Z.
    std::function<void(double z)> setBoardsZ;
    std::function<void(const std::string& settingsId, const std::string& action)> onSetupVisionAction;
    void refreshSetupForm();
    // The view of a camera's feed, shown; null when it has none.
    JPCameraView* cameraViewOf(const JPCameraFeed* feed);
    // For a job (JPlacerJobMachine): the open cell, the head camera's
    // pictures, why a tip change cannot be made (empty: it can), and the tip
    // now on a nozzle kept (a step in Machine Setup; nothing moves).
    JPCell*       cell() const { return m_cell.get(); }
    JPCameraFeed* headCameraFeed() const;
    // The first camera fixed to the machine (looking up at the nozzles), a
    // calibrated one first; none when there is none.
    JPCameraFeed* upCameraFeed() const;
    // A camera's picture in front (where its dock is), for a look at it.
    void showCamera(const std::string& cameraId);
    // The nozzle chosen on the Jog panel (else the first); empty: none.
    std::string   chosenNozzleId() const;
    std::string   tipChangeRefusal(const std::string& nozzleId, const std::string& tipId) const;
    void          setTipOn(const std::string& nozzleId, const std::string& tipId);
    // OpenPnP's Photon feeders talk through a machine actuator named
    // PhotonFeederData: made, when the machine has none, on its first
    // controller, reading with "M485 {value}" and "rs485-reply: (.*)" (a
    // Machine Setup step like any other).
    void          ensurePhotonActuator();
    // For Issues & Solutions: Machine Setup's part selected ("camera:<id>",
    // empty: none), ready there when its tab is opened; a change to the setup made as a Machine Setup step;
    // whether a camera has a calibration.
    void          showSetupNode(const std::string& path);
    void          changeSetup(const std::string& what, const std::function<void(JPCellConfig&)>& edit);
    bool          cameraCalibrated(const std::string& cameraId) const;
    // Where the docks live, and View's entries for them.
    JPlacerLayout& layout() { return m_layout; }
    // OpenPnP's scripting: the scripts folder's scripts, and its events' (JPScripting).
    JPScripting& scripting() { return *m_scripting; }
    std::shared_ptr<JPScripting> sharedScripting() const { return m_scripting; }
    // An event's scripts run off the screen's thread (Startup, Machine.AfterHoming); a failure said.
    // `then` (screen's thread) once they have run, whether they failed or not.
    void runEvent(const std::string& event, std::function<void()> then = nullptr, JJson globals = JJson::object());

    // The directory cell files are kept in.
    static std::string cellsDir();

private:
    bool openCell(const std::string& path, std::string& error);
    void importFrom(const std::string& machineXml);
    void setPort(const std::string& driverId, const std::string& port);
    // A change in Machine Setup (and a port chosen): the running machine
    // takes `cell` (with the calibrations and squareness measured meanwhile,
    // see JPCell::reconfigure), the panels it changes are made again, and it
    // is kept in the cell file. False when the machine is moving (it is
    // tried again shortly).
    bool applySetup(JPCellConfig cell);
    // Follow the open cell's signals (the menu, the strip, the status bar).
    void watchCell();
    void updateMenu();
    // The connect and home icons follow the cell; the strip across the window
    // is kept for what is critical (ALARM, CONNECTION LOST) and a failure goes
    // to the status bar.
    void showState();
    // What stays when the panels are made again: nothing (another cell),
    // Machine Setup (its changes are what is being taken), or the cameras too
    // (and the Board, which works from them) when their settings did not change.
    enum class Keep { Nothing, Setup, SetupAndCameras };
    // One dock per panel, made the first time a cell opens; a new cell gets
    // new panels in the same docks, so where the person put them is kept.
    // The cameras' docks are the cell's own, made new with it.
    void buildPanels(Keep keep = Keep::Nothing);
    void buildCameras();
    std::unique_ptr<JPMachineSetupPanel> makeSetup();
    // A camera's light is on while its camera runs (on screen) and the
    // machine is connected; while not connected, a camera with a light says
    // why its picture is dark.
    // A light's state as last switched (none: not known), shown on its cameras' toggles; the toggle clicked.
    void showLight(const std::string& light, std::optional<bool> on);
    void toggleLight(const std::string& light);
    // OpenPnP's Move Selected Nozzle to Camera: the Jog panel's nozzle over a fixed camera.
    void moveNozzleToCamera(const std::string& cameraId);
    void lightCameras();
    void bringForward(JPCameraPanel& camera);
    void dropPanels(Keep keep = Keep::Nothing);
    void updateEditItems();
    // Machine Setup's buttons: Visual Test, Visual Home, a camera's Start Calibration.
    void setupAction(const std::string& path, const std::string& action);
    // What Machine Setup's place buttons use: the camera on the head, or the
    // nozzle chosen on the Jog panel (else the first). Null when there is none.
    const JPMountConfig* toolMount(JPSetupForm::Tool tool) const;
    Where whereIsMount(const JPMountConfig* mount) const;
    // Connected and homed; else the status bar says what is needed first.
    // OpenPnP's auto tool select: the tool a panel moved chosen on the Jog panel.
    void selectMoved(const JPMountConfig& mount);
    bool readyToMove();
    // The nozzle Offset Wizard's two steps: store where the nozzle left its
    // mark; then, the camera over the mark, move the nozzle's offset by the
    // difference (a step in Machine Setup, to undo).
    void nozzleOffsetWizard(const std::string& nozzleId, bool storeMark);

    struct Dock {
        std::unique_ptr<JDockWidget> dock;
        std::unique_ptr<JContainer>  panel;
    };

    JAppWindow&                         m_window;
    JSceneGraph&                        m_graph;
    JPlacerLayout                       m_layout;
    std::shared_ptr<JPScripting>        m_scripting;   // shared with a script run off the screen's thread
    std::vector<JPFirmwareProfile>      m_profiles;
    std::unique_ptr<JPCell>             m_cell;
    std::string                         m_cellPath;
    std::vector<Dock>                   m_docks;
    struct CameraDock {
        std::unique_ptr<JDockWidget>   dock;
        std::unique_ptr<JPCameraPanel> panel;
    };
    std::map<std::string, JPCameraView::Overlay> m_overlays;   // drawn on every camera (setCameraOverlay)
    std::vector<CameraDock>             m_cameras;   // the window's centre
    JPlacerEstimateZ                    m_estimateZ;   // on one of them, while under way
    std::unique_ptr<JPlacerCameraTasks> m_cameraTasks;   // its Calibrate and Visual Test
    std::unique_ptr<JPlacerTipChanges>  m_tipChanges;    // the nozzles' tips loaded and unloaded
    std::unique_ptr<JPlacerTestMotion>  m_testMotion;    // the motion planner's Test Motion
    std::string                         m_positionedCamera;   // a camera moved to look somewhere, by name, until there
    // A tip's runout measured on the nozzle it is on (asking first, or not: an automatic recalibration),
    // then kept; `done` whether it was, and why not.
    void calibrateTipRunout(const std::string& nozzleId, bool ask, std::function<void(bool, const std::string&)> done);
    // Once homed, each of `nozzles` in turn (JPlacerMachine::recalibrateAfterHoming); `done` false when one
    // failed with Fail Homing (the machine then unhomed).
    void recalibrateAfterHoming(std::vector<std::string> nozzles, std::function<void(bool)> done);
    // By nozzle tip: its last background calibration's problem pictures (BGR, as seen and marked, in pairs).
    std::map<std::string, std::vector<cv::Mat>> m_backgroundProblems;
    std::map<std::string, bool>         m_lights;   // by actuator id: on or off as last switched
    JPJogPanel*                         m_jog = nullptr; // its chosen tool, for the status bar
    JPMachineSetupPanel*                m_setup = nullptr;
    JPVisionTests                       m_setupVisionTests;
    JPConfiguration*                    m_configuration = nullptr;
    std::map<std::string, std::string>  m_nozzleParts;   // nozzle: the part it holds
    std::vector<std::function<void()>>  m_unwatch;   // this class's watches on the cell
    std::shared_ptr<bool>               m_alive = std::make_shared<bool>(true);
    JMenuItem*                          m_connectItem    = nullptr;
    JMenuItem*                          m_disconnectItem = nullptr;
    JMenuItem*                          m_homeItem       = nullptr;
    JMenuItem*                          m_parkItem       = nullptr;
    JMenuItem*                          m_undoItem       = nullptr;
    JMenuItem*                          m_redoItem       = nullptr;
    JPConnectIcon                       m_connectIcon;
    JPHomeIcon                          m_homeIcon;
    JPPositionReadout                   m_position;   // the chosen tool's, in the status bar
    bool                                m_connecting  = false;   // asked, not yet answered
    bool                                m_connectFailed = false; // the last connect failed
    std::string                         m_lost;                  // why the link dropped, until the next connect
    bool                                m_homeFailed = false;
    std::string                         m_setupSelected;   // Machine Setup's node, kept while the cell reopens
    struct NozzleMark {
        std::string nozzleId;
        double      x, y;
    };
    std::optional<NozzleMark>           m_nozzleMark;      // the Offset Wizard's stored mark
};

} // inline namespace jf
