// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCell.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>
#include <j/io/HttpClient.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <future>
#include <regex>
#include <chrono>
#include <thread>

inline namespace jf {

namespace {
// OpenPnP's Pick & Place Checking: not for its test object, nor this near the discard location.
constexpr const char* kTestObjectPartId = "TEST-OBJECT";
constexpr double kDiscardNearMm = 4.0;
} // namespace

JPCell::JPCell(JPCellConfig config, std::vector<JPFirmwareProfile> profiles)
    : m_config(std::move(config)), m_profiles(std::move(profiles)) {
    for (const JPAxisConfig& a : m_config.axes) m_positions[a.id] = a.homeCoordinate;
    for (const JPDriverConfig& d : m_config.drivers) m_drivers.push_back(makeDriver(asRun(d, m_config)));
    m_thread.post([this] { m_threadId = std::this_thread::get_id(); });
    // What each actuator was switched to; nothing known once the connection goes.
    onActuator.connect([this](std::string id, bool ok, std::string value) {
        if (!ok || (value != "on" && value != "off")) return;
        std::lock_guard lk(m_mutex);
        m_switchedOn[id] = value == "on";
    });
    onConnection.connect([this](bool, std::string) {
        std::lock_guard lk(m_mutex);
        m_switchedOn.clear();
    });
}

JPSimulationConfig JPCell::simulation() const {
    std::lock_guard lk(m_mutex);
    return m_config.simulation;
}

std::optional<bool> JPCell::switchedOn(const std::string& actuatorId) const {
    std::lock_guard lk(m_mutex);
    const auto it = m_switchedOn.find(actuatorId);
    return it == m_switchedOn.end() ? std::nullopt : std::optional(it->second);
}

JPDriverConfig JPCell::asRun(const JPDriverConfig& driver, const JPCellConfig& cell) {
    if (!cell.simulation.replacesDrivers()) return driver;
    JPDriverConfig run = driver;
    JJson letters = JJson::array();
    for (const JPAxisConfig& a : cell.axes)
        if (a.kind == JPAxisConfig::Kind::Controller && a.driverId == driver.id && !a.letter.empty()) letters.push(a.letter);
    run.link = JJson::object();
    run.link["type"] = std::string("simulated");
    run.link["simulator"]["axisLetters"] = letters;
    // A grblHAL, as jplacer's own simulator is.
    run.link["simulator"]["identity"] = JJson::array();
    run.link["simulator"]["identity"].push(std::string("[VER:1.1f.20250101:]"));
    run.link["simulator"]["identity"].push(std::string("[FIRMWARE:grblHAL]"));
    return run;
}

std::unique_ptr<JPGcodeDriver> JPCell::makeDriver(const JPDriverConfig& config) {
    auto driver = std::make_unique<JPGcodeDriver>(config, m_profiles);
    JPGcodeDriver* d = driver.get();   // its name as it is when it speaks: it can be renamed while it runs
    driver->onStatus.connect([this, d](JPFirmwareProfile::Status st) { updatePositions(d->id(), st); });
    driver->onTraffic.connect([this, d](bool sent, std::string line) { onTraffic.emit(d->config().name, sent, line); });
    driver->onAlarm.connect([this, d](std::string what) { onAlarm.emit(d->config().name + ": " + what); });
    driver->onPlaceLost.connect([this, d](std::string why) {
        onAlarm.emit(d->config().name + ": reset mid-move, " + why + "; home the machine again");
        if (m_homed.exchange(false)) onHomed.emit(false);
    });
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
                drivers.push_back(makeDriver(asRun(d, config)));
                continue;
            }
            (*old)->setConfig(asRun(d, config));
            drivers.push_back(std::move(*old));
        }
        for (auto& gone : m_drivers)
            if (gone) gone->disconnect();   // taken out of the cell
        m_drivers = std::move(drivers);

        // The axes as they were: the coordinates still mean what they did.
        // Only what says where an axis is counts: its speed, limits and
        // backlash do not change what its coordinates mean.
        auto where = [](const JPAxisConfig& a) {
            JJson j = JJson::object();
            j["id"] = a.id;
            j["kind"] = JPAxisConfig::kindName(a.kind);
            j["type"] = JPAxisConfig::typeName(a.type);
            j["home"] = a.homeCoordinate;
            j["driver"] = a.driverId;
            j["letter"] = a.letter;
            j["wrap"] = a.wrapAroundRotation;
            j["limit"] = a.limitRotation;
            j["input"] = a.inputAxisId;
            j["map"] = JJson::array();
            for (double v : { a.mapInput0, a.mapOutput0, a.mapInput1, a.mapOutput1 }) j["map"].push(v);
            j["cam"] = JJson::array();
            for (double v : { a.camRadius, a.camArmsAngle, a.camWheelRadius, a.camWheelGap, a.camClockwise ? 1.0 : 0.0 }) j["cam"].push(v);
            return j;
        };
        JJson axesBefore = JJson::array(), axesAfter = JJson::array();
        for (const JPAxisConfig& a : m_config.axes) axesBefore.push(where(a));
        for (const JPAxisConfig& a : config.axes) axesAfter.push(where(a));
        const bool axesChanged = axesBefore.dump() != axesAfter.dump();
        // A nozzle given another tip: its Z calibration (the old tip's) dropped, and with the new tip's
        // NozzleTipChange trigger, made again once this is done (OpenPnP's ensureZCalibrated on load).
        std::vector<std::string> recalibrate;
        std::map<std::string, std::string> tipBefore;
        for (const JPNozzleConfig& n : m_config.nozzles) tipBefore[n.id] = n.tipId;
        {
            std::lock_guard lk(m_mutex);
            m_config = std::move(config);
            for (const JPAxisConfig& a : m_config.axes) m_positions.try_emplace(a.id, a.homeCoordinate);
            for (const JPNozzleConfig& n : m_config.nozzles) {
                const auto was = tipBefore.find(n.id);
                if (was != tipBefore.end() && was->second == n.tipId) continue;
                m_zCalibration.erase(n.id);
                for (const JPNozzleTipConfig& t : m_config.nozzleTips)
                    if (t.id == n.tipId && n.contactProbe.on() && t.zCalibrationTrigger == "NozzleTipChange" && t.touchLocation && m_homed)
                        recalibrate.push_back(n.id);
            }
        }
        for (const std::string& id : recalibrate) calibrateZ(id, false);
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
            // Kept alive since the last disconnect: taken up as it is.
            if (d->isConnected()) {
                JLOGC(JPlacerLog::kCell, JLogLevel::Info) << d->config().name << ": kept alive, taken up again";
            } else if (!d->connect(error)) {
                JLOGC(JPlacerLog::kCell, JLogLevel::Error) << m_config.name << ": not connected: " << error;
                doDisconnect();
                onConnection.emit(false, error);
                return;
            }
            if (d->profile()->hasSettings() && !d->readSettings(error))
                JLOGC(JPlacerLog::kDriver, JLogLevel::Warn) << error;
            std::lock_guard lk(m_mutex);
            m_firmware[d->id()] = d->profile()->name();
            m_firmwareIdentity[d->id()] = d->identity();
        }
        m_connected = true;
        JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": connected";
        actuateFor(&JPActuatorConfig::enabledActuation);
        onConnection.emit(true, std::string());
    });
}

void JPCell::actuateFor(const std::string JPActuatorConfig::*state) {
    for (const JPActuatorConfig& a : m_config.actuators) {
        const std::string& what = a.*state;
        if (what != "ActuateOn" && what != "ActuateOff") continue;
        std::string why;
        switchTelling(a.id, what == "ActuateOn", why);
    }
}

void JPCell::disconnect() {
    m_thread.post([this] {
        const bool was = m_connected;
        if (was) actuateFor(&JPActuatorConfig::disabledActuation);   // as the machine is let go
        doDisconnect(true);
        if (was) onConnection.emit(false, std::string());
    });
}

std::string JPCell::format(double v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

void JPCell::doDisconnect(bool keepingAlive) {
    m_homed = false;
    m_inMotion.clear();
    m_streaming = false;
    m_pumpOn.clear();
    m_holding.clear();
    if (m_connected) JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": disconnected";
    for (const auto& d : m_drivers)
        if (!keepingAlive || !d->config().keepAlive) d->disconnect();   // OpenPnP's Keep Alive: left open
    m_connected = false;
    std::lock_guard lk(m_mutex);
    m_firmware.clear();
    m_firmwareIdentity.clear();
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
        const bool ok = finished(doPark(headId, speed, why), why);
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

const JPAxisConfig* JPCell::zMotor(const JPMountConfig& mount) const {
    const JPAxisConfig* z = m_config.axis(mount.axisZ);
    if (z && z->transformed()) z = m_config.axis(z->inputAxisId);
    return z && z->kind == JPAxisConfig::Kind::Controller ? z : nullptr;
}

std::vector<std::string> JPCell::nozzlesHomedWith(const std::string& nozzleId) const {
    const JPNozzleConfig* nozzle = nullptr;
    for (const JPNozzleConfig& n : m_config.nozzles) if (n.id == nozzleId) nozzle = &n;
    if (!nozzle) return {};
    std::vector<std::string> ids{ nozzleId };
    const JPAxisConfig* motor = zMotor(nozzle->mount);
    if (!motor) return ids;
    for (const JPNozzleConfig& n : m_config.nozzles)
        if (n.id != nozzleId && zMotor(n.mount) == motor) ids.push_back(n.id);
    return ids;
}

void JPCell::homeNozzle(const std::string& nozzleId, double speed) {
    if (m_moving.exchange(true)) return;
    m_thread.post([this, nozzleId, speed] {
        std::string why;
        bool ok = false;
        if (!m_connected || !m_homed) why = "not homed: home the machine first";
        else ok = doHomeNozzle(nozzleId, speed, why);
        ok = finished(ok, why);
        m_moving = false;
        onMotion.emit(ok, why);
    });
}

bool JPCell::doHomeNozzle(const std::string& nozzleId, double speed, std::string& why) {
    const JPNozzleConfig* nozzle = nullptr;
    for (const JPNozzleConfig& n : m_config.nozzles) if (n.id == nozzleId) nozzle = &n;
    if (!nozzle) { why = "there is no such nozzle"; return false; }
    if (!doCoordinate("WaitForStillstand", why)) return false;
    if (nozzle->homeCommand.find_first_not_of(" \t\r\n") == std::string::npos) {
        why = nozzle->name + " has no Z home command (Machine Setup, the nozzle's Homing tab)";
        return false;
    }
    const JPAxisConfig* motor = zMotor(nozzle->mount);
    JPGcodeDriver* d = motor ? driver(motor->driverId) : nullptr;
    if (!d) { why = nozzle->name + "'s Z is not on a controller's axis"; return false; }

    // Somewhere nothing is below before a Z goes anywhere unexpected.
    if (!doPark(nozzle->mount.headId, speed, why)) return false;

    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": homing " << nozzle->name << "'s Z (" << motor->name << ")";
    const JPReply r = d->sendLines(nozzle->homeCommand, d->config().homeTimeoutMs);
    if (!r.ok) { why = nozzle->name + ": Z homing failed (" + r.error + ")"; return false; }
    const JPReply w = d->waitForMotion();
    if (!w.ok) { why = nozzle->name + ": Z homing did not finish (" + w.error + ")"; return false; }
    const JPReply p = d->command("setPosition", { { "axes", word(*motor, motor->homeCoordinate, *d) } });
    if (!p.ok) { why = nozzle->name + ": Z home coordinate not set (" + p.error + ")"; return false; }
    const JPReply after = d->waitForMotion();
    if (!after.ok) { why = d->config().name + ": " + after.error; return false; }
    {
        std::lock_guard lk(m_mutex);
        m_sent[motor->id] = motor->homeCoordinate;
        m_corrected.erase(motor->id);
        m_backlashApplied.erase(motor->id);
        m_backlashLag.erase(motor->id);
    }
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": " << nozzle->name << "'s Z homed";
    return true;
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
        const bool ok = finished(doSafeZ(headId, speed, w), w);
        m_moving = false;
        onMotion.emit(ok, w);
        done.set_value({ ok, w });
    });
    const auto [ok, w] = result.get();
    why = w;
    return ok;
}

void JPCell::setNozzlePart(const std::string& nozzleId, const PartOnNozzle& part) {
    m_thread.post([this, nozzleId, part] {
        if (!part.partId.empty() || part.heightMm != 0 || part.pickVacuumLevel != 0 || part.placeBlowOffLevel != 0)
            m_nozzleParts[nozzleId] = part;
        else m_nozzleParts.erase(nozzleId);
        // No part, no rotation mode offset (OpenPnP's setPart(null)).
        if (part.partId.empty()) setRotationModeOffset(nozzleId, std::nullopt);
    });
}

void JPCell::setRotationModeOffset(const std::string& nozzleId, std::optional<double> offset) {
    // In turn with the moves and the part given before and after it.
    auto set = [this, nozzleId, offset] {
        std::lock_guard lk(m_mutex);
        if (offset) m_rotationModeOffset[nozzleId] = *offset;
        else m_rotationModeOffset.erase(nozzleId);
        JLOGC(JPlacerLog::kCell, JLogLevel::Debug) << "nozzle " << nozzleId << ": rotation mode offset "
                                                   << (offset ? std::to_string(*offset) + "°" : std::string("none"));
    };
    if (onCellThread()) set();
    else m_thread.post(set);
}

double JPCell::rotationModeOffset(const std::string& nozzleId) const {
    std::lock_guard lk(m_mutex);
    const auto it = m_rotationModeOffset.find(nozzleId);
    return it == m_rotationModeOffset.end() ? 0.0 : it->second;
}

double JPCell::rotationModeOffsetOf(const JPMountConfig& mount) const {
    const JPNozzleConfig* n = nozzleOf(mount);
    return n ? rotationModeOffset(n->id) : 0.0;
}

