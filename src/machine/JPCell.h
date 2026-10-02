// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCellConfig.h"
#include "JPFirmwareProfile.h"
#include "JPGcodeDriver.h"

#include <j/concurrent/WorkerThread.h>
#include <j/core/Signal.h>

#include <map>
#include <memory>
#include <mutex>
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

    // HOMING: each controller's home command, then the axes are told where
    // they now are (their home coordinates) and the cell is homed. Until it
    // is, no move is made: positions mean nothing to a soft limit before.
    void home();
    // Park the head (its JPHeadConfig::park): every Z on it into its safe
    // zone first, then the head's camera (else its first tool on X and Y) to
    // the park place. The outcome arrives as onMotion.
    void park(const std::string& headId, double speed);
    bool isHomed() const { return m_homed; }
    // A home is under way (from the request until homed or failed).
    bool isHoming() const { return m_homing; }

    // Move a tool — a nozzle, camera or actuator — by the given amounts along
    // its own axes (mm, degrees), at `speed` (0..1) of the slowest axis's
    // rate. Refused while a move is under way, so held jogging cannot pile up.
    void jog(const std::string& toolId, double dx, double dy, double dz, double drot, double speed);

    // Move axes to coordinates, by axis id. Checked against soft limits; a
    // mapped axis moves its input axis. Runs on the cell thread; the outcome
    // arrives as onMotion.
    void moveAxes(std::map<std::string, double> targets, double speed);
    // The same, waiting for the outcome: for a procedure on a thread of its
    // own (never the cell's, which runs the move). False with `why`.
    bool moveAxesAndWait(std::map<std::string, double> targets, double speed, std::string& why);

    // The machine is not where its coordinates say: each axis (by id) is off
    // by `by`, so from now on where it is now is called (now - by). Told to
    // the controllers (their setPosition command); nothing moves. Waits, like
    // moveAxesAndWait. For visual homing, which measures how far off it is.
    bool correctPosition(const std::map<std::string, double>& by, std::string& why);
    // A new squareness correction (JPSquarenessConfig). Every coordinate means
    // something else after it, so the machine is no longer homed.
    void setSquareness(const JPSquarenessConfig& squareness);
    JPSquarenessConfig squareness() const;

    // The corrections made since the last home, summed, by axis id: where
    // the switches put the machine is (coordinates + this).
    std::map<std::string, double> correctionSinceHome() const;

    // Keep a camera's calibration (the cell's own copy; the owner saves it).
    void setCameraCalibration(const std::string& cameraId, const JPCameraCalibration& calibration);
    JPCameraCalibration cameraCalibration(const std::string& cameraId) const;

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
    static std::string format(double v, int decimals);
    void updatePositions(const std::string& driverId, const JPFirmwareProfile::Status& status);
    void doDisconnect();
    // The cell thread's side of moveAxes: false with `why` when refused or failed.
    bool doMove(std::map<std::string, double> targets, double speed, std::string& why);
    bool doHome(std::string& why);
    bool doPark(const std::string& headId, double speed, std::string& why);
    bool doSwitch(const std::string& actuatorId, bool on, std::string& why);
    bool doCorrectPosition(const std::map<std::string, double>& by, std::string& why);
    // Square coordinates of controller axes to the axes' own (JPSquarenessConfig):
    // the X axis takes the lean for the Y it will be at, so a target for
    // either brings in the other (from `now`, square, when not targeted).
    std::map<std::string, double> toAxes(std::map<std::string, double> square,
                                         const std::map<std::string, double>& now) const;

    JPCellConfig                                m_config;
    std::vector<JPFirmwareProfile>              m_profiles;
    std::vector<std::unique_ptr<JPGcodeDriver>> m_drivers;
    std::atomic<bool>                           m_connected{ false };
    std::atomic<bool>                           m_homed{ false };
    std::atomic<bool>                           m_homing{ false };
    std::atomic<bool>                           m_moving{ false };

    mutable std::mutex                 m_mutex;   // guards the members below
    std::map<std::string, double>      m_positions;
    std::map<std::string, double>      m_axisPositions;   // controller axes as they report (not squared)
    std::map<std::string, double>      m_sent;       // last commanded coordinate, by axis id
    std::map<std::string, double>      m_corrected;  // correctPosition's since the last home, summed
    std::map<std::string, std::string> m_firmware;
    std::map<std::string, std::string> m_states;

    JWorkerThread m_thread;                  // last: stopped first, before what it uses
};

} // inline namespace jf
