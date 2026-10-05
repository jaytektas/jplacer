// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCellConfig.h"
#include "JPScripting.h"
#include "JPMotionTestResult.h"
#include "JPFirmwareProfile.h"
#include "JPGcodeDriver.h"

#include <j/concurrent/WorkerThread.h>
#include <j/core/Signal.h>

#include <array>
#include <atomic>
#include <thread>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

inline namespace jf {

// One machine, running: its controllers and the CELL THREAD that sequences
// everything done to it.
//
// Every request (connect, switch an actuator, read one, a console line) is
// posted to the cell thread and returns at once; the outcome arrives as a
// signal. Signals fire on the cell or a driver's I/O thread, never the GUI's:
// a widget re-posts through JMainThreadDispatcher before touching itself.
class JPCell {
public:
    JPCell(JPCellConfig config, std::vector<JPFirmwareProfile> profiles);
    ~JPCell();

    JPCell(const JPCell&)            = delete;
    JPCell& operator=(const JPCell&) = delete;

    const JPCellConfig& config() const { return m_config; }
    bool isConnected() const { return m_connected; }

    // NEW SETTINGS FOR THE RUNNING MACHINE (Machine Setup). Nothing is let
    // go: each controller takes its new settings as it runs, its open link
    // staying open (settings of how to connect are used at the next
    // connect); one added is connected at the next connect, one removed let
    // go. The machine stays homed unless its axes changed (then its
    // coordinates mean something else). Waits for the cell thread, so no move
    // runs meanwhile; refused (false, with `why`) while one is under way.
    bool reconfigure(JPCellConfig config, std::string& why);

    // Connect every controller, identify it and read its stored settings.
    // All or nothing: if one fails, the others are disconnected again.
    void connect();
    void disconnect();

    // Send a line as typed to one controller (the console).
    void sendLine(const std::string& driverId, const std::string& line);

    void switchActuator(const std::string& actuatorId, bool on);
    // A Number or Text actuator set to `value` (its value command); reported
    // by onActuator as a switch is.
    void setActuator(const std::string& actuatorId, const std::string& value);
    // The same, waiting for the controller's answer: for a procedure on a
    // thread of its own. False with `why`.
    bool switchActuatorAndWait(const std::string& actuatorId, bool on, std::string& why);
    void readActuator(const std::string& actuatorId);
    // Read an actuator, waiting for its value: for a procedure on a thread of
    // its own. With `parameter`, its read command's {value} is it (OpenPnP's
    // actuator.read(parameter): a feeder's number). False with `why`.
    bool readActuatorAndWait(const std::string& actuatorId, const std::optional<std::string>& parameter, std::string& value,
                             std::string& why);
    // A Number or Text actuator set to `value`, waiting for the controller's
    // answer: for a procedure on a thread of its own. False with `why`.
    bool setActuatorAndWait(const std::string& actuatorId, const std::string& value, std::string& why);
    // Pick with a nozzle where it is: its head's vacuum pump on as the head's
    // pump control says (waiting the pump-on time when it starts), its vacuum
    // on, then the nozzle's and its tip's pick dwell, and the part checked as
    // the tip's part detection says. Place: its vacuum off (unless its
    // blow-off closes the valve itself), the blow-off pulsed for the place
    // dwell, the pump off when its control says so, and the part checked
    // gone. Each actuator switched is reported on onActuator, a failed check
    // on onAlarm; nothing moves.
    void pick(const std::string& nozzleId);
    void place(const std::string& nozzleId);
    // Every Z on `headId` into its safe zone (OpenPnP's Head Safe Z). Refused
    // while a move is under way.
    void safeZ(const std::string& headId, double speed);
    // OpenPnP's Z park: the tool on `mount` to its safe Z (the low end of its
    // Z axis's safe zone), and first, with the machine's Park all at Safe Z?,
    // every Z on its head into its safe zone. Refused while a move is under way.
    void parkZ(const JPMountConfig& mount, double speed);
    // Drop what `nozzleId` holds at the machine's discard location: up to
    // safe Z, across, down to its Z, then a place. Refused while a move is
    // under way; nothing happens when no discard location is set.
    void discard(const std::string& nozzleId, double speed);
    // For a procedure on a thread of its own (a job), waiting: a pick with
    // `nozzleId` at `to` (X, Y, Z, rotation in its own coordinates; one not
    // given stays as it is: up to safe Z, across and turned, down to Z, the
    // pick as pick(), up to safe Z again), a place there (the part let go, as place()), a
    // discard, the head parked, a tool taken to `to` (as moveTool). False
    // with `why`.
    // For a procedure on a thread of its own, waiting, nothing moving: a
    // nozzle's vacuum on as a pick puts it on (its head's pump started as its
    // control says); a pick where it is (as pick()); its vacuum level read.
    bool vacuumOnAndWait(const std::string& nozzleId, std::string& why);
    bool pickAndWait(const std::string& nozzleId, std::string& why);
    bool readVacuumAndWait(const std::string& nozzleId, double& level, std::string& why);
    bool pickAtAndWait(const std::string& nozzleId, std::array<std::optional<double>, 4> to, double speed, std::string& why);
    bool placeAtAndWait(const std::string& nozzleId, std::array<std::optional<double>, 4> to, double speed, std::string& why);
    bool discardAndWait(const std::string& nozzleId, double speed, std::string& why);
    bool parkAndWait(const std::string& headId, double speed, std::string& why);
    bool moveToolAndWait(const JPMountConfig& mount, std::array<std::optional<double>, 4> to, double speed, std::string& why);
    // A tool straight to `to` from where it is, every axis given at once,
    // not up to safe Z first (OpenPnP's moveTo of a head mountable: a drag
    // pin put down and pulled along). Waiting; false with `why`.
    bool moveToolStraightAndWait(const JPMountConfig& mount, std::array<std::optional<double>, 4> to, double speed,
                                 std::string& why);
    // OpenPnP's motion planner Test Motion (JPMotionPlannerConfig): `tool` to
    // the run's first place by way of safe Z, and once it stands there on
    // through the others, each leg at its share of the machine's speed, by
    // way of safe Z or straight, then up to safe Z; `reverse` from the last.
    // Waiting; `result` what the run took (JPMotionTestResult). False with `why`.
    bool testMotionAndWait(const JPMountConfig& tool, bool reverse, JPMotionTestResult& result, std::string& why);
    // OpenPnP's ContactProbeNozzle.contactProbe, from where the nozzle is:
    // forward probes down (at most `depthMm`) until contact, and leaves it
    // there (Final Adjustment applied); back retracts. `probedZ`: the nozzle's
    // Z then. Waiting; false with `why` (no contact, no way to probe).
    bool contactProbeAndWait(const std::string& nozzleId, bool forward, double depthMm, double& probedZ, std::string& why);
    // The probed height offsets a contact probing nozzle keeps, by feeder
    // (`feeder`) or by part; forgotten on homing as its triggers say.
    std::optional<double> probedOffset(const std::string& nozzleId, bool feeder, const std::string& key) const;
    // OpenPnP's nozzle tip Z calibration (JPNozzleTipConfig::touchLocation):
    // the tip on a contact probing nozzle probed at its touch location, and
    // every Z move of the nozzle made by how far off it was; or (`reset`)
    // forgotten. Not waited for; a failure is an alarm. The offset in use
    // (none: not calibrated); dropped when the tip changes.
    void calibrateZ(const std::string& nozzleId, bool reset);
    std::optional<double> zCalibration(const std::string& nozzleId) const;
    // The same, waited for (from a thread of the caller's own).
    bool calibrateZAndWait(const std::string& nozzleId, std::string& why);
    // OpenPnP's contactProbeCycle: the nozzle above `at` by its Start Offset (by way of safe Z),
    // probed down as far as its Depth and retracted; `probedZ` where it met. `resetZCalibration`:
    // its Z calibration forgotten first (OpenPnP's reference probing). Waited for.
    bool contactProbeCycleAndWait(const std::string& nozzleId, const JPMachineLocation& at, bool resetZCalibration,
                                  double& probedZ, std::string& why);
    void setProbedOffset(const std::string& nozzleId, bool feeder, const std::string& key, double offsetMm);

    // HOMING: each controller's home command, then the axes are told where
    // they now are (their home coordinates) and the cell is homed. Until it
    // is, no move is made: positions mean nothing to a soft limit before.
    void home();
    // No longer homed (a homing that failed after the controllers homed: a calibration with Fail Homing).
    void unhome();
    // Park the head (its JPHeadConfig::park): every Z on it into its safe
    // zone first, then the head's camera (else its first tool on X and Y) to
    // the park place. The outcome arrives as onMotion.
    void park(const std::string& headId, double speed);
    // HOME ONE NOZZLE'S Z (a motor that slipped a step forcing a tip on
    // leaves it at the wrong height): the head parked first (up to safe Z,
    // across to its park place, which must be clear of anything below), then
    // the nozzle's home command (JPNozzleConfig::homeCommand) sent to the
    // controller of the motor behind its Z, and that axis then at its home
    // coordinate, as after a full home. Every nozzle sharing the motor is
    // homed with it (nozzlesHomedWith). Needs the machine homed; the outcome
    // arrives as onMotion.
    void homeNozzle(const std::string& nozzleId, double speed);
    // The nozzles one Z home of `nozzleId` homes (those on the same motor),
    // itself first.
    std::vector<std::string> nozzlesHomedWith(const std::string& nozzleId) const;
    // Every Z behind the head's tools into its safe zone, waiting: for a
    // procedure on a thread of its own. False with `why`.
    bool safeZAndWait(const std::string& headId, double speed, std::string& why);
    bool isHomed() const { return m_homed; }
    // STOP, from any thread (it does not wait behind the move it stops):
    // every controller held, then its queue thrown away, the position kept;
    // or, `emergency`, every controller reset at once, after which the
    // machine must be homed again (a controller stopped mid-move may lose its
    // place). What was moving fails with "stopped". False with `why` when no
    // controller's firmware has a way to stop.
    bool stop(bool emergency, std::string& why);
    // A home is under way (from the request until homed or failed).
    bool isHoming() const { return m_homing; }
    // THE MACHINE'S SPEED, as OpenPnP's: a share of full speed (0..1) every
    // move is scaled by, on top of its own (a jog's, a park's, a task's, a
    // changer step's): a step at 1% with the machine at 5% goes at 0.05%.
    void setSpeed(double share);
    double speed() const { return m_speed; }
    // A move is under way.
    bool isMoving() const { return m_moving; }
    // The part a nozzle is given (OpenPnP's setPart, before its pick): its
    // height, for the nozzle's Dynamic Safe Z, and its package's pick vacuum
    // level and place blow-off level (0: none, the vacuum switched on, the
    // tip's blow-off level used). All 0: no part.
    struct PartOnNozzle {
        std::string partId;   // empty: no part
        double heightMm = 0, pickVacuumLevel = 0, placeBlowOffLevel = 0;
    };
    void setNozzlePart(const std::string& nozzleId, const PartOnNozzle& part);
    // OpenPnP's rotation mode offset: while a nozzle holds a part, its
    // rotation is the part's angle, its rotation axis that much less (as
    // OpenPnP's ReferenceNozzle.toHeadLocation): a rotation it is sent to is
    // the axis's plus this, and the rotation it reads the axis's plus this.
    // Set for a pick (by its Rotation Mode, and with Align with Part by bottom
    // vision's turn); none again when the part goes. 0: none.
    void   setRotationModeOffset(const std::string& nozzleId, std::optional<double> offset);
    double rotationModeOffset(const std::string& nozzleId) const;
    // The offset of the nozzle `mount` is (0 for another tool).
    double rotationModeOffsetOf(const JPMountConfig& mount) const;
    // Simulation Mode's Pick & Place Checking (OpenPnP's): a nozzle holding
    // a part switching its vacuum on (a pick) or off (a place), not near the
    // discard location, with an image camera on its head, is checked by
    // `checker` where the simulated machine has the nozzle; a pick or place
    // it does not recognize fails ("pick location not recognized").
    struct PnpCheck {
        std::string nozzleId, partId;
        bool        pick = true;
        double      x = 0, y = 0, rotation = 0;   // the nozzle, as the simulated machine has it
        JJson       camera;                        // the head's image camera's device settings
    };
    using PnpChecker = std::function<bool(const PnpCheck& check, std::string& detail)>;
    void setPnpChecker(PnpChecker checker) { m_pnpChecker = std::move(checker); }
    // Where script actuators' scripts are found and run (none: they cannot be actuated).
    void setScripting(std::shared_ptr<JPScripting> scripting) { m_scripting = std::move(scripting); }

    // Move a tool — a nozzle, camera or actuator — by the given amounts along
    // its own axes (mm, degrees), at `speed` (0..1) of the slowest axis's
    // rate. Refused while a move is under way, so held jogging cannot pile up.
    void jog(const std::string& toolId, double dx, double dy, double dz, double drot, double speed);

    // Take a tool (what `mount` describes: a nozzle, a camera on the head) to
    // a place in its own coordinates: up to safe Z, across to X, Y and the
    // rotation given, then down to Z when one is given. A coordinate not
    // given stays as it is. Refused while a move is under way.
    void moveTool(const JPMountConfig& mount, std::array<std::optional<double>, 4> to, double speed);
    // The same, straight there (not by way of safe Z): OpenPnP's Position Tool (Without Safe Z).
    void moveToolStraight(const JPMountConfig& mount, std::array<std::optional<double>, 4> to, double speed);
    // Move axes to coordinates, by axis id. Checked against soft limits; a
    // mapped axis moves its input axis. Runs on the cell thread; the outcome
    // arrives as onMotion.
    void moveAxes(std::map<std::string, double> targets, double speed);
    // The same, waiting for the outcome: for a procedure on a thread of its
    // own (never the cell's, which runs the move). False with `why`.
    // `squared` false: the targets are in the axes' own coordinates, as
    // reported and as taught (a nozzle tip changer's places), not corrected
    // for the gantry's squareness.
    bool moveAxesAndWait(std::map<std::string, double> targets, double speed, std::string& why, bool squared = true);

    // The machine is not where its coordinates say: each axis (by id) is off
    // by `by`, so from now on where it is now is called (now - by). Told to
    // the controllers (their setPosition command); nothing moves. Waits, like
    // moveAxesAndWait. For visual homing, which measures how far off it is.
    bool correctPosition(const std::map<std::string, double>& by, std::string& why);
    // A new squareness correction (JPSquarenessConfig). Every coordinate means
    // something else after it, so the machine is no longer homed.
    void setSquareness(const JPSquarenessConfig& squareness);
    JPSquarenessConfig squareness() const;

    // An axis's backlash compensation, in use from the next move (the cell's
    // own copy; the owner keeps it): what calibrating it found, to test.
    void setBacklash(const std::string& axisId, JPAxisConfig::Backlash method, double offset, double sneakUpMm,
                     double speedFactor, std::vector<std::pair<double, double>> table = {}, double approachMm = 0);
    // Backlash compensation on (as each axis says) or off (every move goes
    // straight to its target): off while the backlash is being measured.
    void setBacklashCompensation(bool on) { m_backlashOn = on; }
    // The controller axes' positions as their controllers report them: with
    // a directional backlash offset in effect, the axis's plus it (where the
    // drive was sent). positions() gives the axes' own.
    std::map<std::string, double> reportedPositions() const;

    // The corrections made since the last home, summed, by axis id: where
    // the switches put the machine is (coordinates + this).
    std::map<std::string, double> correctionSinceHome() const;

    // Keep a camera's calibration, in place of one at the same picture size
    // (the cell's own copy; the owner saves it).
    void setCameraCalibration(const std::string& cameraId, const JPCameraCalibration& calibration);
    // A camera's calibration for pictures width x height; not valid when it
    // has none at that size.
    JPCameraCalibration cameraCalibration(const std::string& cameraId, int width, int height) const;
    // All of a camera's calibrations, a picture size each.
    std::vector<JPCameraCalibration> cameraCalibrations(const std::string& cameraId) const;
    // Whether the caller is the cell's own thread (where waiting for the cell would never end).
    bool onCellThread() const { return std::this_thread::get_id() == m_threadId.load(); }
    // The cell's Simulation Mode as it is now (for the cameras' threads).
    JPSimulationConfig simulation() const;
    // What an actuator was last switched to (on or off), if it has been since connecting.
    std::optional<bool> switchedOn(const std::string& actuatorId) const;

    // Each controller's last reported state (Idle, Run, Alarm…), by controller id.
    std::map<std::string, std::string> states() const;
    // Whether any controller reports its profile's alarm state.
    bool inAlarm() const;

    // The latest coordinate of every axis, by axis id.
    std::map<std::string, double> positions() const;

    // Where each axis was last SENT, where that still agrees with where it
    // reports being (within kSettledTolerance); otherwise where it reports.
    // A motor lands on its nearest step, so a reported position is a hair off
    // the commanded one (389.001 for 389), and stepping from it carries the
    // error into every next step — until an exact limit refuses a move back.
    std::map<std::string, double> jogBase() const;
    // Whether a tool can be taken to (x, y): every axis it moves within its soft limits (OpenPnP's isReachable).
    bool reaches(const JPMountConfig& mount, double x, double y) const;
    // The firmware each connected controller identified as, by controller id.
    std::map<std::string, std::string> firmware() const;
    // By controller id: what it said when it was identified (JPGcodeDriver::identity).
    std::map<std::string, std::string> firmwareIdentity() const;

    JSignal<bool, std::string>                   onConnection;   // connected; why not (a failure or a lost link)
    JSignal<std::map<std::string, double>>       onPositions;
    JSignal<std::string, bool, std::string>      onTraffic;      // controller name, sent, line
    JSignal<std::string, bool, std::string>      onActuator;     // id, done, value or why not
    JSignal<std::string>                         onAlarm;        // in words, with the controller's name
    JSignal<bool, std::string>                   onMotion;       // a move or home ended: ok, why not
    JSignal<bool>                                onHomed;
    JSignal<>                                    onCalibration;  // a camera's calibration or the squareness changed
    JSignal<std::string, std::string>            onState;        // controller id, its new state

private:
    JPGcodeDriver* driver(const std::string& id) const;
    std::unique_ptr<JPGcodeDriver> makeDriver(const JPDriverConfig& config);
    // A controller as it is run: as set up, or, while the cell's Simulation Mode
    // replaces drivers (OpenPnP's Replace Drivers?), simulated, its axes' letters kept.
    static JPDriverConfig asRun(const JPDriverConfig& driver, const JPCellConfig& cell);
    static std::string format(double v, int decimals);
    void updatePositions(const std::string& driverId, const JPFirmwareProfile::Status& status);
    // `keepingAlive`: a Disconnect asked for, which leaves the Keep Alive controllers open.
    void doDisconnect(bool keepingAlive = false);
    // The cell thread's side of moveAxes: false with `why` when refused or failed.
    bool doMove(std::map<std::string, double> targets, double speed, std::string& why, bool squared = true);
    // OpenPnP's linear transform axes among `targets` solved back onto their input
    // axes (OpenPnP's ReferenceLinearTransformAxis.toRaw); false and why when they cannot be.
    bool resolveLinear(std::map<std::string, double>& targets, const std::map<std::string, double>& now, std::string& why) const;
    bool doMoveNow(std::map<std::string, double> targets, double speed, std::string& why, bool squared);
    // OpenPnP's axis interlocks (JPActuatorConfig::Interlock) for a move from `from` to `to`, before or after it.
    bool doInterlocks(const std::map<std::string, double>& from, const std::map<std::string, double>& to, bool before,
                      double speed, std::string& why);
    bool inSafeZone(const std::string& axisId, double value) const;
    // A controller's units (its Driver Settings' Units): a millimetre in them,
    // an axis's letter and coordinate as sent, and a coordinate it reports in mm.
    double driverUnits(const JPGcodeDriver& d) const;
    // Whether another axis of its controller has its letter (a shared output, OpenPnP's pre-move commands).
    bool sharesLetter(const JPAxisConfig& a) const;
    // OpenPnP's HttpActuator: a GET of `url` (not again when it was the last one), and its read.
    static constexpr int kHttpTimeoutMs = 5000;
    bool httpGet(const JPActuatorConfig& a, const std::string& url, std::string& why);
    bool httpRead(const JPActuatorConfig& a, std::string& value, std::string& why);
    std::string word(const JPAxisConfig& a, double value, const JPGcodeDriver& d) const;
    double fromDriver(const JPAxisConfig& a, double value, const std::string& driverId);
    // OpenPnP's Unsafe Z Roaming, for a jog of a tool: its Z to safe Z with the move when too far from where it was left low.
    void roamUnsafeZ(const std::string& toolId, const JPMountConfig& mount, const std::map<std::string, double>& now,
                     std::map<std::string, double>& targets);
    bool doHome(std::string& why);
    bool doPark(const std::string& headId, double speed, std::string& why);
    bool doHomeNozzle(const std::string& nozzleId, double speed, std::string& why);
    // The controller axis behind a mount's Z (through a mapped axis); null when none.
    const JPAxisConfig* zMotor(const JPMountConfig& mount) const;
    bool doSafeZ(const std::string& headId, double speed, std::string& why);
    bool doParkZ(const JPMountConfig& mount, double speed, std::string& why);
    // How far a nozzle's safe Z is raised for the part it carries (Dynamic Safe Z); 0 for anything else.
    double dynamicLift(const JPMountConfig& mount) const;
    // `depth`: how deep in profiles naming profiles (JPActuatorConfig::Profile) this is.
    // Each coordinates with the machine as the actuator says (OpenPnP's Machine
    // Coordination) before and after the actuation itself (doSwitchNow, doSetNow).
    bool doSwitch(const std::string& actuatorId, bool on, std::string& why, int depth = 0);
    bool doSet(const std::string& actuatorId, const std::string& value, std::string& why, int depth = 0);
    bool doSwitchNow(const JPActuatorConfig& a, bool on, std::string& why, int depth);
    bool doSetNow(const JPActuatorConfig& a, const std::string& value, std::string& why, int depth);
    bool doProfile(const JPActuatorConfig& actuator, const JPActuatorConfig::Profile& profile, std::string& why, int depth);
    bool doPick(const JPNozzleConfig& nozzle, std::string& why);
    // A nozzle to `to` at safe Z, the pick or place there, and up again.
    bool doAt(const std::string& nozzleId, const std::array<std::optional<double>, 4>& to, double speed, bool pick,
              std::string& why);
    bool doDiscard(const std::string& nozzleId, double speed, std::string& why);
    // `work` on the cell thread as a move (refused while one is under way), waited for.
    bool waitFor(std::function<bool(std::string&)> work, std::string& why);
    bool doPlace(const JPNozzleConfig& nozzle, std::string& why);
    bool doRead(const std::string& actuatorId, std::string& value, std::string& why,
                const std::optional<std::string>& parameter = std::nullopt);
    bool doReadNow(const JPActuatorConfig& a, std::string& value, std::string& why, const std::optional<std::string>& parameter);
    // OpenPnP's machine coordination, `how` one of JPActuatorConfig's
    // coordinations: none; the controllers moving told to finish
    // (CommandStillstand); waited for to stand still (WaitForStillstand); or
    // every controller waited for and its position read, moving or not
    // (WaitForUnconditionalCoordination).
    bool doCoordinate(const std::string& how, std::string& why);
    bool doContactProbe(const JPNozzleConfig& n, bool forward, double depthMm, double& probedZ, std::string& why);
    bool doCalibrateZ(const JPNozzleConfig& n, std::string& why);
    bool doContactProbeCycle(const JPNozzleConfig& n, const JPMachineLocation& at, double& probedZ, std::string& why);
    bool doZCalibrationsAfterHoming(std::string& why);
    // The nozzle a mount is (none: a camera's, an actuator's), and its Z offset with its tip's Z calibration in.
    const JPNozzleConfig* nozzleOf(const JPMountConfig& mount) const;
    double zOffsetOf(const JPMountConfig& mount) const;
    // A nozzle's part-off check (its valve opened, closed, the vacuum read): `off` whether it senses none.
    bool partOffCheck(const JPNozzleConfig& n, const JPNozzleTipConfig& tip, bool& off, std::string& why);
    // A tool to `to`: by way of safe Z (up, across and turned, down to Z), or straight.
    bool doMoveTool(const JPMountConfig& mount, const std::array<std::optional<double>, 4>& to, double speed, bool atSafeZ,
                    std::string& why);
    // A piece of work's end, `ok` and `why` as it ended: with continuous
    // motion, the moves it left running waited for (a failure there the
    // work's, when it had none of its own).
    bool finished(bool ok, std::string& why);
    // The nozzle's vacuum level, from its sensing actuator (else its vacuum actuator).
    bool readVacuum(const JPNozzleConfig& nozzle, double& level, std::string& why);
    // `work` on the cell's thread, waited for (not a move: no motion is reported).
    bool onThreadAndWait(const std::function<bool(std::string&)>& work, std::string& why);
    // The nozzle's vacuum on (the pump first, as its head's control says).
    bool doVacuumOn(const JPNozzleConfig& nozzle, std::string& why);
    // A part on (or off) as `sensing` says, from the level now (and `before`
    // for a difference); false with why not.
    bool sensed(const JPNozzleConfig& nozzle, const JPNozzleTipConfig::Sensing& sensing, double before, const char* onOff,
                std::string& why);
    // Switch, and say so on onActuator.
    bool switchTelling(const std::string& actuatorId, bool on, std::string& why);
    // Switch every actuator as its setting for this machine state says
    // (JPActuatorConfig::enabledActuation, homedActuation, disabledActuation).
    void actuateFor(const std::string JPActuatorConfig::*state);
    bool doCorrectPosition(const std::map<std::string, double>& by, std::string& why);
    // Square coordinates of controller axes to the axes' own (JPSquarenessConfig):
    // the X axis takes the lean for the Y it will be at, so a target for
    // either brings in the other (from `now`, square, when not targeted).
    // The directional backlash offset in effect on an axis (0: none).
    double backlashApplied(const std::string& axisId) const;
    // The runout to compensate for the nozzle on `mount` (its tip's, on it,
    // when compensated); null for any other tool.
    const JPRunout* runoutFor(const JPMountConfig& mount) const;
    // `targets` for a tool's axes (X, Y, rotation) made to carry its runout:
    // an X or Y target is where the tip's centre is to be (`stepped`: the
    // axis's own position now plus a step, the centre moved by that step);
    // a turn alone moves X and Y so the centre stays put.
    void compensateRunout(const JPMountConfig& mount, std::map<std::string, double>& targets, bool stepped) const;
    std::map<std::string, double> toAxes(std::map<std::string, double> square,
                                         const std::map<std::string, double>& now) const;

    JPCellConfig                                m_config;
    std::vector<JPFirmwareProfile>              m_profiles;
    std::vector<std::unique_ptr<JPGcodeDriver>> m_drivers;
    std::atomic<bool>                           m_connected{ false };
    std::atomic<bool>                           m_homed{ false };
    std::atomic<bool>                           m_homing{ false };
    std::atomic<bool>                           m_moving{ false };
    std::map<std::string, PartOnNozzle>         m_nozzleParts;   // by nozzle id; the cell's thread's
    std::mutex                                  m_roamMutex;
    std::map<std::string, std::pair<double, double>> m_roamFrom;   // by tool: where it was left at unsafe Z
    std::map<std::string, bool>                 m_actuated;      // by actuator id: what it was last switched to
    std::map<std::string, std::string>          m_lastHttpUrl;   // by HTTP actuator: the URL asked last
    std::shared_ptr<JPScripting>                m_scripting;
    PnpChecker                                  m_pnpChecker;
    // The pick or place of the part on `n` checked (true: no check due, or recognized).
    bool pnpChecked(const JPNozzleConfig& n, bool pick, std::string& why);
    // A script actuator's script run, told `globals`.
    bool runActuatorScript(const JPActuatorConfig& a, JJson globals, std::string& why);
    std::map<std::string, std::optional<bool>>  m_conditionalLast;   // by interlocked actuator: its condition's last state
    std::atomic<double>                         m_speed{ 1.0 };
    // On the cell thread: the heads whose pump is on, the nozzles holding a part.
    std::set<std::string>                       m_pumpOn, m_holding;

    mutable std::mutex                 m_mutex;   // guards the members below
    std::map<std::string, double>      m_rotationModeOffset;   // by nozzle id
    std::map<std::string, bool>        m_switchedOn;   // switchedOn()
    std::atomic<std::thread::id>       m_threadId;     // the cell's thread (onCellThread)
    std::map<std::string, double>      m_positions;
    std::map<std::string, double>      m_axisPositions;   // controller axes as they report (not squared)
    std::map<std::string, double>      m_sent;       // last commanded coordinate, by axis id
    // Continuous motion (JPMotionPlannerConfig): the controllers sent moves not
    // yet waited for; while any are, where the axes are going is where they are.
    std::vector<std::string>           m_inMotion;
    std::atomic<bool>                  m_streaming { false };
    std::optional<double>              m_planned;
    // Contact probing's offsets, by nozzle, then feeder (or part) id.
    std::map<std::string, std::map<std::string, double>> m_probedFeederOffsets, m_probedPartOffsets;
    // By nozzle: its tip's Z calibration (the tip it was made with, the offset).
    struct ZCalibration {
        std::string tipId;
        double      offsetMm = 0;
    };
    std::map<std::string, ZCalibration> m_zCalibration;    // Test Motion: the moves' planned seconds, summed while set
    // A directional backlash offset in effect, by axis id: the controller's
    // coordinate is the axis's plus this (JPAxisConfig::Backlash).
    std::map<std::string, double>      m_backlashApplied;
    // DistanceAware: how far each axis's drive lags where it was sent (signed).
    std::map<std::string, double>      m_backlashLag;
    std::map<std::string, double>      m_reported;   // controller axes, as reported
    std::atomic<bool>                  m_backlashOn{ true };
    std::map<std::string, double>      m_corrected;  // correctPosition's since the last home, summed
    std::map<std::string, std::string> m_firmware;
    std::map<std::string, std::string> m_firmwareIdentity;
    std::map<std::string, std::string> m_states;

    JWorkerThread m_thread;                  // last: stopped first, before what it uses
};

} // inline namespace jf