const JPNozzleConfig* JPCell::nozzleOf(const JPMountConfig& mount) const {
    for (const JPNozzleConfig& n : m_config.nozzles) {
        const JPMountConfig& m = n.mount;
        if (m.headId == mount.headId && m.axisX == mount.axisX && m.axisY == mount.axisY && m.axisZ == mount.axisZ
            && m.offsetX == mount.offsetX && m.offsetY == mount.offsetY && m.offsetZ == mount.offsetZ)
            return &n;
    }
    return nullptr;
}

double JPCell::zOffsetOf(const JPMountConfig& mount) const {
    // A nozzle's tip Z calibration (OpenPnP's toHeadLocation): the tip met lower than it should,
    // the nozzle goes that much lower everywhere.
    if (const JPNozzleConfig* n = nozzleOf(mount)) {
        std::lock_guard lk(m_mutex);
        if (const auto c = m_zCalibration.find(n->id); c != m_zCalibration.end()) return mount.offsetZ + c->second.offsetMm;
    }
    return mount.offsetZ;
}

std::optional<std::array<double, 2>> JPCell::slotOffset(const std::string& tipId) const {
    std::lock_guard lk(m_mutex);
    const auto o = m_slotOffsets.find(tipId);
    if (o == m_slotOffsets.end()) return std::nullopt;
    return o->second;
}

void JPCell::setSlotOffset(const std::string& tipId, std::optional<std::array<double, 2>> offset) {
    std::lock_guard lk(m_mutex);
    if (offset) m_slotOffsets[tipId] = *offset;
    else m_slotOffsets.erase(tipId);
}

std::optional<double> JPCell::zCalibration(const std::string& nozzleId) const {
    std::lock_guard lk(m_mutex);
    const auto c = m_zCalibration.find(nozzleId);
    if (c == m_zCalibration.end()) return std::nullopt;
    return c->second.offsetMm;
}

void JPCell::calibrateZ(const std::string& nozzleId, bool reset) {
    m_thread.post([this, nozzleId, reset] {
        std::string why;
        bool ok = true;
        if (reset) {
            std::lock_guard lk(m_mutex);
            m_zCalibration.erase(nozzleId);
        } else {
            for (const JPNozzleConfig& n : m_config.nozzles)
                if (n.id == nozzleId) ok = finished(doCalibrateZ(n, why), why);
        }
        if (!ok) onAlarm.emit(why);
        onCalibration.emit();
    });
}

bool JPCell::doCalibrateZ(const JPNozzleConfig& n, std::string& why) {
    const JPNozzleTipConfig* tip = nullptr;
    for (const JPNozzleTipConfig& t : m_config.nozzleTips) if (t.id == n.tipId) tip = &t;
    if (!tip) { why = "Nozzle " + n.name + " has no nozzle tip loaded."; return false; }
    if (!tip->touchLocation) { why = "Nozzle tip " + tip->name + " has no touch location configured."; return false; }
    if (!n.contactProbe.on()) { why = "Nozzle " + n.name + " has contact probing disabled."; return false; }
    {
        std::lock_guard lk(m_mutex);
        m_zCalibration.erase(n.id);
    }
    const JPMachineLocation& at = *tip->touchLocation;
    const JPNozzleConfig::ContactProbe& p = n.contactProbe;
    double probed = 0;
    if (!doContactProbeCycle(n, at, probed, why)) return false;
    const double offset = at.z - probed;
    if (std::abs(offset) > p.maxZOffsetMm) {
        why = "Nozzle " + n.name + " nozzle tip " + tip->name + " Z calibration offset " + format(offset, 3)
            + " mm unexpectedly large. Check setup.";
        return false;
    }
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << n.name << ": nozzle tip " << tip->name << " Z calibration offset " << format(offset, 3);
    std::lock_guard lk(m_mutex);
    m_zCalibration[n.id] = { tip->id, offset };
    return true;
}

bool JPCell::doContactProbeCycle(const JPNozzleConfig& n, const JPMachineLocation& at, double& probedZ, std::string& why) {
    // OpenPnP's contactProbeCycle: above the place by the start offset (by way of safe Z), probed, retracted.
    const JPNozzleConfig::ContactProbe& p = n.contactProbe;
    double ignored = 0;
    return doMoveTool(n.mount, { at.x, at.y, at.z + p.startOffsetMm, std::nullopt }, 1.0, true, why)
        && doContactProbe(n, true, p.depthMm, probedZ, why) && doContactProbe(n, false, p.depthMm, ignored, why);
}

bool JPCell::contactProbeCycleAndWait(const std::string& nozzleId, const JPMachineLocation& at, bool resetZCalibration,
                                      double& probedZ, std::string& why) {
    return waitFor([&](std::string& w) {
        for (const JPNozzleConfig& n : m_config.nozzles) {
            if (n.id != nozzleId) continue;
            if (resetZCalibration) {
                std::lock_guard lk(m_mutex);
                m_zCalibration.erase(n.id);
            }
            return doContactProbeCycle(n, at, probedZ, w);
        }
        w = "no nozzle " + nozzleId;
        return false;
    }, why);
}

bool JPCell::calibrateZAndWait(const std::string& nozzleId, std::string& why) {
    return waitFor([&](std::string& w) {
        for (const JPNozzleConfig& n : m_config.nozzles)
            if (n.id == nozzleId) {
                const bool ok = doCalibrateZ(n, w);
                onCalibration.emit();
                return ok;
            }
        w = "no nozzle " + nozzleId;
        return false;
    }, why);
}

bool JPCell::doZCalibrationsAfterHoming(std::string& why) {
    // OpenPnP's ContactProbeNozzle.home: each probing nozzle's tip calibrated again, unless by hand only.
    for (const JPNozzleConfig& n : m_config.nozzles) {
        const JPNozzleTipConfig* tip = nullptr;
        for (const JPNozzleTipConfig& t : m_config.nozzleTips) if (t.id == n.tipId) tip = &t;
        if (!tip || !n.contactProbe.on() || tip->zCalibrationTrigger == "Manual") continue;
        std::string w;
        if (doCalibrateZ(n, w)) continue;
        if (tip->zCalibrationFailHoming) {
            why = w;
            return false;
        }
        JLOGC(JPlacerLog::kCell, JLogLevel::Warn) << w;
        onAlarm.emit(w);
    }
    return true;
}

double JPCell::dynamicLift(const JPMountConfig& mount) const {
    // Only a nozzle on a Z of its own: on a shared one (mapped, a cam) raising it lowers the other.
    const JPAxisConfig* z = m_config.axis(mount.axisZ);
    if (!z || z->kind != JPAxisConfig::Kind::Controller) return 0;
    for (const JPNozzleConfig& n : m_config.nozzles) {
        const JPMountConfig& m = n.mount;
        const bool same = m.headId == mount.headId && m.axisX == mount.axisX && m.axisY == mount.axisY && m.axisZ == mount.axisZ
                       && m.offsetX == mount.offsetX && m.offsetY == mount.offsetY && m.offsetZ == mount.offsetZ;
        if (!same || !n.dynamicSafeZ) continue;
        if (const auto h = m_nozzleParts.find(n.id); h != m_nozzleParts.end()) return h->second.heightMm;
    }
    return 0;
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
        if (z && z->transformed()) z = m_config.axis(z->inputAxisId);
        if (!z || z->kind != JPAxisConfig::Kind::Controller || !now.count(z->id)) continue;
        double t = now.at(z->id);
        // Dynamic Safe Z: the part's bottom at the zone's low end.
        if (z->safeZoneLowEnabled)  t = std::max(t, z->safeZoneLow + dynamicLift(*m));
        if (z->safeZoneHighEnabled) t = std::min(t, z->safeZoneHigh);
        if (t != now.at(z->id)) safe[z->id] = std::max(t, safe.count(z->id) ? safe[z->id] : t);
    }
    return safe.empty() || doMove(safe, speed, why);
}

void JPCell::setActuator(const std::string& actuatorId, const std::string& value) {
    m_thread.post([this, actuatorId, value] {
        std::string why;
        const bool ok = finished(doSet(actuatorId, value, why), why);
        onActuator.emit(actuatorId, ok, ok ? value : why);
    });
}

bool JPCell::doSet(const std::string& actuatorId, const std::string& value, std::string& why, int depth) {
    for (const JPActuatorConfig& a : m_config.actuators)
        if (a.id == actuatorId)
            return doCoordinate(a.coordinatedBeforeActuate, why) && doSetNow(a, value, why, depth)
                && doCoordinate(a.coordinatedAfterActuate, why);
    why = "no actuator " + actuatorId;
    return false;
}

bool JPCell::doSetNow(const JPActuatorConfig& a, const std::string& value, std::string& why, int depth) {
    const std::string& actuatorId = a.id;
    // A script actuator: its script told actuateDouble (a Double one) or actuateString.
    if (!a.scriptName.empty()) {
        JJson g = JJson::object();
        if (a.valueType == JPActuatorConfig::ValueType::Number) g["actuateDouble"] = std::strtod(value.c_str(), nullptr);
        else g["actuateString"] = value;
        return runActuatorScript(a, g, why);
    }
    // An HTTP actuator: its parameter URL, {val} the value.
    if (a.http.on) {
        if (a.http.paramUrl.empty()) {
            why = a.name + " has no parameter URL to set it by";
            return false;
        }
        std::string url = a.http.paramUrl;
        for (size_t at; (at = url.find("{val}")) != std::string::npos;) url.replace(at, 5, value);
        return httpGet(a, url, why);
    }
    // A profile actuator: set to the profile of that name.
    if (a.valueType == JPActuatorConfig::ValueType::Profile) {
        const JPActuatorConfig::Profile* p = a.profileNamed(value);
        if (!p) {
            why = "Actuator " + a.name + " profile " + value + " not found.";
            return false;
        }
        return doProfile(a, *p, why, depth);
    }
    JPGcodeDriver* d = driver(a.driverId);
    if (!d || !a.canSet()) {
        why = a.name + " cannot be set to a value";
        return false;
    }
    const JPReply r = d->send(JPFirmwareProfile::fill(a.valueCommand, { { "index", a.index }, { "value", value } })).get();
    JLOGC(JPlacerLog::kCell, r.ok ? JLogLevel::Info : JLogLevel::Warn)
        << a.name << " set to " << value << (r.ok ? std::string() : ": " + r.error);
    why = r.error;
    return r.ok;
}

bool JPCell::doProfile(const JPActuatorConfig& a, const JPActuatorConfig::Profile& p, std::string& why, int depth) {
    // A profile naming a profile actuator that names this one again goes no deeper than this.
    if (depth > int(JPActuatorConfig::kProfileActuators)) {
        why = "Actuator " + a.name + ": its profiles name each other";
        return false;
    }
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << a.name << " profile " << p.name;
    for (size_t k = 0; k < JPActuatorConfig::kProfileActuators; ++k) {
        const std::string& id = a.profileActuators[k];
        const std::string& value = p.values[k];
        if (id.empty() || value.empty()) continue;
        const JPActuatorConfig* member = nullptr;
        for (const JPActuatorConfig& m : m_config.actuators)
            if (m.id == id) member = &m;
        if (!member) continue;
        // A switch by true or false; a number, text or profile by its value (as OpenPnP: a nested profile by name).
        const bool ok = member->valueType == JPActuatorConfig::ValueType::Boolean ? doSwitch(id, value == "true", why, depth + 1)
                                                                                  : doSet(id, value, why, depth + 1);
        if (!ok) return false;
    }
    return true;
}

void JPCell::switchActuator(const std::string& actuatorId, bool on) {
    m_thread.post([this, actuatorId, on] {
        std::string why;
        const bool ok = finished(doSwitch(actuatorId, on, why), why);
        onActuator.emit(actuatorId, ok, ok ? (on ? "on" : "off") : why);
    });
}

bool JPCell::setActuatorAndWait(const std::string& actuatorId, const std::string& value, std::string& why) {
    std::promise<std::pair<bool, std::string>> done;
    auto result = done.get_future();
    m_thread.post([this, actuatorId, value, &done] {
        std::string w;
        const bool ok = finished(doSet(actuatorId, value, w), w);
        onActuator.emit(actuatorId, ok, ok ? value : w);
        done.set_value({ ok, w });
    });
    const auto [ok, w] = result.get();
    why = w;
    return ok;
}

bool JPCell::switchActuatorAndWait(const std::string& actuatorId, bool on, std::string& why) {
    std::promise<std::pair<bool, std::string>> done;
    auto result = done.get_future();
    m_thread.post([this, actuatorId, on, &done] {
        std::string w;
        const bool ok = finished(doSwitch(actuatorId, on, w), w);
        onActuator.emit(actuatorId, ok, ok ? (on ? "on" : "off") : w);
        done.set_value({ ok, w });
    });
    const auto [ok, w] = result.get();
    why = w;
    return ok;
}

void JPCell::pick(const std::string& nozzleId) {
    m_thread.post([this, nozzleId] {
        std::string why;
        for (const JPNozzleConfig& n : m_config.nozzles)
            if (n.id == nozzleId && !finished(doPick(n, why), why)) onAlarm.emit(n.name + ": pick failed (" + why + ")");
    });
}

void JPCell::place(const std::string& nozzleId) {
    m_thread.post([this, nozzleId] {
        std::string why;
        for (const JPNozzleConfig& n : m_config.nozzles)
            if (n.id == nozzleId && !finished(doPlace(n, why), why)) onAlarm.emit(n.name + ": place failed (" + why + ")");
    });
}

