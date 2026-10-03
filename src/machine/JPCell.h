// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCellConfig.h"
#include "JPFirmwareProfile.h"
#include "JPGcodeDriver.h"

#include <j/concurrent/WorkerThread.h>
#include <j/core/Signal.h>

#include <array>
#include <atomic>
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
    // The same, waiting for the controller's answer: for a procedure on a
    // thread of its own. False with `why`.
    bool switchActuatorAndWait(const std::string& actuatorId, bool on, std::string& why);
    void readActuator(const std::string& actuatorId);
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
    // Drop what `nozzleId` holds at the machine's discard location: up to
    // safe Z, across, down to its Z, then a place. Refused while a move is
    // under way; nothing happens when no discard location is set.
    void discard(const std::string& nozzleId, double speed);

    // HOMING: each controller's home command, then the axes are told where
    // they now are (their home coordinates) and the cell is homed. Until it
    // is, no move is made: positions mean nothing to a soft limit before.
    void home();
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

    // Move a tool — a nozzle, camera or actuator — by the given amounts along
    // its own axes (mm, degrees), at `speed` (0..1) of the slowest axis's
    // rate. Refused while a move is under way, so held jogging cannot pile up.
    void jog(const std::string& toolId, double dx, double dy, double dz, double drot, double speed);

    // Take a tool (what `mount` describes: a nozzle, a camera on the head) to
    // a place in its own coordinates: up to safe Z, across to X, Y and the
    // rotation given, then down to Z when one is given. A coordinate not
    // given stays as it is. Refused while a move is under way.
    void moveTool(const JPMountConfig& mount, std::array<std::optional<double>, 4> to, double speed);
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
                     double speedFactor);
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
    // The firmware each connected controller identified as, by controller id.
    std::map<std::string, std::string> firmware() const;

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
    static std::string format(double v, int decimals);
    void updatePositions(const std::string& driverId, const JPFirmwareProfile::Status& status);
    void doDisconnect();
    // The cell thread's side of moveAxes: false with `why` when refused or failed.
    bool doMove(std::map<std::string, double> targets, double speed, std::string& why, bool squared = true);
    bool doHome(std::string& why);
    bool doPark(const std::string& headId, double speed, std::string& why);
    bool doHomeNozzle(const std::string& nozzleId, double speed, std::string& why);
    // The controller axis behind a mount's Z (through a mapped axis); null when none.
    const JPAxisConfig* zMotor(const JPMountConfig& mount) const;
    bool doSafeZ(const std::string& headId, double speed, std::string& why);
    bool doSwitch(const std::string& actuatorId, bool on, std::string& why);
    bool doPick(const JPNozzleConfig& nozzle, std::string& why);
    bool doPlace(const JPNozzleConfig& nozzle, std::string& why);
    bool doRead(const std::string& actuatorId, std::string& value, std::string& why);
    // The nozzle's vacuum level, from its sensing actuator (else its vacuum actuator).
    bool readVacuum(const JPNozzleConfig& nozzle, double& level, std::string& why);
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
    std::atomic<double>                         m_speed{ 1.0 };
    // On the cell thread: the heads whose pump is on, the nozzles holding a part.
    std::set<std::string>                       m_pumpOn, m_holding;

    mutable std::mutex                 m_mutex;   // guards the members below
    std::map<std::string, double>      m_positions;
    std::map<std::string, double>      m_axisPositions;   // controller axes as they report (not squared)
    std::map<std::string, double>      m_sent;       // last commanded coordinate, by axis id
    // A directional backlash offset in effect, by axis id: the controller's
    // coordinate is the axis's plus this (JPAxisConfig::Backlash).
    std::map<std::string, double>      m_backlashApplied;
    std::map<std::string, double>      m_reported;   // controller axes, as reported
    std::atomic<bool>                  m_backlashOn{ true };
    std::map<std::string, double>      m_corrected;  // correctPosition's since the last home, summed
    std::map<std::string, std::string> m_firmware;
    std::map<std::string, std::string> m_states;

    JWorkerThread m_thread;                  // last: stopped first, before what it uses
};

} // inline namespace jf
