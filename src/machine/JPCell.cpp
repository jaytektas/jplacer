// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCell.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <regex>

inline namespace jf {

JPCell::JPCell(JPCellConfig config, std::vector<JPFirmwareProfile> profiles)
    : m_config(std::move(config)), m_profiles(std::move(profiles)) {
    for (const JPAxisConfig& a : m_config.axes) m_positions[a.id] = a.homeCoordinate;
    for (const JPDriverConfig& d : m_config.drivers) {
        auto driver = std::make_unique<JPGcodeDriver>(d, m_profiles);
        const std::string id = d.id, name = d.name;
        driver->onStatus.connect([this, id](JPFirmwareProfile::Status st) { updatePositions(id, st); });
        driver->onTraffic.connect([this, name](bool sent, std::string line) { onTraffic.emit(name, sent, line); });
        driver->onAlarm.connect([this, name](std::string what) { onAlarm.emit(name + ": " + what); });
        driver->onLost.connect([this, name](std::string why) {
            const std::string what = name + ": connection lost (" + why + ")";
            onAlarm.emit(what);
            m_thread.post([this, what] {
                doDisconnect();
                onConnection.emit(false, what);
            });
        });
        m_drivers.push_back(std::move(driver));
    }
}

JPCell::~JPCell() {
    m_thread.stop();
    doDisconnect();
}

JPGcodeDriver* JPCell::driver(const std::string& id) const {
    for (const auto& d : m_drivers) if (d->config().id == id) return d.get();
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
            m_firmware[d->config().id] = d->profile()->name();
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

void JPCell::switchActuator(const std::string& actuatorId, bool on) {
    m_thread.post([this, actuatorId, on] {
        for (const JPActuatorConfig& a : m_config.actuators) {
            if (a.id != actuatorId) continue;
            JPGcodeDriver* d = driver(a.driverId);
            const std::string& tmpl = on ? a.onCommand : a.offCommand;
            if (!d || tmpl.empty()) {
                onActuator.emit(a.id, false, a.name + " cannot be switched " + (on ? "on" : "off"));
                return;
            }
            const JPReply r = d->send(JPFirmwareProfile::fill(tmpl, { { "index", a.index } })).get();
            JLOGC(JPlacerLog::kCell, r.ok ? JLogLevel::Info : JLogLevel::Warn)
                << a.name << " " << (on ? "on" : "off") << (r.ok ? std::string() : ": " + r.error);
            onActuator.emit(a.id, r.ok, r.ok ? (on ? "on" : "off") : r.error);
            return;
        }
    });
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
            if (it != status.positions.end()) m_positions[a.id] = it->second;
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
        const auto s = m_states.find(d->config().id);
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
            if (a.kind == JPAxisConfig::Kind::Controller && a.driverId == d->config().id)
                axes += (axes.empty() ? "" : " ") + a.letter + format(a.homeCoordinate, d->profile()->decimals());
        if (!axes.empty()) {
            const JPReply p = d->command("setPosition", { { "axes", axes } });
            if (!p.ok) { why = d->config().name + ": home coordinates not set (" + p.error + ")"; return false; }
        }
    }
    {
        std::lock_guard lk(m_mutex);
        m_sent.clear();
        for (const JPAxisConfig& a : m_config.axes) {
            if (a.kind == JPAxisConfig::Kind::Virtual) m_positions[a.id] = a.homeCoordinate;
            if (a.kind != JPAxisConfig::Kind::Mapped) m_sent[a.id] = a.homeCoordinate;
        }
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
        if ((hw->softLimitLowEnabled && t < hw->softLimitLow) || (hw->softLimitHighEnabled && t > hw->softLimitHigh)) {
            why = "axis " + hw->name + " would go to " + format(t, 3) + ", outside its soft limits ("
                + format(hw->softLimitLow, 3) + " to " + format(hw->softLimitHigh, 3) + ")";
            return false;
        }
        hardware[hw->id] = t;
    }

    // Backlash: an axis that would end travelling the wrong way goes past
    // its target by the offset first; then everything comes in to the
    // targets at the slowest backlash speed among those that went past.
    std::map<std::string, double> overshoot = hardware;
    double approach = 1;
    bool needApproach = false;
    {
        const auto from = jogBase();
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
            feed *= std::clamp(speed, 0.0, 1.0) * factor;
            JLOGC(JPlacerLog::kCell, JLogLevel::Debug) << "move " << d->config().name << ": " << words << " F" << format(feed, 0);
            const JPReply r = d->command("move", { { "axes", words }, { "feed", format(feed, 0) } });
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
    std::lock_guard lk(m_mutex);
    for (const auto& [id, t] : virtuals) m_positions[id] = t;
    for (const auto& [id, t] : targets)  m_sent[id] = t;
    for (const auto& [id, t] : hardware) m_sent[id] = t;
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