bool JPCell::stop(bool emergency, std::string& why) {
    bool any = false;
    for (const auto& d : m_drivers) any = d->halt(emergency) || any;
    if (!any) {
        why = m_connected ? "no controller's firmware profile says how to stop it" : "not connected";
        return false;
    }
    JLOGC(JPlacerLog::kCell, JLogLevel::Warn) << m_config.name << (emergency ? ": EMERGENCY STOP" : ": stop");
    if (emergency && m_homed) {
        m_homed = false;   // a controller reset mid-move may have lost its place
        onHomed.emit(false);
    }
    return true;
}

void JPCell::setSpeed(double share) {
    m_speed = std::clamp(share, 0.0, 1.0);
}

void JPCell::safeZ(const std::string& headId, double speed) {
    if (m_moving.exchange(true)) return;
    m_thread.post([this, headId, speed] {
        std::string why;
        bool ok = false;
        if (!m_connected || !m_homed) why = "not homed: home the machine first";
        else ok = doSafeZ(headId, speed, why);
        ok = finished(ok, why);
        m_moving = false;
        onMotion.emit(ok, why);
    });
}

void JPCell::parkZ(const JPMountConfig& mount, double speed) {
    if (m_moving.exchange(true)) return;
    m_thread.post([this, mount, speed] {
        std::string why;
        bool ok = false;
        if (!m_connected || !m_homed) why = "not homed: home the machine first";
        else ok = (!m_config.safeZPark || doSafeZ(mount.headId, speed, why)) && doParkZ(mount, speed, why);
        ok = finished(ok, why);
        m_moving = false;
        onMotion.emit(ok, why);
    });
}

bool JPCell::doParkZ(const JPMountConfig& mount, double speed, std::string& why) {
    const JPAxisConfig* z = m_config.axis(mount.axisZ);
    if (z && z->transformed()) z = m_config.axis(z->inputAxisId);
    if (!z || z->kind != JPAxisConfig::Kind::Controller || !z->safeZoneLowEnabled) return true;
    // Its effective safe Z: the part it carries lifted clear (Dynamic Safe Z), within the zone.
    double target = z->safeZoneLow + dynamicLift(mount);
    if (z->safeZoneHighEnabled) target = std::min(target, z->safeZoneHigh);
    const auto now = jogBase();
    if (now.count(z->id) && now.at(z->id) == target) return true;
    return doMove({ { z->id, target } }, speed, why);
}

void JPCell::discard(const std::string& nozzleId, double speed) {
    if (m_moving.exchange(true)) return;
    m_thread.post([this, nozzleId, speed] {
        std::string why;
        const bool ok = finished(doDiscard(nozzleId, speed, why), why);
        m_moving = false;
        onMotion.emit(ok, why);
    });
}

bool JPCell::doDiscard(const std::string& nozzleId, double speed, std::string& why) {
    for (const JPNozzleConfig& n : m_config.nozzles) {
        if (n.id != nozzleId) continue;
        if (!m_config.discardLocation) {
            why = "no discard location is set (Machine Setup, Machine)";
            return false;
        }
        // Up, across, down to it, the part let go, and up again.
        const JPMachineLocation& at = *m_config.discardLocation;
        const JPMountConfig& m = n.mount;
        std::map<std::string, double> across;
        if (!m.axisX.empty()) across[m.axisX] = at.x - m.offsetX;
        if (!m.axisY.empty()) across[m.axisY] = at.y - m.offsetY;
        // OpenPnP's discard probing (a contact probing nozzle's Discard Probing): from the tip's tallest
        // part above the place, probed down into it (something soft or slanted to brush the part off),
        // retracted, and the part let go there.
        const JPNozzleConfig::ContactProbe& p = n.contactProbe;
        if (p.method == "ContactSenseActuator" && p.partHeightProbing != "Off" && p.discardProbing && !m.axisZ.empty()) {
            double tall = 0;
            for (const JPNozzleTipConfig& t : m_config.nozzleTips)
                if (t.id == n.tipId) tall = t.maxPartHeightMm;
            JJson g = JJson::object();
            g["nozzle"] = n.name;
            double probed = 0, back = 0;
            const bool ok = doSafeZ(m.headId, speed, why) && doMove(across, speed, why)
                         && doMove({ { m.axisZ, at.z + tall + p.startOffsetMm - zOffsetOf(m) } }, speed, why)
                         && (!m_scripting || m_scripting->on("Nozzle.BeforePlaceProbe", g, why))
                         && doContactProbe(n, true, tall + p.depthMm, probed, why) && doContactProbe(n, false, p.depthMm, back, why)
                         && (!m_scripting || m_scripting->on("Nozzle.AfterPlaceProbe", g, why));
            return ok && doPlace(n, why) && doSafeZ(m.headId, speed, why);
        }
        return doSafeZ(m.headId, speed, why) && doMove(across, speed, why)
            && (m.axisZ.empty() || doMove({ { m.axisZ, at.z - zOffsetOf(m) } }, speed, why)) && doPlace(n, why)
            && doSafeZ(m.headId, speed, why);
    }
    why = "no nozzle " + nozzleId;
    return false;
}

bool JPCell::doAt(const std::string& nozzleId, const std::array<std::optional<double>, 4>& to, double speed, bool pick,
                  std::string& why) {
    for (const JPNozzleConfig& n : m_config.nozzles) {
        if (n.id != nozzleId) continue;
        const JPMountConfig& m = n.mount;
        std::map<std::string, double> across;
        if (to[0] && !m.axisX.empty()) across[m.axisX] = *to[0] - m.offsetX;
        if (to[1] && !m.axisY.empty()) across[m.axisY] = *to[1] - m.offsetY;
        if (to[3] && !m.axisRotation.empty()) across[m.axisRotation] = *to[3] - rotationModeOffset(n.id);
        compensateRunout(m, across, false);
        return doSafeZ(m.headId, speed, why) && (across.empty() || doMove(across, speed, why))
            && (!to[2] || m.axisZ.empty() || doMove({ { m.axisZ, *to[2] - zOffsetOf(m) } }, speed, why))
            && (pick ? doPick(n, why) : doPlace(n, why)) && doSafeZ(m.headId, speed, why);
    }
    why = "no nozzle " + nozzleId;
    return false;
}

bool JPCell::reaches(const JPMountConfig& mount, double x, double y) const {
    std::map<std::string, double> target;
    if (!mount.axisX.empty()) target[mount.axisX] = x - mount.offsetX;
    if (!mount.axisY.empty()) target[mount.axisY] = y - mount.offsetY;
    for (const auto& [id, t] : toAxes(target, jogBase())) {
        const JPAxisConfig* a = m_config.axis(id);
        if (!a) continue;
        if ((a->softLimitLowEnabled && t < a->softLimitLow) || (a->softLimitHighEnabled && t > a->softLimitHigh)) return false;
    }
    return true;
}

bool JPCell::waitFor(std::function<bool(std::string&)> work, std::string& why) {
    if (m_moving.exchange(true)) {
        why = "another move is under way";
        return false;
    }
    std::promise<std::pair<bool, std::string>> done;
    auto result = done.get_future();
    m_thread.post([this, &work, &done] {
        std::string w;
        const bool ok = finished(work(w), w);
        m_moving = false;
        onMotion.emit(ok, w);
        done.set_value({ ok, w });
    });
    const auto [ok, w] = result.get();
    why = w;
    return ok;
}

bool JPCell::pickAtAndWait(const std::string& nozzleId, std::array<std::optional<double>, 4> to, double speed,
                           std::string& why) {
    return waitFor([&](std::string& w) { return doAt(nozzleId, to, speed, true, w); }, why);
}

bool JPCell::vacuumOnAndWait(const std::string& nozzleId, std::string& why) {
    return onThreadAndWait([&](std::string& w) {
        for (const JPNozzleConfig& n : m_config.nozzles)
            if (n.id == nozzleId) return doVacuumOn(n, w);
        w = "no nozzle " + nozzleId;
        return false;
    }, why);
}

bool JPCell::pickAndWait(const std::string& nozzleId, std::string& why) {
    return onThreadAndWait([&](std::string& w) {
        for (const JPNozzleConfig& n : m_config.nozzles)
            if (n.id == nozzleId) return doPick(n, w);
        w = "no nozzle " + nozzleId;
        return false;
    }, why);
}

bool JPCell::readVacuumAndWait(const std::string& nozzleId, double& level, std::string& why) {
    return onThreadAndWait([&](std::string& w) {
        for (const JPNozzleConfig& n : m_config.nozzles)
            if (n.id == nozzleId) return readVacuum(n, level, w);
        w = "no nozzle " + nozzleId;
        return false;
    }, why);
}

bool JPCell::onThreadAndWait(const std::function<bool(std::string&)>& work, std::string& why) {
    std::promise<std::pair<bool, std::string>> done;
    auto result = done.get_future();
    m_thread.post([this, &work, &done] {
        std::string w;
        const bool ok = finished(work(w), w);
        done.set_value({ ok, w });
    });
    const auto [ok, w] = result.get();
    why = w;
    return ok;
}

bool JPCell::placeAtAndWait(const std::string& nozzleId, std::array<std::optional<double>, 4> to, double speed,
                            std::string& why) {
    return waitFor([&](std::string& w) { return doAt(nozzleId, to, speed, false, w); }, why);
}

bool JPCell::discardAndWait(const std::string& nozzleId, double speed, std::string& why) {
    return waitFor([&](std::string& w) { return doDiscard(nozzleId, speed, w); }, why);
}

bool JPCell::parkAndWait(const std::string& headId, double speed, std::string& why) {
    return waitFor([&](std::string& w) { return doPark(headId, speed, w); }, why);
}

bool JPCell::moveToolAndWait(const JPMountConfig& mount, std::array<std::optional<double>, 4> to, double speed,
                             std::string& why) {
    return waitFor([&](std::string& w) { return doMoveTool(mount, to, speed, true, w); }, why);
}

bool JPCell::moveToolStraightAndWait(const JPMountConfig& mount, std::array<std::optional<double>, 4> to, double speed,
                                     std::string& why) {
    return waitFor([&](std::string& w) { return doMoveTool(mount, to, speed, false, w); }, why);
}

bool JPCell::doMoveTool(const JPMountConfig& mount, const std::array<std::optional<double>, 4>& to, double speed,
                        bool atSafeZ, std::string& why) {
    std::map<std::string, double> axes;
    if (to[0] && !mount.axisX.empty()) axes[mount.axisX] = *to[0] - mount.offsetX;
    if (to[1] && !mount.axisY.empty()) axes[mount.axisY] = *to[1] - mount.offsetY;
    if (to[3] && !mount.axisRotation.empty()) axes[mount.axisRotation] = *to[3] - rotationModeOffsetOf(mount);
    compensateRunout(mount, axes, false);
    if (!atSafeZ) {
        if (to[2] && !mount.axisZ.empty()) axes[mount.axisZ] = *to[2] - zOffsetOf(mount);
        return axes.empty() || doMove(axes, speed, why);
    }
    if (!mount.headId.empty() && !doSafeZ(mount.headId, speed, why)) return false;
    if (!axes.empty() && !doMove(axes, speed, why)) return false;
    return !to[2] || mount.axisZ.empty() || doMove({ { mount.axisZ, *to[2] - zOffsetOf(mount) } }, speed, why);
}

