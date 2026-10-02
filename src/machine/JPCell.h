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
    void readActuator(const std::string& actuatorId);

    // HOMING: each controller's home command, then the axes are told where
    // they now are (their home coordinates) and the cell is homed. Until it
    // is, no move is made: positions mean nothing to a soft limit before.
    void home();
    bool isHomed() const { return m_homed; }

    // Move a tool — a nozzle, camera or actuator — by the given amounts along
    // its own axes (mm, degrees), at `speed` (0..1) of the slowest axis's
    // rate. Refused while a move is under way, so held jogging cannot pile up.
    void jog(const std::string& toolId, double dx, double dy, double dz, double drot, double speed);

    // Move axes to coordinates, by axis id. Checked against soft limits; a
    // mapped axis moves its input axis. Runs on the cell thread; the outcome
    // arrives as onMotion.
    void moveAxes(std::map<std::string, double> targets, double speed);

    // Each controller's last reported state (Idle, Run, Alarm…), by controller id.
    std::map<std::string, std::string> states() const;
    // Whether any controller reports its profile's alarm state.
    bool inAlarm() const;

    // The latest coordinate of every axis, by axis id.
    std::map<std::string, double> positions() const;
    // The firmware each connected controller identified as, by controller id.
    std::map<std::string, std::string> firmware() const;

    JSignal<bool, std::string>                   onConnection;   // connected, why not
    JSignal<std::map<std::string, double>>       onPositions;
    JSignal<std::string, bool, std::string>      onTraffic;      // controller name, sent, line
    JSignal<std::string, bool, std::string>      onActuator;     // id, done, value or why not
    JSignal<std::string>                         onAlarm;        // in words, with the controller's name
    JSignal<bool, std::string>                   onMotion;       // a move or home ended: ok, why not
    JSignal<bool>                                onHomed;
    JSignal<std::string, std::string>            onState;        // controller id, its new state

private:
    JPGcodeDriver* driver(const std::string& id) const;
    static std::string format(double v, int decimals);
    void updatePositions(const std::string& driverId, const JPFirmwareProfile::Status& status);
    void doDisconnect();
    // The cell thread's side of moveAxes: false with `why` when refused or failed.
    bool doMove(std::map<std::string, double> targets, double speed, std::string& why);
    bool doHome(std::string& why);

    JPCellConfig                                m_config;
    std::vector<JPFirmwareProfile>              m_profiles;
    std::vector<std::unique_ptr<JPGcodeDriver>> m_drivers;
    std::atomic<bool>                           m_connected{ false };
    std::atomic<bool>                           m_homed{ false };
    std::atomic<bool>                           m_moving{ false };

    mutable std::mutex                 m_mutex;   // guards the members below
    std::map<std::string, double>      m_positions;
    std::map<std::string, std::string> m_firmware;
    std::map<std::string, std::string> m_states;

    JWorkerThread m_thread;                  // last: stopped first, before what it uses
};

} // inline namespace jf
