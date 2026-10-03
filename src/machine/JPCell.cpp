// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCell.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <future>
#include <regex>

inline namespace jf {

JPCell::JPCell(JPCellConfig config, std::vector<JPFirmwareProfile> profiles)
    : m_config(std::move(config)), m_profiles(std::move(profiles)) {
    for (const JPAxisConfig& a : m_config.axes) m_positions[a.id] = a.homeCoordinate;
    for (const JPDriverConfig& d : m_config.drivers) m_drivers.push_back(makeDriver(d));
}

std::unique_ptr<JPGcodeDriver> JPCell::makeDriver(const JPDriverConfig& config) {
    auto driver = std::make_unique<JPGcodeDriver>(config, m_profiles);
    JPGcodeDriver* d = driver.get();   // its name as it is when it speaks: it can be renamed while it runs
    driver->onStatus.connect([this, d](JPFirmwareProfile::Status st) { updatePositions(d->id(), st); });
    driver->onTraffic.connect([this, d](bool sent, std::string line) { onTraffic.emit(d->config().name, sent, line); });
    driver->onAlarm.connect([this, d](std::string what) { onAlarm.emit(d->config().name + ": " + what); });
    driver->onLost.connect([this, d](std::string why) {
        const std::string what = d->config().name + ": connection lost (" + why + ")";
        onAlarm.emit(what);
        m_thread.post([this, what] {
            doDisconnect();
            onConnection.emit(false, what);
        });
    });
    return driver;
}

bool JPCell::reconfigure(JPCellConfig config, std::string& why) {
    if (m_moving || m_homing) {
        why = "the machine is moving: try again once it has stopped";
        return false;
    }
    std::promise<std::pair<bool, std::string>> done;
    auto result = done.get_future();
    m_thread.post([this, &config, &done] {
        // Every controller takes its new settings as it runs: the link that is
        // open stays open, and settings of how to connect are used the next time.
        // A controller new to the cell is connected the next time too.
        std::vector<std::unique_ptr<JPGcodeDriver>> drivers;
        for (const JPDriverConfig& d : config.drivers) {
            auto old = std::find_if(m_drivers.begin(), m_drivers.end(), [&d](const auto& o) { return o && o->id() == d.id; });
            if (old == m_drivers.end()) {
                drivers.push_back(makeDriver(d));
                continue;
            }
            (*old)->setConfig(d);
            drivers.push_back(std::move(*old));
        }
        for (auto& gone : m_drivers)
            if (gone) gone->disconnect();   // taken out of the cell
        m_drivers = std::move(drivers);

        // The axes as they were: the coordinates still mean what they did.
        JJson axesBefore = JJson::array(), axesAfter = JJson::array();
        for (const JPAxisConfig& a : m_config.axes) axesBefore.push(a.toJson());
        for (const JPAxisConfig& a : config.axes) axesAfter.push(a.toJson());
        const bool axesChanged = axesBefore.dump() != axesAfter.dump();
        {
            std::lock_guard lk(m_mutex);
            m_config = std::move(config);
            for (const JPAxisConfig& a : m_config.axes) m_positions.try_emplace(a.id, a.homeCoordinate);
        }
        JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": set up anew"
            << (axesChanged ? "; the axes changed" : "");
        if (axesChanged && m_homed) {
            m_homed = false;
            onHomed.emit(false);
        }
        done.set_value({ true, "" });
    });
    const auto [ok, w] = result.get();
    why = w;
    return ok;
}

JPCell::~JPCell() {
    m_thread.stop();
    doDisconnect();
}

JPGcodeDriver* JPCell::driver(const std::string& id) const {
    for (const auto& d : m_drivers) if (d->id() == id) return d.get();
    return nullptr;
}

void JPCell::connect() {
    m_thread.post([this] {
        if (m_connected) return;
        JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": connecting " << m_drivers.size() << " controller(s)";
        for (const auto& d : m_drivers) {
            std::string error;
            if (!d->connect(error)) {
                JLOGC(JPlacerLog::kCell, JLogLevel::Error) << m_config.name << ": not connected: " << error;
                doDisconnect();
                onConnection.emit(false, error);
                return;
            }
            if (d->profile()->hasSettings() && !d->readSettings(error))
                JLOGC(JPlacerLog::kDriver, JLogLevel::Warn) << error;
            std::lock_guard lk(m_mutex);
            m_firmware[d->id()] = d->profile()->name();
        }
        m_connected = true;
        JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": connected";
        onConnection.emit(true, std::string());
    });
}

void JPCell::disconnect() {
    m_thread.post([this] {
        const bool was = m_connected;
        doDisconnect();
        if (was) onConnection.emit(false, std::string());
    });
}

std::string JPCell::format(double v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

void JPCell::doDisconnect() {
    m_homed = false;
    if (m_connected) JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": disconnected";
    for (const auto& d : m_drivers) d->disconnect();
    m_connected = false;
    std::lock_guard lk(m_mutex);
    m_firmware.clear();
}

void JPCell::sendLine(const std::string& driverId, const std::string& line) {
    m_thread.post([this, driverId, line] {
        if (JPGcodeDriver* d = driver(driverId)) d->send(line).wait();
    });
}

void JPCell::park(const std::string& headId, double speed) {
    if (m_moving.exchange(true)) return;
    m_thread.post([this, headId, speed] {
        std::string why;
        const bool ok = doPark(headId, speed, why);
        m_moving = false;
        onMotion.emit(ok, why);
    });
}

bool JPCell::doPark(const std::string& headId, double speed, std::string& why) {
    const JPHeadConfig* head = nullptr;
    for (const JPHeadConfig& h : m_config.heads) if (h.id == headId) head = &h;
    if (!head || !head->park) { why = "the head has no park place set"; return false; }
    std::vector<const JPMountConfig*> mounts;
    for (const JPCameraConfig& c : m_config.cameras)     if (c.mount.headId == headId) mounts.push_back(&c.mount);
    for (const JPNozzleConfig& n : m_config.nozzles)     if (n.mount.headId == headId) mounts.push_back(&n.mount);
    for (const JPActuatorConfig& a : m_config.actuators) if (a.mount.headId == headId) mounts.push_back(&a.mount);

    // Up out of harm's way first.
    if (!doSafeZ(headId, speed, why)) return false;

    // Then the head, by the tool that marks its place: its camera, else the first on X and Y.
    const JPMountConfig* by = nullptr;
    for (const JPMountConfig* m : mounts)
        if (!by && !m->axisX.empty() && !m->axisY.empty()) by = m;
    if (!by) { why = "nothing on the head moves on X and Y"; return false; }
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": parking " << head->name;
    // A park place is often at the end of travel: out of the way is the
    // point, so it goes as near as the soft limits allow (in the axes' own
    // coordinates, where the limits are) rather than not at all.
    std::map<std::string, double> target = { { by->axisX, head->park->x - by->offsetX },
                                             { by->axisY, head->park->y - by->offsetY } };
    std::map<std::string, double> axes = toAxes(target, jogBase());
    bool clamped = false;
    for (auto& [id, t] : axes) {
        const JPAxisConfig* a = m_config.axis(id);
        if (!a) continue;
        const double was = t;
        if (a->softLimitLowEnabled)  t = std::max(t, a->softLimitLow);
        if (a->softLimitHighEnabled) t = std::min(t, a->softLimitHigh);
        clamped = clamped || t != was;
    }
    if (clamped) {
        JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": the park place is past a soft limit; parking at the limit";
        target = axes;
        if (const JPSquarenessConfig& q = m_config.squareness; q.active() && target.count(q.axisX) && target.count(q.axisY))
            target[q.axisX] += q.xPerY * (target[q.axisY] - q.atY);   // back to square coordinates for the move
    }
    return doMove(target, speed, why);
}

bool JPCell::safeZAndWait(const std::string& headId, double speed, std::string& why) {
    if (m_moving.exchange(true)) {
        why = "another move is under way";
        return false;
    }
    std::promise<std::pair<bool, std::string>> done;
    auto result = done.get_future();
    m_thread.post([this, headId, speed, &done] {
        std::string w;
        const bool ok = doSafeZ(headId, speed, w);
        m_moving = false;
        onMotion.emit(ok, w);
        done.set_value({ ok, w });
    });
    const auto [ok, w] = result.get();
    why = w;
    return ok;
}

bool JPCell::doSafeZ(const std::string& headId, double speed, std::string& why) {
    std::vector<const JPMountConfig*> mounts;
    for (const JPCameraConfig& c : m_config.cameras)     if (c.mount.headId == headId) mounts.push_back(&c.mount);
    for (const JPNozzleConfig& n : m_config.nozzles)     if (n.mount.headId == headId) mounts.push_back(&n.mount);
    for (const JPActuatorConfig& a : m_config.actuators) if (a.mount.headId == headId) mounts.push_back(&a.mount);
    const auto now = jogBase();
    std::map<std::string, double> safe;
    for (const JPMountConfig* m : mounts) {
        const JPAxisConfig* z = m_config.axis(m->axisZ);
        if (z && z->kind == JPAxisConfig::Kind::Mapped) z = m_config.axis(z->inputAxisId);
        if (!z || z->kind != JPAxisConfig::Kind::Controller || !now.count(z->id)) continue;
        double t = now.at(z->id);
        if (z->safeZoneLowEnabled)  t = std::max(t, z->safeZoneLow);
        if (z->safeZoneHighEnabled) t = std::min(t, z->safeZoneHigh);
        if (t != now.at(z->id)) safe[z->id] = t;
    }
    return safe.empty() || doMove(safe, speed, why);
}

void JPCell::switchActuator(const std::string& actuatorId, bool on) {
    m_thread.post([this, actuatorId, on] {
        std::string why;
        const bool ok = doSwitch(actuatorId, on, why);
        onActuator.emit(actuatorId, ok, ok ? (on ? "on" : "off") : why);
    });
}

bool JPCell::switchActuatorAndWait(const std::string& actuatorId, bool on, std::string& why) {
    std::promise<std::pair<bool, std::string>> done;
    auto result = done.get_future();
    m_thread.post([this, actuatorId, on, &done] {
        std::string w;
        const bool ok = doSwitch(actuatorId, on, w);
        onActuator.emit(actuatorId, ok, ok ? (on ? "on" : "off") : w);
        done.set_value({ ok, w });
    });
    const auto [ok, w] = result.get();
    why = w;
    return ok;
}

bool JPCell::doSwitch(const std::string& actuatorId, bool on, std::string& why) {
    for (const JPActuatorConfig& a : m_config.actuators) {
        if (a.id != actuatorId) continue;
        JPGcodeDriver* d = driver(a.driverId);
        const std::string& tmpl = on ? a.onCommand : a.offCommand;
        if (!d || tmpl.empty()) {
            why = a.name + " cannot be switched " + (on ? "on" : "off");
            return false;
        }
        const JPReply r = d->send(JPFirmwareProfile::fill(tmpl, { { "index", a.index } })).get();
        JLOGC(JPlacerLog::kCell, r.ok ? JLogLevel::Info : JLogLevel::Warn)
            << a.name << " " << (on ? "on" : "off") << (r.ok ? std::string() : ": " + r.error);
        why = r.error;
        return r.ok;
    }
    why = "no actuator " + actuatorId;
    return false;
}

void JPCell::readActuator(const std::string& actuatorId) {
    m_thread.post([this, actuatorId] {
        for (const JPActuatorConfig& a : m_config.actuators) {
            if (a.id != actuatorId) continue;
            JPGcodeDriver* d = driver(a.driverId);
            if (!d || !a.canRead()) {
                onActuator.emit(a.id, false, a.name + " cannot be read");
                return;
            }
            std::regex pattern;
            try {
                pattern = std::regex(a.readPattern);
            } catch (const std::regex_error&) {
                onActuator.emit(a.id, false, a.name + ": its read pattern is not a valid pattern");
                return;
            }
            const JPReply r = d->send(JPFirmwareProfile::fill(a.readCommand, { { "index", a.index } })).get();
            if (!r.ok) {
                onActuator.emit(a.id, false, r.error);
                return;
            }
            for (const std::string& line : r.lines) {
                std::smatch m;
                if (std::regex_search(line, m, pattern) && m.size() > 1) {
                    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << a.name << " read " << m[1].str() << (a.unit.empty() ? std::string() : " " + a.unit);
                    onActuator.emit(a.id, true, m[1].str());
                    return;
                }
            }
            JLOGC(JPlacerLog::kCell, JLogLevel::Warn) << a.name << ": no value in the reply to '" << a.readCommand
                                                       << "' (pattern " << a.readPattern << ")";
            onActuator.emit(a.id, false, a.name + ": the reply held no value");
            return;
        }
    });
}

void JPCell::updatePositions(const std::string& driverId, const JPFirmwareProfile::Status& status) {
    std::map<std::string, double> snapshot;
    bool stateChanged = false;
    {
        std::lock_guard lk(m_mutex);
        std::string& state = m_states[driverId];
        stateChanged = state != status.state;
        state = status.state;
        for (const JPAxisConfig& a : m_config.axes) {
            if (a.kind != JPAxisConfig::Kind::Controller || a.driverId != driverId) continue;
            const auto it = status.positions.find(a.letter);
            if (it != status.positions.end()) m_axisPositions[a.id] = m_positions[a.id] = it->second;
        }
        // Square coordinates from the axes' own: X takes back the Y axis's lean.
        if (const JPSquarenessConfig& q = m_config.squareness; q.active()) {
            const auto x = m_axisPositions.find(q.axisX), y = m_axisPositions.find(q.axisY);
            if (x != m_axisPositions.end() && y != m_axisPositions.end())
                m_positions[q.axisX] = x->second + q.xPerY * (y->second - q.atY);
        }
        for (const JPAxisConfig& a : m_config.axes) {
            if (a.kind != JPAxisConfig::Kind::Mapped) continue;
            const auto in = m_positions.find(a.inputAxisId);
            if (in == m_positions.end()) continue;
            if (const auto out = a.mapped(in->second)) m_positions[a.id] = *out;
        }
        snapshot = m_positions;
    }
    if (stateChanged) {
        JLOGC(JPlacerLog::kCell, JLogLevel::Info) << "controller " << driverId << " is " << status.state;
        onState.emit(driverId, status.state);
    }
    onPositions.emit(snapshot);
}

bool JPCell::inAlarm() const {
    std::lock_guard lk(m_mutex);
    for (const auto& d : m_drivers) {
        const JPFirmwareProfile* p = d->profile();
        const auto s = m_states.find(d->id());
        if (p && !p->alarmState().empty() && s != m_states.end() && s->second == p->alarmState()) return true;
    }
    return false;
}

std::map<std::string, std::string> JPCell::states() const {
    std::lock_guard lk(m_mutex);
    return m_states;
}

void JPCell::home() {
    m_thread.post([this] {
        std::string why;
        m_homing = true;
        const bool ok = doHome(why);
        m_homing = false;
        if (ok) {
            m_homed = true;
            onHomed.emit(true);
        }
        onMotion.emit(ok, why);
    });
}

bool JPCell::doHome(std::string& why) {
    if (!m_connected) { why = "not connected"; return false; }
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": homing";
    m_homed = false;
    onHomed.emit(false);
    for (const auto& d : m_drivers) {
        const JPReply r = d->command("home");
        if (!r.ok) { why = d->config().name + ": homing failed (" + r.error + ")"; return false; }
        const JPReply w = d->waitForMotion();
        if (!w.ok) { why = d->config().name + ": homing did not finish (" + w.error + ")"; return false; }
        // Where the axes are now: their home coordinates.
        std::string axes;
        for (const JPAxisConfig& a : m_config.axes)
            if (a.kind == JPAxisConfig::Kind::Controller && a.driverId == d->id())
                axes += (axes.empty() ? "" : " ") + a.letter + format(a.homeCoordinate, d->profile()->decimals());
        if (!axes.empty()) {
            const JPReply p = d->command("setPosition", { { "axes", axes } });
            if (!p.ok) { why = d->config().name + ": home coordinates not set (" + p.error + ")"; return false; }
            // Homed means the positions shown are the home coordinates: a report from after.
            const JPReply r = d->waitForMotion();
            if (!r.ok) { why = d->config().name + ": " + r.error; return false; }
        }
    }
    {
        std::lock_guard lk(m_mutex);
        m_sent.clear();
        m_corrected.clear();
        for (const JPAxisConfig& a : m_config.axes) {
            if (a.kind == JPAxisConfig::Kind::Virtual) m_positions[a.id] = a.homeCoordinate;
            if (a.kind != JPAxisConfig::Kind::Mapped) m_sent[a.id] = a.homeCoordinate;
        }
        // The home coordinates are the axes' own; squarely, X takes the lean.
        if (const JPSquarenessConfig& q = m_config.squareness; q.active() && m_sent.count(q.axisX) && m_sent.count(q.axisY))
            m_sent[q.axisX] += q.xPerY * (m_sent[q.axisY] - q.atY);
    }
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": homed";
    return true;
}

void JPCell::jog(const std::string& toolId, double dx, double dy, double dz, double drot, double speed) {
    const JPMountConfig* mount = nullptr;
    for (const JPNozzleConfig& n : m_config.nozzles)     if (n.id == toolId) mount = &n.mount;
    for (const JPCameraConfig& c : m_config.cameras)     if (c.id == toolId) mount = &c.mount;
    for (const JPActuatorConfig& a : m_config.actuators) if (a.id == toolId) mount = &a.mount;
    if (!mount) return;
    const auto now = jogBase();
    std::map<std::string, double> targets;
    auto add = [&](const std::string& axis, double delta) {
        if (axis.empty() || delta == 0) return;
        const auto p = now.find(axis);
        if (p != now.end()) targets[axis] = p->second + delta;
    };
    add(mount->axisX, dx);
    add(mount->axisY, dy);
    add(mount->axisZ, dz);
    add(mount->axisRotation, drot);
    if (!targets.empty()) moveAxes(std::move(targets), speed);
}

bool JPCell::moveAxesAndWait(std::map<std::string, double> targets, double speed, std::string& why) {
    if (m_moving.exchange(true)) {
        why = "another move is under way";
        return false;
    }
    std::promise<std::pair<bool, std::string>> done;
    auto result = done.get_future();
    m_thread.post([this, targets = std::move(targets), speed, &done] {
        std::string w;
        const bool ok = doMove(targets, speed, w);
        m_moving = false;
        onMotion.emit(ok, w);
        done.set_value({ ok, w });
    });
    const auto [ok, w] = result.get();
    why = w;
    return ok;
}

bool JPCell::correctPosition(const std::map<std::string, double>& by, std::string& why) {
    if (m_moving.exchange(true)) {
        why = "a move is under way";
        return false;
    }
    std::promise<std::pair<bool, std::string>> done;
    auto result = done.get_future();
    m_thread.post([this, &by, &done] {
        std::string w;
        const bool ok = doCorrectPosition(by, w);
        m_moving = false;
        done.set_value({ ok, w });
    });
    const auto [ok, w] = result.get();
    why = w;
    return ok;
}

bool JPCell::doCorrectPosition(const std::map<std::string, double>& by, std::string& why) {
    if (!m_connected) { why = "not connected"; return false; }
    if (!m_homed)     { why = "not homed: home the machine first"; return false; }
    const auto base = jogBase();
    std::map<std::string, double> renamed;   // where the machine is now, by its new square coordinates
    for (const auto& [id, d] : by) {
        const JPAxisConfig* a = m_config.axis(id);
        if (!a || a->kind != JPAxisConfig::Kind::Controller) {
            why = "axis " + (a ? a->name : id) + " is not a controller's: its position cannot be corrected";
            return false;
        }
        renamed[id] = base.at(id) - d;
    }
    std::map<std::string, std::string> words;   // by controller, in the axes' own coordinates
    for (const auto& [id, v] : toAxes(renamed, base)) {
        const JPAxisConfig* a = m_config.axis(id);
        JPGcodeDriver* dr = driver(a->driverId);
        if (!dr) { why = "no controller " + a->driverId; return false; }
        std::string& w = words[a->driverId];
        w += (w.empty() ? "" : " ") + a->letter + format(v, dr->profile()->decimals());
    }
    for (const auto& [driverId, axes] : words) {
        JPGcodeDriver* dr = driver(driverId);
        const JPReply p = dr->command("setPosition", { { "axes", axes } });
        if (!p.ok) { why = dr->config().name + ": position not set (" + p.error + ")"; return false; }
        // Its next report says the new coordinates.
        const JPReply w = dr->waitForMotion();
        if (!w.ok) { why = dr->config().name + ": " + w.error; return false; }
    }
    std::lock_guard lk(m_mutex);
    for (const auto& [id, d] : by) {
        if (const auto s = m_sent.find(id); s != m_sent.end()) s->second -= d;
        m_corrected[id] += d;
    }
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": position corrected";
    return true;
}

std::map<std::string, double> JPCell::toAxes(std::map<std::string, double> square,
                                             const std::map<std::string, double>& now) const {
    const JPSquarenessConfig& q = m_config.squareness;
    if (!q.active() || (!square.count(q.axisX) && !square.count(q.axisY))) return square;
    const double x = square.count(q.axisX) ? square.at(q.axisX) : now.at(q.axisX);
    const double y = square.count(q.axisY) ? square.at(q.axisY) : now.at(q.axisY);
    square[q.axisX] = x - q.xPerY * (y - q.atY);
    return square;
}

void JPCell::setSquareness(const JPSquarenessConfig& squareness) {
    m_thread.post([this, squareness] {
        {
            std::lock_guard lk(m_mutex);
            m_config.squareness = squareness;
        }
        m_homed = false;
        onHomed.emit(false);
        onCalibration.emit();
        JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": squareness " << squareness.xPerY
                                                  << " mm of X a mm of Y; home again";
    });
}

std::map<std::string, double> JPCell::correctionSinceHome() const {
    std::lock_guard lk(m_mutex);
    return m_corrected;
}

void JPCell::setCameraCalibration(const std::string& cameraId, const JPCameraCalibration& calibration) {
    {
        std::lock_guard lk(m_mutex);
        for (JPCameraConfig& c : m_config.cameras)
            if (c.id == cameraId) c.keepCalibration(calibration);
    }
    onCalibration.emit();
}

JPSquarenessConfig JPCell::squareness() const {
    std::lock_guard lk(m_mutex);
    return m_config.squareness;
}

JPCameraCalibration JPCell::cameraCalibration(const std::string& cameraId, int width, int height) const {
    std::lock_guard lk(m_mutex);
    for (const JPCameraConfig& c : m_config.cameras)
        if (c.id == cameraId)
            if (const JPCameraCalibration* k = c.calibrationFor(width, height)) return *k;
    return {};
}

std::vector<JPCameraCalibration> JPCell::cameraCalibrations(const std::string& cameraId) const {
    std::lock_guard lk(m_mutex);
    for (const JPCameraConfig& c : m_config.cameras)
        if (c.id == cameraId) return c.calibrations;
    return {};
}

void JPCell::moveTool(const JPMountConfig& mount, std::array<std::optional<double>, 4> to, double speed) {
    if (m_moving.exchange(true)) {
        JLOGC(JPlacerLog::kCell, JLogLevel::Debug) << "move refused: one is under way";
        return;
    }
    m_thread.post([this, mount, to, speed] {
        std::string why;
        bool ok = mount.headId.empty() || doSafeZ(mount.headId, speed, why);
        // Tool = axis + its offset on the head.
        std::map<std::string, double> across;
        if (to[0] && !mount.axisX.empty()) across[mount.axisX] = *to[0] - mount.offsetX;
        if (to[1] && !mount.axisY.empty()) across[mount.axisY] = *to[1] - mount.offsetY;
        if (to[3] && !mount.axisRotation.empty()) across[mount.axisRotation] = *to[3];
        if (ok && !across.empty()) ok = doMove(across, speed, why);
        if (ok && to[2] && !mount.axisZ.empty()) ok = doMove({ { mount.axisZ, *to[2] - mount.offsetZ } }, speed, why);
        m_moving = false;
        if (!ok) JLOGC(JPlacerLog::kCell, JLogLevel::Warn) << "move refused: " << why;
        onMotion.emit(ok, why);
    });
}

void JPCell::moveAxes(std::map<std::string, double> targets, double speed) {
    if (m_moving.exchange(true)) {
        JLOGC(JPlacerLog::kCell, JLogLevel::Debug) << "move refused: one is under way";
        return;
    }
    m_thread.post([this, targets = std::move(targets), speed] {
        std::string why;
        const bool ok = doMove(targets, speed, why);
        m_moving = false;
        if (!ok) JLOGC(JPlacerLog::kCell, JLogLevel::Warn) << "move refused: " << why;
        onMotion.emit(ok, why);
    });
}

bool JPCell::doMove(std::map<std::string, double> targets, double speed, std::string& why) {
    if (!m_connected) { why = "not connected"; return false; }
    if (!m_homed)     { why = "not homed: home the machine first"; return false; }

    // A rotation goes the short way round (wrap around), or is kept to
    // -180..180 (limit to range), as its axis is set.
    const auto start = jogBase();
    for (auto& [id, t] : targets) {
        const JPAxisConfig* a = m_config.axis(id);
        if (!a || a->type != JPAxisConfig::Type::Rotation || a->kind != JPAxisConfig::Kind::Controller) continue;
        if (const auto cur = start.find(id); a->wrapAroundRotation && cur != start.end())
            t = cur->second + std::remainder(t - cur->second, 360.0);
        else if (a->limitRotation)
            t = std::remainder(t, 360.0);
    }

    // Down to controller axes: a mapped axis becomes its input axis's target,
    // a virtual axis just takes its coordinate.
    std::map<std::string, double> hardware;   // controller axis id -> target
    std::map<std::string, double> virtuals;
    for (const auto& [id, target] : targets) {
        const JPAxisConfig* a = m_config.axis(id);
        if (!a) { why = "no axis " + id; return false; }
        if (a->kind == JPAxisConfig::Kind::Virtual) { virtuals[id] = target; continue; }
        const JPAxisConfig* hw = a;
        double t = target;
        if (a->kind == JPAxisConfig::Kind::Mapped) {
            const auto in = a->unmapped(target);
            hw = m_config.axis(a->inputAxisId);
            if (!in || !hw) { why = "axis " + a->name + " cannot be moved through its map"; return false; }
            t = *in;
        }
        if (hw->kind != JPAxisConfig::Kind::Controller) { why = "axis " + a->name + " has no controller behind it"; return false; }
        if (const auto prev = hardware.find(hw->id); prev != hardware.end() && std::abs(prev->second - t) > 1e-9) {
            why = "axis " + hw->name + " was asked to be in two places at once";
            return false;
        }
        hardware[hw->id] = t;
    }
    // From here on the axes' own coordinates (leaned, where the gantry is not
    // square); the soft limits are theirs.
    const std::map<std::string, double> square = hardware;
    const auto now = jogBase();
    hardware = toAxes(hardware, now);
    // Whole steps of an axis with a resolution: where it can actually stop.
    for (auto& [id, t] : hardware)
        if (const double r = m_config.axis(id)->resolution; r > 0) t = std::round(t / r) * r;
    for (const auto& [id, t] : hardware) {
        const JPAxisConfig* hw = m_config.axis(id);
        if ((hw->softLimitLowEnabled && t < hw->softLimitLow) || (hw->softLimitHighEnabled && t > hw->softLimitHigh)) {
            why = "axis " + hw->name + " would go to " + format(t, 3) + ", outside its soft limits ("
                + format(hw->softLimitLow, 3) + " to " + format(hw->softLimitHigh, 3) + ")";
            return false;
        }
    }

    // Backlash: an axis that would end travelling the wrong way goes past
    // its target by the offset first; then everything comes in to the
    // targets at the slowest backlash speed among those that went past.
    std::map<std::string, double> overshoot = hardware;
    double approach = 1;
    bool needApproach = false;
    {
        const auto from = toAxes(now, now);
        for (auto& [id, t] : overshoot) {
            const JPAxisConfig* a = m_config.axis(id);
            if (a->backlash != JPAxisConfig::Backlash::OneSided || a->backlashOffset == 0) continue;
            const auto f = from.find(id);
            const double travel = t - (f == from.end() ? t : f->second);
            // Ending travel must be opposite to the offset's sign; a move of
            // nothing, or the wrong way, goes past first.
            if (travel != 0 && (travel > 0) == (a->backlashOffset < 0)) continue;
            t += a->backlashOffset;
            needApproach = true;
            approach = std::min(approach, a->backlashSpeedFactor);
        }
    }

    // One move per controller, at the slowest axis's rate (mm or degrees per
    // minute: the axis's own, else what the controller stores).
    std::map<std::string, std::vector<const JPAxisConfig*>> byDriver;
    for (const auto& [id, t] : hardware) byDriver[m_config.axis(id)->driverId].push_back(m_config.axis(id));
    auto send = [&](const std::map<std::string, double>& to, double factor, std::vector<JPGcodeDriver*>& moved) {
        for (const auto& [driverId, axes] : byDriver) {
            JPGcodeDriver* d = driver(driverId);
            if (!d) { why = "no controller " + driverId; return false; }
            std::string words;
            double feed = 0;
            for (const JPAxisConfig* a : axes) {
                words += (words.empty() ? "" : " ") + a->letter + format(to.at(a->id), d->profile()->decimals());
                double rate = a->feedratePerSecond * 60;
                if (rate <= 0) rate = d->axisSetting("maxRate", a->letter).value_or(0);
                if (rate <= 0) { why = "axis " + a->name + " has no speed: neither the cell nor its controller gives one"; return false; }
                feed = feed <= 0 ? rate : std::min(feed, rate);
            }
            if (const double cap = d->config().maxFeedRate; cap > 0) feed = std::min(feed, cap);
            const double k = std::clamp(speed, 0.0, 1.0) * factor;
            feed *= k;
            std::map<std::string, std::string> values{ { "axes", words }, { "feed", format(feed, 0) } };
            // The slowest acceleration and jerk among the axes, for a command
            // that sets them ({acceleration}, {jerk}): scaled as the speed is,
            // so a slower move is the same move stretched in time.
            double accel = 0, jerk = 0;
            for (const JPAxisConfig* a : axes) {
                if (a->accelerationPerSecond2 > 0) accel = accel > 0 ? std::min(accel, a->accelerationPerSecond2) : a->accelerationPerSecond2;
                if (a->jerkPerSecond3 > 0) jerk = jerk > 0 ? std::min(jerk, a->jerkPerSecond3) : a->jerkPerSecond3;
            }
            if (accel > 0) values["acceleration"] = format(accel * k * k, 0);
            if (jerk > 0) values["jerk"] = format(jerk * k * k * k, 0);
            JLOGC(JPlacerLog::kCell, JLogLevel::Debug) << "move " << d->config().name << ": " << words << " F" << format(feed, 0);
            const JPReply r = d->command("move", values);
            if (!r.ok) { why = d->config().name + ": move refused (" + r.error + ")"; return false; }
            if (std::find(moved.begin(), moved.end(), d) == moved.end()) moved.push_back(d);
        }
        return true;
    };
    std::vector<JPGcodeDriver*> moved;
    if (needApproach && !send(overshoot, 1, moved)) return false;
    if (!send(hardware, needApproach ? approach : 1, moved)) return false;
    for (JPGcodeDriver* d : moved) {
        const JPReply w = d->waitForMotion();
        if (!w.ok) { why = d->config().name + ": move did not finish (" + w.error + ")"; return false; }
    }
    // A rotation that both wraps and is limited, gone past +-180 the short
    // way: its controller is told it is at the same angle within the range.
    std::map<std::string, double> rewrapped;
    for (const auto& [id, t] : hardware) {
        const JPAxisConfig* a = m_config.axis(id);
        if (a->type != JPAxisConfig::Type::Rotation || !a->wrapAroundRotation || !a->limitRotation || std::abs(t) <= 180) continue;
        JPGcodeDriver* d = driver(a->driverId);
        const double in = std::remainder(t, 360.0);
        const JPReply r = d->command("setPosition", { { "axes", a->letter + format(in, d->profile()->decimals()) } });
        if (!r.ok) JLOGC(JPlacerLog::kCell, JLogLevel::Warn) << a->name << ": could not bring the rotation back into range (" << r.error << ")";
        else rewrapped[id] = in;
    }
    std::lock_guard lk(m_mutex);
    for (const auto& [id, t] : rewrapped) {
        hardware[id] = t;
        m_positions[id] = t;
        if (targets.count(id)) targets[id] = t;
    }
    for (const auto& [id, t] : virtuals) m_positions[id] = t;
    for (const auto& [id, t] : targets) m_sent[id] = t;
    for (const auto& [id, t] : square)  m_sent[id] = t;
    // An axis moved only to keep the gantry square is where it was, squarely.
    for (const auto& [id, t] : hardware)
        if (!square.count(id)) m_sent[id] = now.at(id);
    return true;
}

namespace {

// How far a reported position may sit from the commanded one and still be
// "there": a few motor steps on any machine that places parts.
constexpr double kSettledTolerance = 0.05;

} // namespace

std::map<std::string, double> JPCell::jogBase() const {
    std::lock_guard lk(m_mutex);
    std::map<std::string, double> out = m_positions;
    for (const auto& [id, sent] : m_sent)
        if (const auto p = out.find(id); p != out.end() && std::abs(p->second - sent) <= kSettledTolerance)
            p->second = sent;
    return out;
}

std::map<std::string, double> JPCell::positions() const {
    std::lock_guard lk(m_mutex);
    return m_positions;
}

std::map<std::string, std::string> JPCell::firmware() const {
    std::lock_guard lk(m_mutex);
    return m_firmware;
}

} // inline namespace jf