bool JPCell::testMotionAndWait(const JPMountConfig& tool, bool reverse, JPMotionTestResult& result, std::string& why) {
    const JPMotionPlannerConfig planner = m_config.motionPlanner;
    const auto first = planner.initial(reverse);
    if (!first) {
        why = "Test Motion undefined. Please go to the Motion Planner tab and define/enable the Test Motion locations.";
        return false;
    }
    result = JPMotionTestResult();
    // Where each axis is over the run, from the controllers' reports.
    std::atomic<bool> sampling { false }, done { false };
    std::atomic<double> t0 { 0 };
    using Clock = std::chrono::steady_clock;
    const Clock::time_point epoch = Clock::now();
    auto seconds = [epoch] { return std::chrono::duration<double>(Clock::now() - epoch).count(); };
    std::map<std::string, std::vector<std::pair<double, double>>> samples;   // by axis id
    std::thread sampler([&] {
        std::map<std::string, double> last;
        while (!done) {
            if (sampling) {
                const double t = seconds() - t0;
                for (const auto& [id, at] : reportedPositions())
                    if (const auto l = last.find(id); l == last.end() || l->second != at) {
                        samples[id].emplace_back(t, at);
                        last[id] = at;
                    }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    const double speed = m_speed;
    const bool ok = waitFor(
        [&](std::string& w) {
            auto at = [&](int i) {
                const JPMachineLocation& l = planner.stops[size_t(i)].at;
                return std::array<std::optional<double>, 4> { l.x, l.y, l.z, l.rotation };
            };
            // To the first place, by way of safe Z, and standing there.
            if (!doMoveTool(tool, at(*first), 1, true, w) || !doCoordinate("WaitForUnconditionalCoordination", w)) return false;
            m_planned = 0;
            t0 = seconds();
            sampling = true;
            // Then on through the others, each leg at its speed (the machine's share), as the planner says.
            for (int i = reverse ? *first - 1 : *first + 1; reverse ? i >= 0 : i < 4; i += reverse ? -1 : 1) {
                if (!planner.stops[size_t(i)].enabled) continue;
                // As OpenPnP's: the leg's settings are those of the way into this place (from the one before it).
                const size_t leg = size_t(reverse ? i : i - 1);
                if (!doMoveTool(tool, at(i), planner.speeds[leg] * speed, planner.safeZ[leg], w)) return false;
            }
            if (!doCoordinate("WaitForStillstand", w)) return false;
            result.actualS = seconds() - t0;
            result.plannedS = m_planned.value_or(0);
            m_planned.reset();
            sampling = false;
            // Up to safe Z after, as OpenPnP's.
            return tool.headId.empty() || doSafeZ(tool.headId, 1, w);
        },
        why);
    m_planned.reset();
    done = true;
    sampler.join();
    for (auto& [id, points] : samples) {
        const JPAxisConfig* a = m_config.axis(id);
        if (points.size() > 1) result.axes[a ? a->name : id] = std::move(points);
    }
    return ok;
}

bool JPCell::switchTelling(const std::string& actuatorId, bool on, std::string& why) {
    const bool ok = doSwitch(actuatorId, on, why);
    onActuator.emit(actuatorId, ok, ok ? (on ? "on" : "off") : why);
    return ok;
}

bool JPCell::doVacuumOn(const JPNozzleConfig& n, std::string& why) {
    if (!m_connected) { why = "not connected"; return false; }
    if (n.vacuumActuatorId.empty()) { why = "it has no vacuum actuator"; return false; }
    const JPHeadConfig* head = nullptr;
    for (const JPHeadConfig& h : m_config.heads) if (h.id == n.mount.headId) head = &h;
    // The pump: with PartOn it runs while any of the head's nozzles holds a
    // part; with TaskDuration or KeepRunning it is started once and left.
    if (head && !head->pumpActuatorId.empty() && head->pumpControl != "None" && !m_pumpOn.count(head->id)) {
        if (!switchTelling(head->pumpActuatorId, true, why)) return false;
        m_pumpOn.insert(head->id);
        std::this_thread::sleep_for(std::chrono::milliseconds(head->pumpOnWaitMs));
    }
    // The package's pick vacuum level, to a vacuum actuator taking a value (OpenPnP's actuateVacuumValve(level)).
    const auto part = m_nozzleParts.find(n.id);
    const JPActuatorConfig* valve = nullptr;
    for (const JPActuatorConfig& a : m_config.actuators) if (a.id == n.vacuumActuatorId) valve = &a;
    if (part != m_nozzleParts.end() && part->second.pickVacuumLevel != 0 && valve && valve->canSet()) {
        const std::string level = format(part->second.pickVacuumLevel, 3);
        const bool ok = doSet(n.vacuumActuatorId, level, why);
        onActuator.emit(n.vacuumActuatorId, ok, ok ? level : why);
        if (!ok) return false;
        m_actuated[n.vacuumActuatorId] = true;
    } else if (!switchTelling(n.vacuumActuatorId, true, why)) {
        return false;
    }
    m_holding.insert(n.id);
    return true;
}

bool JPCell::pnpChecked(const JPNozzleConfig& n, bool pick, std::string& why) {
    const JPSimulationConfig& sim = m_config.simulation;
    if (!sim.on() || !sim.pickAndPlaceChecking || !m_pnpChecker) return true;
    const auto part = m_nozzleParts.find(n.id);
    if (part == m_nozzleParts.end() || part->second.partId.empty() || part->second.partId == kTestObjectPartId) return true;
    // The head's (first) camera, when it shows a picture of the table.
    const JPCameraConfig* camera = nullptr;
    for (const JPCameraConfig& c : m_config.cameras)
        if (!camera && !n.mount.headId.empty() && c.mount.headId == n.mount.headId) camera = &c;
    if (!camera || camera->device["backend"].str() != "image") return true;
    // Where the simulated machine has the nozzle: its axes, its offset, visual
    // homing's correction, then the homing error, the non-squareness and the runout.
    const auto at = positions();
    const auto corrected = correctionSinceHome();
    auto coordinate = [&](const std::string& axis) {
        const auto p = at.find(axis), c = corrected.find(axis);
        return (p == at.end() ? 0.0 : p->second) + (c == corrected.end() ? 0.0 : c->second);
    };
    PnpCheck check;
    check.nozzleId = n.id;
    check.partId = part->second.partId;
    check.pick = pick;
    check.x = coordinate(n.mount.axisX) + n.mount.offsetX;
    check.y = coordinate(n.mount.axisY) + n.mount.offsetY;
    const double axisRotation = coordinate(n.mount.axisRotation);
    check.rotation = axisRotation + rotationModeOffset(n.id);   // the part's angle, as the nozzle reads it
    if (sim.imperfect()) {
        check.x -= sim.homingErrorX;
        check.y -= sim.homingErrorY;
        check.x += sim.nonSquarenessFactor * check.y;
    }
    if (sim.dynamic() && sim.runoutMm != 0) {
        const double a = (axisRotation - sim.runoutPhaseDeg) * M_PI / 180;
        check.x += sim.runoutMm * std::cos(a);
        check.y += sim.runoutMm * std::sin(a);
    }
    // Not where parts are discarded.
    if (m_config.discardLocation
        && std::hypot(check.x - m_config.discardLocation->x, check.y - m_config.discardLocation->y) <= kDiscardNearMm)
        return true;
    check.camera = camera->device;
    std::string detail;
    const bool ok = m_pnpChecker(check, detail);
    JLOGC(JPlacerLog::kCell, ok ? JLogLevel::Debug : JLogLevel::Error)
        << "pick & place checking: " << n.name << " " << (pick ? "pick" : "place") << " of " << check.partId << " at "
        << check.x << ", " << check.y << ", " << check.rotation << ": " << detail;
    if (!ok) why = "Nozzle " + n.name + " part " + check.partId + (pick ? " pick" : " place") + " location not recognized.";
    return ok;
}

bool JPCell::doPick(const JPNozzleConfig& n, std::string& why) {
    if (!m_connected) { why = "not connected"; return false; }
    if (n.vacuumActuatorId.empty()) { why = "it has no vacuum actuator"; return false; }
    const JPNozzleTipConfig* tip = nullptr;
    for (const JPNozzleTipConfig& t : m_config.nozzleTips) if (t.id == n.tipId) tip = &t;
    // Part on, by a difference: the level before the vacuum comes on (none when it already is).
    double before = 0;
    if (tip && tip->partOn.method == "Difference" && !m_holding.count(n.id) && !readVacuum(n, before, why)) return false;
    if (!pnpChecked(n, true, why)) return false;
    if (!doVacuumOn(n, why)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(n.pickDwellMs + (tip ? tip->pickDwellMs : 0)));
    if (tip && tip->partOn.method != "None" && !sensed(n, tip->partOn, before, "on", why)) return false;
    return true;
}

bool JPCell::readVacuum(const JPNozzleConfig& n, double& level, std::string& why) {
    const std::string& sensor = n.vacuumSenseActuatorId.empty() ? n.vacuumActuatorId : n.vacuumSenseActuatorId;
    std::string value;
    if (!doRead(sensor, value, why)) return false;
    char* end = nullptr;
    level = std::strtod(value.c_str(), &end);
    if (end == value.c_str()) {
        why = "the vacuum reading '" + value + "' is not a number";
        return false;
    }
    return true;
}

bool JPCell::sensed(const JPNozzleConfig& n, const JPNozzleTipConfig::Sensing& s, double before, const char* onOff,
                    std::string& why) {
    double level = 0;
    if (!readVacuum(n, level, why)) return false;
    auto outside = [](double v, double lo, double hi) { return v < lo || v > hi; };
    const std::string part = std::string("part ") + onOff;
    if (s.method == "Difference") {
        if (outside(before, s.low, s.high)) {
            why = part + ": the vacuum before, " + format(before, 1) + ", is outside " + format(s.low, 1) + " .. " + format(s.high, 1);
            return false;
        }
        if (const double d = level - before; outside(d, s.diffLow, s.diffHigh)) {
            why = part + ": the vacuum changed by " + format(d, 1) + ", outside " + format(s.diffLow, 1) + " .. " + format(s.diffHigh, 1);
            return false;
        }
    } else if (outside(level, s.low, s.high)) {
        why = part + ": the vacuum, " + format(level, 1) + ", is outside " + format(s.low, 1) + " .. " + format(s.high, 1);
        return false;
    }
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << n.name << ": " << part << " (vacuum " << format(level, 1) << ")";
    return true;
}

bool JPCell::partOffCheck(const JPNozzleConfig& n, const JPNozzleTipConfig& tip, bool& off, std::string& why) {
    double before = 0;
    if (tip.partOff.method == "Difference" && !readVacuum(n, before, why)) return false;
    if (!switchTelling(n.vacuumActuatorId, true, why)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(tip.partOffProbingMs));
    if (!switchTelling(n.vacuumActuatorId, false, why)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(tip.partOffDwellMs));
    off = sensed(n, tip.partOff, before, "off", why);
    return true;
}

bool JPCell::contactProbeAndWait(const std::string& nozzleId, bool forward, double depthMm, double& probedZ, std::string& why) {
    return waitFor([&](std::string& w) {
        for (const JPNozzleConfig& n : m_config.nozzles)
            if (n.id == nozzleId) return doContactProbe(n, forward, depthMm, probedZ, w);
        w = "no nozzle " + nozzleId;
        return false;
    }, why);
}

bool JPCell::doContactProbe(const JPNozzleConfig& n, bool forward, double depthMm, double& probedZ, std::string& why) {
    if (!m_connected || !m_homed) { why = "not homed: home the machine first"; return false; }
    const JPNozzleConfig::ContactProbe& p = n.contactProbe;
    const JPMountConfig& m = n.mount;
    // The nozzle's Z (its own, the offset in).
    auto zNow = [&] {
        const auto at = jogBase();
        const auto z = at.find(m.axisZ);
        return (z == at.end() ? 0.0 : z->second) + zOffsetOf(m);
    };
    auto moveZ = [&](double z) { return doMove({ { m.axisZ, z - zOffsetOf(m) } }, 1.0, why); };
    if (p.method == "ContactSenseActuator") {
        if (p.actuatorId.empty()) { why = n.name + " has no contact sense actuator"; return false; }
        if (!doSwitch(p.actuatorId, forward, why)) return false;
        // The controller moved under the actuator's command: where it stopped, as it reports it.
        if (!doCoordinate("WaitForUnconditionalCoordination", why)) return false;
        {
            std::lock_guard lk(m_mutex);
            for (auto& [id, sent] : m_sent)
                if (const auto at = m_positions.find(id); at != m_positions.end()) sent = at->second;
        }
        probedZ = zNow();
        if (forward) {
            probedZ += p.adjustMm;
            if (!moveZ(probedZ)) return false;
        }
        return true;
    }
    if (p.method == "VacuumSense") {
        probedZ = zNow();
        if (!forward) return true;
        // OpenPnP's sniffle: the part-off check, stepping down until the nozzle is blocked.
        const JPNozzleTipConfig* tip = nullptr;
        for (const JPNozzleTipConfig& t : m_config.nozzleTips) if (t.id == n.tipId) tip = &t;
        if (!tip) { why = "Nozzle " + n.name + " cannot sniffle-probe without nozzle tip."; return false; }
        if (tip->partOff.method == "None") {
            why = "Nozzle tip " + tip->name + " cannot sniffle-probe without Part-Off sensing method.";
            return false;
        }
        if (n.vacuumActuatorId.empty()) { why = "Nozzle " + n.name + " cannot sniffle-probe without vacuum valve actuator."; return false; }
        if (n.vacuumSenseActuatorId.empty()) {
            why = "Nozzle " + n.name + " cannot sniffle-probe without vacuum sensing actuator.";
            return false;
        }
        if (m_holding.count(n.id)) {
            why = "Nozzle " + n.name + " cannot sniffle-probe with part on nozzle. Free nozzle vacuum sensing needed.";
            return false;
        }
        bool off = false;
        std::string ignored;
        if (!partOffCheck(n, *tip, off, ignored)) { why = ignored; return false; }
        if (!off) { why = "Nozzle " + n.name + " first sniffle-probe was already sensing contact. Check the settings."; return false; }
        const int count = int(std::ceil(depthMm / std::max(1e-6, p.sniffleIncrementMm)));
        double z = probedZ;
        for (int i = 0; i < count; ++i) {
            z -= p.sniffleIncrementMm;
            if (!moveZ(z) || !doCoordinate("WaitForStillstand", why)) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(p.sniffleDwellMs));
            if (!partOffCheck(n, *tip, off, ignored)) { why = ignored; return false; }
            if (!off) {
                // Contact.
                probedZ = z + p.adjustMm;
                return moveZ(probedZ);
            }
        }
        why = "Nozzle " + n.name + " sniffle-probing made no contact. Check the settings.";
        return false;
    }
    why = "Nozzle " + n.name + " has contact probing disabled.";
    return false;
}

std::optional<double> JPCell::probedOffset(const std::string& nozzleId, bool feeder, const std::string& key) const {
    std::lock_guard lk(m_mutex);
    const auto& all = feeder ? m_probedFeederOffsets : m_probedPartOffsets;
    const auto n = all.find(nozzleId);
    if (n == all.end()) return std::nullopt;
    const auto k = n->second.find(key);
    if (k == n->second.end()) return std::nullopt;
    return k->second;
}

void JPCell::setProbedOffset(const std::string& nozzleId, bool feeder, const std::string& key, double offsetMm) {
    std::lock_guard lk(m_mutex);
    (feeder ? m_probedFeederOffsets : m_probedPartOffsets)[nozzleId][key] = offsetMm;
}

bool JPCell::doPlace(const JPNozzleConfig& n, std::string& why) {
    if (!m_connected) { why = "not connected"; return false; }
    if (n.vacuumActuatorId.empty()) { why = "it has no vacuum actuator"; return false; }
    if (!pnpChecked(n, false, why)) return false;
    int dwell = n.placeDwellMs;
    for (const JPNozzleTipConfig& t : m_config.nozzleTips) if (t.id == n.tipId) dwell += t.placeDwellMs;
    // OpenPnP's place blow-off: the package's level, else the tip's; none, no blow (only the vacuum off).
    double blowLevel = 0;
    for (const JPNozzleTipConfig& t : m_config.nozzleTips) if (t.id == n.tipId) blowLevel = t.placeBlowOffLevel;
    if (const auto part = m_nozzleParts.find(n.id); part != m_nozzleParts.end() && part->second.placeBlowOffLevel != 0)
        blowLevel = part->second.placeBlowOffLevel;
    const bool blow = !n.blowOffActuatorId.empty() && blowLevel != 0;
    if (!(blow && n.blowOffClosesVacuum) && !switchTelling(n.vacuumActuatorId, false, why)) return false;
    if (blow) {
        const JPActuatorConfig* blower = nullptr;
        for (const JPActuatorConfig& a : m_config.actuators) if (a.id == n.blowOffActuatorId) blower = &a;
        if (blower && blower->canSet()) {
            const std::string level = format(blowLevel, 3);
            const bool ok = doSet(n.blowOffActuatorId, level, why);
            onActuator.emit(n.blowOffActuatorId, ok, ok ? level : why);
            if (!ok) return false;
        } else if (!switchTelling(n.blowOffActuatorId, true, why)) {
            return false;
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(dwell));
    if (blow && !switchTelling(n.blowOffActuatorId, false, why)) return false;
    m_holding.erase(n.id);
    // Part off: the valve opened for the probing time and closed for the
    // dwell, then the vacuum read (a part still on holds it).
    const JPNozzleTipConfig* tip = nullptr;
    for (const JPNozzleTipConfig& t : m_config.nozzleTips) if (t.id == n.tipId) tip = &t;
    if (tip && tip->partOff.method != "None") {
        bool off = false;
        if (!partOffCheck(n, *tip, off, why)) return false;
        if (!off) return false;
    }
    // The pump goes off with the last part on its head, when it runs for parts (or a task).
    const JPHeadConfig* head = nullptr;
    for (const JPHeadConfig& h : m_config.heads) if (h.id == n.mount.headId) head = &h;
    if (head && m_pumpOn.count(head->id) && (head->pumpControl == "PartOn" || head->pumpControl == "TaskDuration")) {
        bool holding = false;
        for (const JPNozzleConfig& other : m_config.nozzles)
            if (other.mount.headId == head->id && m_holding.count(other.id)) holding = true;
        if (!holding) {
            if (!switchTelling(head->pumpActuatorId, false, why)) return false;
            m_pumpOn.erase(head->id);
        }
    }
    return true;
}

bool JPCell::doSwitch(const std::string& actuatorId, bool on, std::string& why, int depth) {
    for (const JPActuatorConfig& a : m_config.actuators)
        if (a.id == actuatorId)
            return doCoordinate(a.coordinatedBeforeActuate, why) && doSwitchNow(a, on, why, depth)
                && doCoordinate(a.coordinatedAfterActuate, why);
    why = "no actuator " + actuatorId;
    return false;
}

bool JPCell::doSwitchNow(const JPActuatorConfig& a, bool on, std::string& why, int depth) {
    const std::string& actuatorId = a.id;
    // A script actuator: its script told actuateBoolean.
    if (!a.scriptName.empty()) {
        JJson g = JJson::object();
        g["actuateBoolean"] = on;
        const bool ok = runActuatorScript(a, g, why);
        if (ok) m_actuated[actuatorId] = on;
        return ok;
    }
    // An HTTP actuator: its on or off URL, else its parameter URL with 1 or 0.
    if (a.http.on) {
        const std::string& url = on ? a.http.onUrl : a.http.offUrl;
        const bool ok = url.empty() ? doSet(actuatorId, on ? "1" : "0", why, depth) : httpGet(a, url, why);
        if (ok) m_actuated[actuatorId] = on;
        return ok;
    }
    // A profile actuator: its Default ON or Default OFF profile.
    if (a.valueType == JPActuatorConfig::ValueType::Profile) {
        const JPActuatorConfig::Profile* p = a.defaultProfile(on);
        if (!p) {
            why = "Actuator " + a.name + " " + (on ? "Default ON" : "Default OFF") + " profile not found.";
            return false;
        }
        return doProfile(a, *p, why, depth);
    }
    JPGcodeDriver* d = driver(a.driverId);
    // Its own command for it, else its value command with its on or off value.
    const std::string& own = on ? a.onCommand : a.offCommand;
    const std::string& value = on ? a.onValue : a.offValue;
    const std::string& tmpl = !own.empty() || value.empty() ? own : a.valueCommand;
    // Nothing to send to a simulated controller (OpenPnP's NullDriver, or
    // one Simulation Mode replaces): switched, as there.
    if (d && tmpl.empty() && d->config().link["type"].str() == "simulated") {
        JLOGC(JPlacerLog::kCell, JLogLevel::Info) << a.name << " " << (on ? "on" : "off") << " (simulated: no command)";
        m_actuated[actuatorId] = on;
        return true;
    }
    if (!d || tmpl.empty()) {
        why = a.name + " cannot be switched " + (on ? "on" : "off");
        return false;
    }
    const JPReply r = d->send(JPFirmwareProfile::fill(tmpl, { { "index", a.index }, { "value", value } })).get();
    JLOGC(JPlacerLog::kCell, r.ok ? JLogLevel::Info : JLogLevel::Warn)
        << a.name << " " << (on ? "on" : "off") << (r.ok ? std::string() : ": " + r.error);
    why = r.error;
    if (r.ok) m_actuated[actuatorId] = on;
    return r.ok;
}

void JPCell::readActuator(const std::string& actuatorId) {
    m_thread.post([this, actuatorId] {
        std::string value, why;
        const bool ok = finished(doRead(actuatorId, value, why), why);
        onActuator.emit(actuatorId, ok, ok ? value : why);
    });
}

bool JPCell::readActuatorAndWait(const std::string& actuatorId, const std::optional<std::string>& parameter, std::string& value,
                                 std::string& why) {
    std::promise<std::pair<bool, std::string>> done;
    auto result = done.get_future();
    m_thread.post([this, actuatorId, parameter, &done] {
        std::string v, w;
        const bool ok = finished(doRead(actuatorId, v, w, parameter), w);
        onActuator.emit(actuatorId, ok, ok ? v : w);
        done.set_value({ ok, ok ? v : w });
    });
    const auto [ok, out] = result.get();
    (ok ? value : why) = out;
    return ok;
}

bool JPCell::doRead(const std::string& actuatorId, std::string& value, std::string& why,
                    const std::optional<std::string>& parameter) {
    for (const JPActuatorConfig& a : m_config.actuators)
        if (a.id == actuatorId) return doCoordinate(a.coordinatedBeforeRead, why) && doReadNow(a, value, why, parameter);
    why = "no actuator " + actuatorId;
    return false;
}

bool JPCell::doReadNow(const JPActuatorConfig& a, std::string& value, std::string& why,
                       const std::optional<std::string>& parameter) {
    if (a.http.on) return httpRead(a, value, why);
    JPGcodeDriver* d = driver(a.driverId);
    if (!d || !a.canRead()) {
        why = a.name + " cannot be read";
        return false;
    }
    std::regex pattern;
    try {
        pattern = std::regex(a.readPattern);
    } catch (const std::regex_error&) {
        why = a.name + ": its read pattern is not a valid pattern";
        return false;
    }
    std::map<std::string, std::string> vars { { "index", a.index } };
    if (parameter) vars["value"] = *parameter;
    const JPReply r = d->send(JPFirmwareProfile::fill(a.readCommand, vars)).get();
    if (!r.ok) {
        why = r.error;
        return false;
    }
    for (const std::string& line : r.lines) {
        std::smatch m;
        if (std::regex_search(line, m, pattern) && m.size() > 1) {
            JLOGC(JPlacerLog::kCell, JLogLevel::Info) << a.name << " read " << m[1].str() << (a.unit.empty() ? std::string() : " " + a.unit);
            value = m[1].str();
            return true;
        }
    }
    JLOGC(JPlacerLog::kCell, JLogLevel::Warn) << a.name << ": no value in the reply to '" << a.readCommand
                                               << "' (pattern " << a.readPattern << ")";
    why = a.name + ": no value in its reply";
    return false;
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
            // A letter shared by several axes (switched between by pre-move commands): its report
            // cannot tell which; each keeps where it was last sent.
            if (sharesLetter(a)) continue;
            const auto it = status.positions.find(a.letter);
            if (it == status.positions.end()) continue;
            const double reported = fromDriver(a, it->second, driverId);   // in the machine's millimetres
            m_reported[a.id] = reported;
            // Less a directional backlash offset in effect: where the axis is.
            const auto applied = m_backlashApplied.find(a.id);
            m_axisPositions[a.id] = m_positions[a.id] = reported - (applied == m_backlashApplied.end() ? 0 : applied->second);
        }
        // Square coordinates from the axes' own: X takes back the Y axis's lean.
        if (const JPSquarenessConfig& q = m_config.squareness; q.active()) {
            const auto x = m_axisPositions.find(q.axisX), y = m_axisPositions.find(q.axisY);
            if (x != m_axisPositions.end() && y != m_axisPositions.end())
                m_positions[q.axisX] = x->second + q.xPerY * (y->second - q.atY);
        }
        for (const JPAxisConfig& a : m_config.axes) {
            if (!a.transformed()) continue;
            const auto in = m_positions.find(a.inputAxisId);
            if (in == m_positions.end()) continue;
            if (const auto out = a.mapped(in->second)) m_positions[a.id] = *out;
        }
        // A linear transform axis: its inputs' coordinates times its factors, plus its offset.
        for (const JPAxisConfig& a : m_config.axes) {
            if (a.kind != JPAxisConfig::Kind::Linear) continue;
            double v = a.linearOffset;
            for (size_t i = 0; i < 4; ++i)
                if (const auto in = m_positions.find(a.linearInputs[i]); !a.linearInputs[i].empty() && in != m_positions.end())
                    v += a.linearFactors[i] * in->second;
            m_positions[a.id] = v;
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

void JPCell::unhome() {
    m_thread.post([this] {
        if (!m_homed) return;
        m_homed = false;
        onHomed.emit(false);
    });
}

void JPCell::home() {
    m_thread.post([this] {
        std::string why;
        m_homing = true;
        bool ok = finished(doHome(why), why);
        m_homing = false;
        if (ok) {
            m_homed = true;
            actuateFor(&JPActuatorConfig::homedActuation);
            // OpenPnP's ReferenceNozzleTip.home: the changer slots found again, unless only by hand.
            {
                std::lock_guard lk(m_mutex);
                for (const JPNozzleTipConfig& t : m_config.nozzleTips)
                    if (t.visionCalibration.trigger != "Manual") m_slotOffsets.erase(t.id);
            }
            // The tips' Z calibrations, as their triggers say; one with Fail Homing failing fails the homing.
            if (!finished(doZCalibrationsAfterHoming(why), why)) {
                m_homed = false;
                ok = false;
            } else {
                onHomed.emit(true);
            }
        }
        onMotion.emit(ok, why);
    });
}

bool JPCell::doHome(std::string& why) {
    if (!m_connected) { why = "not connected"; return false; }
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << m_config.name << ": homing";
    // Moves left running (continuous motion) were waited for as their work ended, or stopped.
    m_inMotion.clear();
    m_streaming = false;
    m_homed = false;
    // OpenPnP's ContactProbeNozzle.home: heights probed "after homing" (or no longer probed) forgotten.
    {
        std::lock_guard lk(m_mutex);
        for (const JPNozzleConfig& n : m_config.nozzles) {
            const JPNozzleConfig::ContactProbe& p = n.contactProbe;
            const bool sensing = p.method == "ContactSenseActuator";
            if (!sensing || p.feederHeightProbing == "Off" || p.feederHeightProbing == "AfterHoming") m_probedFeederOffsets.erase(n.id);
            if (!sensing || p.partHeightProbing == "Off" || p.partHeightProbing == "AfterHoming") m_probedPartOffsets.erase(n.id);
        }
    }
    onHomed.emit(false);
    for (const auto& d : m_drivers) {
        // A controller left in alarm (reset mid-move) is unlocked to home:
        // homing is what makes its position known again.
        const JPReply u = d->unlockForHoming();
        if (!u.ok) { why = d->config().name + ": not unlocked to home (" + u.error + ")"; return false; }
        const JPReply r = d->command("home");
        if (!r.ok) { why = d->config().name + ": homing failed (" + r.error + ")"; return false; }
        const JPReply w = d->waitForMotion();
        if (!w.ok) { why = d->config().name + ": homing did not finish (" + w.error + ")"; return false; }
        // Where the axes are now: their home coordinates.
        std::string axes;
        for (const JPAxisConfig& a : m_config.axes)
            if (a.kind == JPAxisConfig::Kind::Controller && a.driverId == d->id())
                axes += (axes.empty() ? "" : " ") + word(a, a.homeCoordinate, *d);
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
        m_backlashApplied.clear();
        m_backlashLag.clear();
        for (const JPAxisConfig& a : m_config.axes) {
            if (a.kind == JPAxisConfig::Kind::Virtual) m_positions[a.id] = a.homeCoordinate;
            if (!a.transformed()) m_sent[a.id] = a.homeCoordinate;
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
    roamUnsafeZ(toolId, *mount, now, targets);
    // Where every axis would be: the owner may refuse it (OpenPnP's Board Protection).
    if (m_jogGuard) {
        std::map<std::string, double> after = now;
        for (const auto& [axis, to] : targets) after[axis] = to;
        if (!m_jogGuard(*mount, after)) return;
    }
    compensateRunout(*mount, targets, true);
    if (!targets.empty()) moveAxes(std::move(targets), speed);
}

void JPCell::roamUnsafeZ(const std::string& toolId, const JPMountConfig& mount, const std::map<std::string, double>& now,
                         std::map<std::string, double>& targets) {
    const JPAxisConfig* z = m_config.axis(mount.axisZ);
    const auto zNow = now.find(mount.axisZ);
    if (!z || zNow == now.end()) return;
    // Its safe Z: a virtual axis's is its home; any other's, its safe zone.
    const bool isVirtual = z->kind == JPAxisConfig::Kind::Virtual;
    const auto zAt = targets.count(mount.axisZ) ? targets.at(mount.axisZ) : zNow->second;
    constexpr double kSame = 1e-3;   // coordinates this close are the same place
    const bool unsafe = isVirtual ? zAt < z->homeCoordinate - kSame : !inSafeZone(mount.axisZ, zAt);
    auto xy = [&](const std::map<std::string, double>& at) {
        const auto x = at.find(mount.axisX), y = at.find(mount.axisY);
        return std::pair { x == at.end() ? 0.0 : x->second, y == at.end() ? 0.0 : y->second };
    };
    std::lock_guard lk(m_roamMutex);
    if (!unsafe) {
        m_roamFrom.erase(toolId);
        return;
    }
    // Where it was left low (or lowered further): roaming from there.
    if (!m_roamFrom.count(toolId) || targets.count(mount.axisZ)) {
        m_roamFrom[toolId] = xy(now);
        return;
    }
    std::map<std::string, double> after = now;
    for (const auto& [id, t] : targets) after[id] = t;
    const auto [x0, y0] = m_roamFrom[toolId];
    const auto [x1, y1] = xy(after);
    if (std::hypot(x1 - x0, y1 - y0) <= m_config.unsafeZRoamingMm) return;
    // Too far: up to safe Z with this move.
    if (isVirtual) {
        targets[mount.axisZ] = z->homeCoordinate;
    } else {
        const JPAxisConfig* raw = z->transformed() ? m_config.axis(z->inputAxisId) : z;
        if (raw && raw->safeZoneLowEnabled) {
            const auto out = z->transformed() ? z->mapped(raw->safeZoneLow) : std::optional<double>(raw->safeZoneLow);
            if (out) targets[mount.axisZ] = *out;
        }
    }
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << "jogged further than " << m_config.unsafeZRoamingMm
                                              << " mm at unsafe Z: up to safe Z";
    m_roamFrom.erase(toolId);
}

bool JPCell::moveAxesAndWait(std::map<std::string, double> targets, double speed, std::string& why, bool squared) {
    if (m_moving.exchange(true)) {
        why = "another move is under way";
        return false;
    }
    std::promise<std::pair<bool, std::string>> done;
    auto result = done.get_future();
    m_thread.post([this, targets = std::move(targets), speed, squared, &done] {
        std::string w;
        const bool ok = finished(doMove(targets, speed, w, squared), w);
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
        const bool ok = finished(doCorrectPosition(by, w), w);
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
        w += (w.empty() ? "" : " ") + word(*a, v + backlashApplied(id), *dr);
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

void JPCell::compensateRunout(const JPMountConfig& mount, std::map<std::string, double>& targets, bool stepped) const {
    const JPRunout* r = runoutFor(mount);
    if (!r || mount.axisRotation.empty() || mount.axisX.empty() || mount.axisY.empty()) return;
    const auto now = jogBase();
    const auto rot = targets.find(mount.axisRotation);
    const auto at = now.find(mount.axisRotation);
    if (at == now.end()) return;
    const double from = at->second, to = rot == targets.end() ? from : rot->second;
    if (rot == targets.end() && !targets.count(mount.axisX) && !targets.count(mount.axisY)) return;
    // The axes now carry the swing at the angle they are at; sent where the
    // tip's centre is to be, they carry the swing at the new angle instead.
    double fx, fy, tx, ty;
    r->runoutAt(from, fx, fy);
    r->runoutAt(to, tx, ty);
    auto axis = [&](const std::string& id, double was, double swing) {
        const auto t = targets.find(id);
        const auto n = now.find(id);
        if (t != targets.end()) t->second += (stepped ? was : 0) - swing;   // the tip's centre (stepped: from where it is)
        else if (n != now.end()) targets[id] = n->second + was - swing;     // stays where the centre is
    };
    axis(mount.axisX, fx, tx);
    axis(mount.axisY, fy, ty);
}

const JPRunout* JPCell::runoutFor(const JPMountConfig& m) const {
    for (const JPNozzleConfig& n : m_config.nozzles) {
        const JPMountConfig& k = n.mount;
        if (k.headId != m.headId || k.axisX != m.axisX || k.axisY != m.axisY || k.axisZ != m.axisZ
            || k.axisRotation != m.axisRotation || n.tipId.empty())
            continue;
        for (const JPNozzleTipConfig& t : m_config.nozzleTips)
            if (t.id == n.tipId) return t.runoutOn(n.id);
    }
    return nullptr;
}

double JPCell::backlashApplied(const std::string& axisId) const {
    const auto b = m_backlashApplied.find(axisId);
    return b == m_backlashApplied.end() ? 0 : b->second;
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
        if (c.id == cameraId) {
            if (const JPCameraCalibration* k = c.calibrationFor(width, height))
                return c.mount.headId.empty() || !c.workingPlaneZ ? *k : k->atHeight(*c.workingPlaneZ);
            // OpenPnP's simulated cameras not calibrated here: as OpenPnP takes them.
            if (const auto known = c.openPnpCalibration(width, height)) return *known;
        }
    return {};
}

std::vector<JPCameraCalibration> JPCell::cameraCalibrations(const std::string& cameraId) const {
    std::lock_guard lk(m_mutex);
    for (const JPCameraConfig& c : m_config.cameras)
        if (c.id == cameraId) return c.calibrations;
    return {};
}

void JPCell::moveToolStraight(const JPMountConfig& mount, std::array<std::optional<double>, 4> to, double speed) {
    if (m_moving.exchange(true)) {
        JLOGC(JPlacerLog::kCell, JLogLevel::Debug) << "move refused: one is under way";
        return;
    }
    m_thread.post([this, mount, to, speed] {
        std::string why;
        const bool ok = finished(doMoveTool(mount, to, speed, false, why), why);
        m_moving = false;
        if (!ok) JLOGC(JPlacerLog::kCell, JLogLevel::Warn) << "move refused: " << why;
        onMotion.emit(ok, why);
    });
}

void JPCell::moveTool(const JPMountConfig& mount, std::array<std::optional<double>, 4> to, double speed) {
    if (m_moving.exchange(true)) {
        JLOGC(JPlacerLog::kCell, JLogLevel::Debug) << "move refused: one is under way";
        return;
    }
    m_thread.post([this, mount, to, speed] {
        std::string why;
        bool ok = mount.headId.empty() || doSafeZ(mount.headId, speed, why);
        // Tool = axis + its offset on the head (+ its tip's runout at its angle).
        std::map<std::string, double> across;
        if (to[0] && !mount.axisX.empty()) across[mount.axisX] = *to[0] - mount.offsetX;
        if (to[1] && !mount.axisY.empty()) across[mount.axisY] = *to[1] - mount.offsetY;
        if (to[3] && !mount.axisRotation.empty()) across[mount.axisRotation] = *to[3] - rotationModeOffsetOf(mount);
        compensateRunout(mount, across, false);
        if (ok && !across.empty()) ok = doMove(across, speed, why);
        if (ok && to[2] && !mount.axisZ.empty()) ok = doMove({ { mount.axisZ, *to[2] - zOffsetOf(mount) } }, speed, why);
        ok = finished(ok, why);
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
        const bool ok = finished(doMove(targets, speed, why), why);
        m_moving = false;
        if (!ok) JLOGC(JPlacerLog::kCell, JLogLevel::Warn) << "move refused: " << why;
        onMotion.emit(ok, why);
    });
}

bool JPCell::runActuatorScript(const JPActuatorConfig& a, JJson globals, std::string& why) {
    if (!m_scripting) {
        why = a.name + ": no scripts to run";
        return false;
    }
    globals["actuator"] = a.name;
    return m_scripting->execute((std::filesystem::path(m_scripting->directory()) / a.scriptName).string(), globals, why);
}

bool JPCell::httpGet(const JPActuatorConfig& a, const std::string& url, std::string& why) {
    // The same URL as last time: not asked again (OpenPnP's lastActuationUrl).
    if (m_lastHttpUrl[a.id] == url) return true;
    const JHttpResponse r = JHttpClient::getSync(url, kHttpTimeoutMs, { { "User-Agent", "Mozilla/5.0" } });
    JLOGC(JPlacerLog::kCell, r.ok() ? JLogLevel::Info : JLogLevel::Warn)
        << a.name << " GET " << url << ": " << (r.error.empty() ? std::to_string(r.status) : r.error);
    if (!r.error.empty()) {
        why = a.name + ": " + r.error;
        return false;
    }
    m_lastHttpUrl[a.id] = url;
    return true;
}

bool JPCell::httpRead(const JPActuatorConfig& a, std::string& value, std::string& why) {
    const JHttpResponse r = JHttpClient::getSync(a.http.readUrl, kHttpTimeoutMs, { { "User-Agent", "Mozilla/5.0" } });
    if (!r.error.empty()) {
        why = a.name + ": " + r.error;
        return false;
    }
    // OpenPnP's named group "Value": std::regex has no names, so it becomes a plain group, counted.
    std::string pattern = a.http.regex;
    size_t group = 0;
    if (const size_t at = pattern.find("(?<Value>"); at != std::string::npos) {
        group = 1;
        for (size_t i = 0; i < at; ++i)
            if (pattern[i] == '(' && (i == 0 || pattern[i - 1] != '\\') && (i + 1 >= pattern.size() || pattern[i + 1] != '?')) ++group;
        pattern.replace(at, 9, "(");
    }
    std::regex re;
    try {
        re = std::regex(pattern);
    } catch (const std::regex_error&) {
        why = a.name + ": its regex is not a valid pattern";
        return false;
    }
    value.clear();
    std::string text = r.text(), line;
    for (size_t start = 0; start <= text.size();) {
        const size_t end = text.find('\n', start);
        line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::smatch m;
        if (a.http.regex.empty()) value += line;
        else if (group > 0 && std::regex_match(line, m, re) && m.size() > group) value += m[group].str();
        if (end == std::string::npos) break;
        start = end + 1;
    }
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << a.name << " read " << value;
    return true;
}

bool JPCell::sharesLetter(const JPAxisConfig& a) const {
    for (const JPAxisConfig& b : m_config.axes)
        if (&b != &a && b.id != a.id && b.kind == JPAxisConfig::Kind::Controller && b.driverId == a.driverId && b.letter == a.letter)
            return true;
    return false;
}

double JPCell::driverUnits(const JPGcodeDriver& d) const {
    return d.config().units == "Inches" ? 1.0 / 25.4 : 1.0;
}

std::string JPCell::word(const JPAxisConfig& a, double value, const JPGcodeDriver& d) const {
    // A rotation is in degrees whatever the units.
    const double v = a.type == JPAxisConfig::Type::Rotation ? value : value * driverUnits(d);
    return a.letter + format(v, d.profile()->decimals());
}

double JPCell::fromDriver(const JPAxisConfig& a, double value, const std::string& driverId) {
    const JPGcodeDriver* d = driver(driverId);
    if (!d || a.type == JPAxisConfig::Type::Rotation) return value;
    return value / driverUnits(*d);
}

namespace {
// Coordinates this close are the same place, for the interlocks (OpenPnP's coordinatesMatch).
constexpr double kInterlockTolerance = 1e-3;
}

bool JPCell::doMove(std::map<std::string, double> targets, double speed, std::string& why, bool squared) {
    if (!m_connected || !m_homed) return doMoveNow(std::move(targets), speed, why, squared);
    // OpenPnP's axis interlocks, around the move (JPActuatorConfig::Interlock).
    const std::map<std::string, double> from = jogBase();
    std::map<std::string, double> to = from;
    for (const auto& [id, t] : targets) to[id] = t;
    // An axis that follows one moved goes with it.
    for (const JPAxisConfig& a : m_config.axes)
        if (a.transformed() && !targets.count(a.id))
            if (const auto in = targets.find(a.inputAxisId); in != targets.end())
                if (const auto out = a.mapped(in->second)) to[a.id] = *out;
    if (!doInterlocks(from, to, true, speed, why)) return false;
    if (!doMoveNow(std::move(targets), speed, why, squared)) return false;
    return doInterlocks(from, to, false, speed, why);
}

bool JPCell::inSafeZone(const std::string& axisId, double value) const {
    const JPAxisConfig* a = m_config.axis(axisId);
    if (!a) return true;
    if (a->transformed()) {
        const auto raw = a->unmapped(value);
        a = m_config.axis(a->inputAxisId);
        if (!a || !raw) return true;
        value = *raw;
    }
    if (a->safeZoneLowEnabled && value < a->safeZoneLow - kInterlockTolerance) return false;
    if (a->safeZoneHighEnabled && value > a->safeZoneHigh + kInterlockTolerance) return false;
    return true;
}

bool JPCell::doInterlocks(const std::map<std::string, double>& from, const std::map<std::string, double>& to, bool before,
                          double speed, std::string& why) {
    for (const JPActuatorConfig& a : m_config.actuators) {
        const JPActuatorConfig::Interlock& il = a.interlock;
        if (!il.active()) continue;
        // Only when one of its axes moves.
        bool moves = false;
        for (const std::string& id : il.axes) {
            if (id.empty()) continue;
            const auto f = from.find(id), t = to.find(id);
            if (f != from.end() && t != to.end() && std::abs(f->second - t->second) > kInterlockTolerance) moves = true;
        }
        if (!moves) continue;
        if (speed < il.speedMin || speed > il.speedMax) continue;   // masked by the speed
        // Masked by its conditional actuator not being in its state.
        if (!il.conditionalActuatorId.empty()) {
            const std::string& st = il.conditionalState;
            const bool mayBeOn = st == "SwitchedOn" || st == "SwitchedJustOn" || st == "SwitchedOnOrUnknown";
            const bool mustBeKnown = st == "SwitchedOn" || st == "SwitchedJustOn" || st == "SwitchedOff" || st == "SwitchedJustOff";
            const bool justChanged = st.find("Just") != std::string::npos;
            const auto now = m_actuated.find(il.conditionalActuatorId);
            if (now != m_actuated.end()) {
                auto& last = m_conditionalLast[a.id];
                if (justChanged && last && *last == now->second) continue;
                last = now->second;
                if (mayBeOn != now->second) continue;
            } else if (mustBeKnown) {
                continue;
            }
        }
        // Each axis where it will be after the move.
        auto switchTo = [&](bool on) {
            if (const auto s = m_actuated.find(a.id); s != m_actuated.end() && s->second == on) return true;
            return doSwitch(a.id, on, why);
        };
        const std::string& type = il.type;
        if (type == "SignalAxesMoving" || type == "SignalAxesStandingStill") {
            if (!switchTo(before != (type == "SignalAxesStandingStill"))) return false;
        } else if (type == "SignalAxesInsideSafeZone" || type == "SignalAxesOutsideSafeZone") {
            bool willBeSafe = true;
            for (const std::string& id : il.axes)
                if (!id.empty())
                    if (const auto t = to.find(id); t != to.end()) willBeSafe = willBeSafe && inSafeZone(id, t->second);
            // Switched before the move when leaving the zone, after it when coming into it.
            if (before != willBeSafe)
                if (!switchTo(willBeSafe != (type == "SignalAxesOutsideSafeZone"))) return false;
        } else if (type == "SignalAxesParked" || type == "SignalAxesUnparked") {
            const JPHeadConfig* head = nullptr;
            for (const JPHeadConfig& h : m_config.heads)
                if (h.id == a.mount.headId) head = &h;
            if (!head || !head->park) continue;
            // The park place as the head's camera would be there.
            double offX = 0, offY = 0;
            for (const JPCameraConfig& c : m_config.cameras)
                if (c.mount.headId == head->id) {
                    offX = c.mount.offsetX;
                    offY = c.mount.offsetY;
                    break;
                }
            bool willBeParked = true;
            for (const std::string& id : il.axes) {
                const JPAxisConfig* ax = id.empty() ? nullptr : m_config.axis(id);
                const auto t = to.find(id);
                if (!ax || t == to.end()) continue;
                if (ax->type == JPAxisConfig::Type::X) willBeParked = willBeParked && std::abs(t->second - (head->park->x - offX)) <= kInterlockTolerance;
                else if (ax->type == JPAxisConfig::Type::Y) willBeParked = willBeParked && std::abs(t->second - (head->park->y - offY)) <= kInterlockTolerance;
                else if (ax->type == JPAxisConfig::Type::Z) willBeParked = willBeParked && inSafeZone(id, t->second);
                else willBeParked = willBeParked && std::abs(t->second) <= kInterlockTolerance;
            }
            if (before != willBeParked)
                if (!switchTo(willBeParked != (type == "SignalAxesUnparked"))) return false;
        } else if (type == "ConfirmInRangeBeforeAxesMove" || type == "ConfirmInRangeAfterAxesMove"
                   || type == "ConfirmMatchBeforeAxesMove" || type == "ConfirmMatchAfterAxesMove") {
            const bool afterType = type.find("After") != std::string::npos;
            if (before == afterType) continue;
            std::string value;
            if (!doRead(a.id, value, why)) return false;
            if (type.find("InRange") != std::string::npos) {
                char* end = nullptr;
                const double v = std::strtod(value.c_str(), &end);
                if (end == value.c_str() || v < il.goodMin || v > il.goodMax) {
                    m_conditionalLast.erase(a.id);
                    why = a.name + " interlock confirmation " + (end == value.c_str() ? "unreadable: " + value
                                                                  : v < il.goodMin ? "below good range: " + value
                                                                                   : "above good range: " + value);
                    return false;
                }
            } else {
                auto trimmed = [](const std::string& s) {
                    const size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
                    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
                };
                bool match = false;
                try {
                    match = il.byRegex ? std::regex_match(value, std::regex(il.pattern)) : trimmed(value) == trimmed(il.pattern);
                } catch (const std::regex_error&) {
                    match = false;
                }
                if (!match) {
                    m_conditionalLast.erase(a.id);
                    why = a.name + " interlock confirmation does not match: " + value + " vs. " + il.pattern;
                    return false;
                }
            }
        }
    }
    return true;
}

bool JPCell::resolveLinear(std::map<std::string, double>& targets, const std::map<std::string, double>& now,
                           std::string& why) const {
    using Kind = JPAxisConfig::Kind;
    auto slot = [](JPAxisConfig::Type t) { return size_t(t == JPAxisConfig::Type::X ? 0 : t == JPAxisConfig::Type::Y ? 1
                                                         : t == JPAxisConfig::Type::Z ? 2 : 3); };
    // The move's linear axes by their type, and their inputs, consolidated (OpenPnP's toRaw).
    std::array<const JPAxisConfig*, 4> linear {};
    std::array<std::string, 4> input;
    auto take = [&](const JPAxisConfig& a) {
        const size_t i = slot(a.type);
        if (linear[i] && linear[i] != &a) {
            why = "axes " + linear[i]->name + " and " + a.name + " both have the same type in a linear transformation";
            return false;
        }
        linear[i] = &a;
        for (size_t k = 0; k < 4; ++k) {
            if (a.linearInputs[k].empty()) continue;
            if (!input[k].empty() && input[k] != a.linearInputs[k]) {
                why = "axis " + a.name + " has another " + JPAxisConfig::typeName(JPAxisConfig::Type(k)) + " input than the move's other linear axes";
                return false;
            }
            input[k] = a.linearInputs[k];
        }
        return true;
    };
    bool any = false;
    for (const auto& [id, t] : targets)
        if (const JPAxisConfig* a = m_config.axis(id); a && a->kind == Kind::Linear) {
            if (!take(*a)) return false;
            any = true;
        }
    if (!any) return true;
    // The tool the move is of (the one whose axes have its linear axes): its
    // other linear axes keep where they are, as its location holds them.
    std::vector<const JPMountConfig*> mounts;
    for (const JPNozzleConfig& n : m_config.nozzles) mounts.push_back(&n.mount);
    for (const JPCameraConfig& c : m_config.cameras) mounts.push_back(&c.mount);
    for (const JPActuatorConfig& a : m_config.actuators) mounts.push_back(&a.mount);
    for (const JPMountConfig* m : mounts) {
        const std::array<const std::string*, 4> axes { &m->axisX, &m->axisY, &m->axisZ, &m->axisRotation };
        bool all = true;
        for (size_t i = 0; i < 4; ++i) all = all && (!linear[i] || *axes[i] == linear[i]->id);
        if (!all) continue;
        for (size_t i = 0; i < 4; ++i) {
            if (linear[i] || axes[i]->empty()) continue;
            const JPAxisConfig* a = m_config.axis(*axes[i]);
            if (a && a->kind == Kind::Linear && !targets.count(a->linearInputs[i]) && !take(*a)) return false;
        }
        break;
    }
    auto at = [&](const std::string& id) {
        if (const auto t = targets.find(id); t != targets.end()) return t->second;
        const auto n = now.find(id);
        return n == now.end() ? 0.0 : n->second;
    };
    // M raw + offset = coordinate, row by type: a linear axis's factors, else the unit row.
    double m[4][5];
    for (size_t i = 0; i < 4; ++i) {
        for (size_t k = 0; k < 4; ++k) m[i][k] = linear[i] ? (input[k].empty() ? 0.0 : linear[i]->linearFactors[k]) : (i == k ? 1.0 : 0.0);
        m[i][4] = linear[i] ? at(linear[i]->id) - linear[i]->linearOffset : (input[i].empty() ? 0.0 : at(input[i]));
    }
    // Gauss-Jordan with partial pivoting.
    for (size_t c = 0; c < 4; ++c) {
        size_t p = c;
        for (size_t r = c + 1; r < 4; ++r) if (std::abs(m[r][c]) > std::abs(m[p][c])) p = r;
        if (std::abs(m[p][c]) < 1e-12) {
            why = "the linear transformation of the move's axes cannot be inverted";
            return false;
        }
        if (p != c) for (size_t k = 0; k < 5; ++k) std::swap(m[p][k], m[c][k]);
        for (size_t r = 0; r < 4; ++r) {
            if (r == c) continue;
            const double f = m[r][c] / m[c][c];
            for (size_t k = c; k < 5; ++k) m[r][k] -= f * m[c][k];
        }
    }
    for (size_t i = 0; i < 4; ++i) if (linear[i]) targets.erase(linear[i]->id);
    for (size_t k = 0; k < 4; ++k)
        if (!input[k].empty()) targets[input[k]] = m[k][4] / m[k][k];
    return true;
}

bool JPCell::doMoveNow(std::map<std::string, double> targets, double speed, std::string& why, bool squared) {
    if (!m_connected) { why = "not connected"; return false; }
    if (!m_homed)     { why = "not homed: home the machine first"; return false; }

    // Linear transform axes (OpenPnP's) solved back onto their input axes.
    if (!resolveLinear(targets, jogBase(), why)) return false;
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
        if (a->transformed()) {
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
    if (squared) hardware = toAxes(hardware, now);
    // Whole steps of an axis with a resolution: where it can actually stop;
    // the nearest, unless that is past a soft limit (a place at the limit
    // itself), then the nearest on this side of it.
    for (auto& [id, t] : hardware) {
        const JPAxisConfig* a = m_config.axis(id);
        const double r = a->resolution;
        if (r <= 0) continue;
        double stepped = std::round(t / r) * r;
        if (a->softLimitHighEnabled && stepped > a->softLimitHigh && t <= a->softLimitHigh) stepped = std::floor(t / r) * r;
        if (a->softLimitLowEnabled && stepped < a->softLimitLow && t >= a->softLimitLow) stepped = std::ceil(t / r) * r;
        t = stepped;
    }
    for (const auto& [id, t] : hardware) {
        const JPAxisConfig* hw = m_config.axis(id);
        if ((hw->softLimitLowEnabled && t < hw->softLimitLow) || (hw->softLimitHighEnabled && t > hw->softLimitHigh)) {
            why = "axis " + hw->name + " would go to " + format(t, 3) + ", outside its soft limits ("
                + format(hw->softLimitLow, 3) + " to " + format(hw->softLimitHigh, 3) + ")";
            return false;
        }
    }

    // Backlash (JPAxisConfig::Backlash), in the controllers' coordinates (an
    // axis's plus a directional offset in effect). One-sided: an axis that
    // would end travelling the wrong way goes past its target by the offset
    // first. Directional: one travelling the offset's way goes the offset
    // further; sneaking up, it first stops short by the sneak-up distance.
    // Then everything comes in to the targets at the slowest backlash speed
    // among those that went past or sneak up.
    std::map<std::string, double> overshoot = hardware;
    std::map<std::string, double> applied;   // directional offsets in effect after this move
    std::map<std::string, double> lagged;    // DistanceAware: each moved axis's lag after the move
    double approach = 1;
    bool needApproach = false;
    {
        const auto from = toAxes(now, now);
        for (auto& [id, t] : hardware) {
            const JPAxisConfig* a = m_config.axis(id);
            const auto f = from.find(id);
            const double travel = t - (f == from.end() ? t : f->second);
            const double before = backlashApplied(id);
            const double offset = a->backlashOffset;
            const bool aware = a->backlash == JPAxisConfig::Backlash::DistanceAware;
            if (!m_backlashOn || a->backlash == JPAxisConfig::Backlash::None || (aware ? a->backlashTable.empty() : offset == 0)) {
                if (before != 0) applied[id] = 0;
                continue;
            }
            if (aware) {
                if (travel == 0) continue;   // staying put keeps what is in effect
                // Where the drive is sent from, and how far it lags now (as
                // kept, else as last sent: the offset in effect).
                const double sentFrom = (f == from.end() ? t : f->second) + before;
                const auto kept = m_backlashLag.find(id);
                double lag = kept != m_backlashLag.end() ? kept->second : before;
                const int dir = travel > 0 ? 1 : -1;
                double driveFrom = sentFrom;
                // Coming in less than the least approach along the curve: back
                // off that far first (a waypoint, sent as it is).
                const bool backOff = a->travelFor(dir * lag) + std::abs(t - driveFrom) < a->approachMm;
                if (backOff) {
                    const double w = t - dir * a->approachMm;
                    lag = a->lagMoved(lag, driveFrom, w);
                    driveFrom = w;
                    overshoot[id] = w;
                    needApproach = true;
                    approach = std::min(approach, a->backlashSpeedFactor);
                }
                // Sent where it lands on the place: the lag on arriving depends
                // on how far it goes, which depends on the lag; a few rounds settle it.
                double end = lag;
                for (int round = 0; round < 4; ++round) end = a->lagMoved(lag, driveFrom, t + end);
                applied[id] = end;
                lagged[id] = end;
                t += end;
                if (!backOff) overshoot[id] = t;
                continue;
            }
            if (a->backlash == JPAxisConfig::Backlash::OneSided || a->backlash == JPAxisConfig::Backlash::OneSidedOptimized) {
                if (before != 0) applied[id] = 0;
                // Ending travel must be opposite to the offset's sign. One-sided:
                // always by way of the target plus the offset (unless it does not
                // move). Optimized: only a move of nothing, or the wrong way.
                if (a->backlash == JPAxisConfig::Backlash::OneSided ? travel == 0
                                                                     : travel != 0 && (travel > 0) == (offset < 0))
                    continue;
                overshoot[id] = t + offset;
                needApproach = true;
                approach = std::min(approach, a->backlashSpeedFactor);
                continue;
            }
            // Directional: the offset where the move travels its way; none the
            // other way; staying put keeps what is in effect.
            const double effective = travel == 0 ? before : (travel > 0) == (offset > 0) ? offset : 0;
            const double end = t + effective;
            applied[id] = effective;
            t = end;
            overshoot[id] = end;
            if (a->backlash == JPAxisConfig::Backlash::DirectionalSneakUp && travel != 0 && a->sneakUpMm > 0) {
                const double here = (f == from.end() ? end : f->second) + before;
                const double dir = travel > 0 ? 1 : -1;
                double shortOf = end - dir * a->sneakUpMm;
                // A move shorter than the sneak-up is all sneaking up.
                if ((shortOf - here) * dir < 0) shortOf = here;
                overshoot[id] = shortOf;
                needApproach = true;
                approach = std::min(approach, a->backlashSpeedFactor);
            }
        }
    }

    // One move per controller, at the slowest axis's rate (mm or degrees per
    // minute: the axis's own, else what the controller stores).
    std::map<std::string, std::vector<const JPAxisConfig*>> byDriver;
    for (const auto& [id, t] : hardware) byDriver[m_config.axis(id)->driverId].push_back(m_config.axis(id));
    auto send = [&](const std::map<std::string, double>& from, const std::map<std::string, double>& to, double factor,
                    std::vector<JPGcodeDriver*>& moved) {
        for (const auto& [driverId, all] : byDriver) {
            JPGcodeDriver* d = driver(driverId);
            if (!d) { why = "no controller " + driverId; return false; }
            // OpenPnP's Letter Variables off: the axes named by type ({X} {Y} {Z} {Rotation}),
            // so a move has one of each; several of a type (two nozzles' Zs) go one after another.
            std::vector<std::vector<const JPAxisConfig*>> commands;
            if (d->config().usingLetterVariables) {
                commands.push_back(all);
            } else {
                for (const JPAxisConfig* a : all) {
                    auto c = std::find_if(commands.begin(), commands.end(), [a](const auto& cmd) {
                        return std::none_of(cmd.begin(), cmd.end(), [a](const JPAxisConfig* b) { return b->type == a->type; });
                    });
                    if (c == commands.end()) commands.push_back({ a });
                    else c->push_back(a);
                }
            }
            for (const auto& axes : commands) {
            // OpenPnP's Pre-Move Commands: each moving axis's, {Coordinate} where it was.
            if (d->config().supportingPreMove && !d->config().usingLetterVariables)
                for (const JPAxisConfig* a : axes) {
                    if (a->preMoveCommand.empty()) continue;
                    const auto was = now.find(a->id);
                    const double v = was == now.end() ? 0 : (a->type == JPAxisConfig::Type::Rotation ? was->second : was->second * driverUnits(*d));
                    const JPReply pre = d->sendLines(JPFirmwareProfile::fill(a->preMoveCommand, { { "Coordinate", format(v, d->profile()->decimals()) } }));
                    if (!pre.ok) { why = a->name + ": its pre-move command was refused (" + pre.error + ")"; return false; }
                }
            // The feed rate as G-code reads F (NIST RS274NGC 2.1.2.5, as OpenPnP's Motion): over the
            // linear axes' path, or the rotational ones' when nothing linear moves; as fast as the
            // slowest axis allows for its part of the move (one not moving: its own rate).
            std::string words;
            double feed = 0, seconds = 0, linear2 = 0, rotational2 = 0;
            for (const JPAxisConfig* a : axes) {
                words += (words.empty() ? "" : " ") + word(*a, to.at(a->id), *d);
                double rate = a->feedratePerSecond * 60;
                // The controller's own (in its units), in the machine's.
                if (rate <= 0) rate = d->axisSetting("maxRate", a->letter).value_or(0) / driverUnits(*d);
                if (rate <= 0) { why = "axis " + a->name + " has no speed: neither the cell nor its controller gives one"; return false; }
                feed = feed <= 0 ? rate : std::min(feed, rate);
                const auto f = from.find(a->id);
                const double dist = f == from.end() ? 0 : std::abs(to.at(a->id) - f->second);
                if (dist <= 0) continue;
                seconds = std::max(seconds, dist / (rate / 60));
                (a->rotationalOnController() ? rotational2 : linear2) += dist * dist;
            }
            const bool linear = linear2 > 0;
            if (seconds > 0) feed = std::sqrt(linear ? linear2 : rotational2) / seconds * 60;
            // The controller's Max Feed Rate is for linear moves (a mm rate means nothing to a turn).
            if (const double cap = d->config().maxFeedRate; cap > 0 && (linear || seconds <= 0)) feed = std::min(feed, cap);
            const double k = std::clamp(speed, 0.0, 1.0) * m_speed * factor;
            feed *= k;
            // In the controller's units (Driver Settings' Units, as OpenPnP's driverUnitsFactor); a rotational path in degrees.
            const double u = driverUnits(*d);
            std::map<std::string, std::string> values{ { "axes", words }, { "feed", format(feed * (linear || seconds <= 0 ? u : 1), 0) } };
            // By type, for Letter Variables off ({X} 12.5); one not in this move left out with its letter.
            for (const auto& [type, name] : { std::pair { JPAxisConfig::Type::X, "X" }, std::pair { JPAxisConfig::Type::Y, "Y" },
                                              std::pair { JPAxisConfig::Type::Z, "Z" }, std::pair { JPAxisConfig::Type::Rotation, "Rotation" } }) {
                values[name] = JPFirmwareProfile::kLeaveOut;
                for (const JPAxisConfig* a : axes)
                    if (a->type == type) {
                        const std::string w = word(*a, to.at(a->id), *d);
                        values[name] = w.substr(a->letter.size());
                    }
            }
            // The slowest acceleration and jerk among the axes, for a command
            // that sets them ({acceleration}, {jerk}): scaled as the speed is,
            // so a slower move is the same move stretched in time.
            double accel = 0, jerk = 0;
            for (const JPAxisConfig* a : axes) {
                if (a->accelerationPerSecond2 > 0) accel = accel > 0 ? std::min(accel, a->accelerationPerSecond2) : a->accelerationPerSecond2;
                if (a->jerkPerSecond3 > 0) jerk = jerk > 0 ? std::min(jerk, a->jerkPerSecond3) : a->jerkPerSecond3;
            }
            if (accel > 0) values["acceleration"] = format(accel * k * k * u, 0);
            if (jerk > 0) values["jerk"] = format(jerk * k * k * k * u, 0);
            JLOGC(JPlacerLog::kCell, JLogLevel::Debug) << "move " << d->config().name << ": " << words << " F" << format(feed, 0);
            const JPReply r = d->command("move", values);
            if (!r.ok) { why = d->config().name + ": move refused (" + r.error + ")"; return false; }
            if (std::find(moved.begin(), moved.end(), d) == moved.end()) moved.push_back(d);
            }
        }
        return true;
    };
    // Test Motion's plan: each leg as long as its slowest axis takes, speeding
    // up and slowing down at its acceleration (a triangle when it is too short to reach its rate).
    if (m_planned) {
        auto legSeconds = [&](const std::map<std::string, double>& a, const std::map<std::string, double>& b, double factor) {
            double longest = 0;
            for (const auto& [id, to] : b) {
                const JPAxisConfig* ax = m_config.axis(id);
                const auto f = a.find(id);
                if (!ax || f == a.end()) continue;
                const double d = std::abs(to - f->second);
                JPGcodeDriver* dr = driver(ax->driverId);
                const double u = dr ? driverUnits(*dr) : 1;
                double v = ax->feedratePerSecond > 0 ? ax->feedratePerSecond : (dr ? dr->axisSetting("maxRate", ax->letter).value_or(0) / 60 / u : 0);
                double acc = ax->accelerationPerSecond2 > 0 ? ax->accelerationPerSecond2 : (dr ? dr->axisSetting("acceleration", ax->letter).value_or(0) / u : 0);
                const double k = std::clamp(speed, 0.0, 1.0) * m_speed * factor;
                v *= k;
                acc *= k * k;
                if (d <= 0 || v <= 0) continue;
                const double t = acc <= 0 ? d / v : d > v * v / acc ? d / v + v / acc : 2 * std::sqrt(d / acc);
                longest = std::max(longest, t);
            }
            return longest;
        };
        const auto from = toAxes(now, now);
        *m_planned += needApproach ? legSeconds(from, overshoot, 1) + legSeconds(overshoot, hardware, approach)
                                   : legSeconds(from, hardware, 1);
    }
    std::vector<JPGcodeDriver*> moved;
    // Each leg from where the controllers have the axes (the directional offsets in effect included).
    std::map<std::string, double> sentFrom = toAxes(now, now);
    for (auto& [id, v] : sentFrom) v += backlashApplied(id);
    if (needApproach && !send(sentFrom, overshoot, 1, moved)) return false;
    if (!send(needApproach ? overshoot : sentFrom, hardware, needApproach ? approach : 1, moved)) return false;
    // Continuous motion: not waited for now, but when the machine must stand still.
    if (m_config.motionPlanner.continuousMotion) {
        for (JPGcodeDriver* d : moved)
            if (std::find(m_inMotion.begin(), m_inMotion.end(), d->config().id) == m_inMotion.end()) m_inMotion.push_back(d->config().id);
        m_streaming = !m_inMotion.empty();
    } else {
        for (JPGcodeDriver* d : moved) {
            const JPReply w = d->waitForMotion();
            if (!w.ok) { why = d->config().name + ": move did not finish (" + w.error + ")"; return false; }
        }
    }
    // A rotation that both wraps and is limited, gone past +-180 the short
    // way: its controller is told it is at the same angle within the range,
    // once it is there.
    std::map<std::string, double> rewrapped;
    for (const auto& [id, sent] : hardware) {
        const JPAxisConfig* a = m_config.axis(id);
        // The axis's own angle: the controller's less a directional offset now in effect.
        const auto b = applied.find(id);
        const double offset = b != applied.end() ? b->second : backlashApplied(id);
        const double t = sent - offset;
        if (a->type != JPAxisConfig::Type::Rotation || !a->wrapAroundRotation || !a->limitRotation || std::abs(t) <= 180) continue;
        if (!doCoordinate("WaitForStillstand", why)) return false;
        JPGcodeDriver* d = driver(a->driverId);
        const double in = std::remainder(t, 360.0);
        const JPReply r = d->command("setPosition", { { "axes", word(*a, in + offset, *d) } });
        if (!r.ok) JLOGC(JPlacerLog::kCell, JLogLevel::Warn) << a->name << ": could not bring the rotation back into range (" << r.error << ")";
        else rewrapped[id] = in;
    }
    std::lock_guard lk(m_mutex);
    for (const auto& [id, b] : applied) {
        if (b == 0) m_backlashApplied.erase(id);
        else m_backlashApplied[id] = b;
    }
    for (const auto& [id, l] : lagged) m_backlashLag[id] = l;
    for (const auto& [id, t] : rewrapped) {
        hardware[id] = t;
        m_positions[id] = t;
        if (targets.count(id)) targets[id] = t;
    }
    for (const auto& [id, t] : virtuals) m_positions[id] = t;
    for (const auto& [id, t] : hardware)
        if (const JPAxisConfig* a = m_config.axis(id); a && sharesLetter(*a)) m_positions[id] = targets.count(id) ? targets.at(id) : t;
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
    // Moves not waited for (continuous motion): the axes are where they were sent.
    const bool streaming = m_streaming;
    for (const auto& [id, sent] : m_sent)
        if (const auto p = out.find(id); p != out.end() && (streaming || std::abs(p->second - sent) <= kSettledTolerance))
            p->second = sent;
    return out;
}

bool JPCell::finished(bool ok, std::string& why) {
    if (m_inMotion.empty()) return ok;
    std::string w;
    if (!doCoordinate("WaitForStillstand", w) && ok) {
        why = w;
        return false;
    }
    return ok;
}

bool JPCell::doCoordinate(const std::string& how, std::string& why) {
    if (how == "None") return true;
    std::vector<std::string> ids;
    if (how == "WaitForUnconditionalCoordination") {
        for (const JPDriverConfig& d : m_config.drivers) ids.push_back(d.id);
    } else {
        ids = m_inMotion;
    }
    m_inMotion.clear();
    m_streaming = false;
    for (const std::string& id : ids) {
        JPGcodeDriver* d = driver(id);
        if (!d || !d->isConnected()) continue;
        // Told to finish its moves (a CommandStillstand), or waited for and its position read.
        const JPReply w = how == "CommandStillstand" ? d->command("waitMotion", {}, d->config().homeTimeoutMs) : d->waitForMotion();
        if (!w.ok) { why = d->config().name + ": move did not finish (" + w.error + ")"; return false; }
    }
    return true;
}

void JPCell::setBacklash(const std::string& axisId, JPAxisConfig::Backlash method, double offset, double sneakUpMm,
                         double speedFactor, std::vector<std::pair<double, double>> table, double approachMm) {
    m_thread.post([this, axisId, method, offset, sneakUpMm, speedFactor, table = std::move(table), approachMm] {
        std::lock_guard lk(m_mutex);
        for (JPAxisConfig& a : m_config.axes)
            if (a.id == axisId) {
                a.backlash = method;
                a.backlashOffset = offset;
                a.sneakUpMm = sneakUpMm;
                a.backlashSpeedFactor = speedFactor;
                a.backlashTable = table;
                a.approachMm = approachMm;
            }
    });
}

std::map<std::string, double> JPCell::reportedPositions() const {
    std::lock_guard lk(m_mutex);
    return m_reported;
}

std::map<std::string, double> JPCell::positions() const {
    std::lock_guard lk(m_mutex);
    return m_positions;
}

std::map<std::string, std::string> JPCell::firmware() const {
    std::lock_guard lk(m_mutex);
    return m_firmware;
}

std::map<std::string, std::string> JPCell::firmwareIdentity() const {
    std::lock_guard lk(m_mutex);
    return m_firmwareIdentity;
}

} // inline namespace jf
