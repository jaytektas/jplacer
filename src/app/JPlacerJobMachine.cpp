// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerJobMachine.h"

#include "common/JPlacerLog.h"
#include "camera/JPImageFile.h"
#include "tasks/JPCameraLook.h"
#include "tasks/JPTipChanger.h"
#include "vision/JPRoundMarkFinder.h"

#include <j/core/Log.h>

#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

// How far from where it should be a fiducial is looked for at first, then
// once centred on (mm).
constexpr double kFirstSearchMm = 4.0;
constexpr double kSearchMm      = 1.0;

using Where = std::array<std::optional<double>, 4>;

Where where(const JPLocation& l) {
    const JPLocation m = l.convertToUnits(JPLengthUnit::Millimeters);
    return { m.x(), m.y(), m.z(), m.rotation() };
}

} // namespace

JPlacerJobMachine::JPlacerJobMachine(JPlacerMachine& machine, OnMain onMain, std::function<bool(const std::string&)> ask,
                                     std::function<void(const std::string&)> progress)
    : m_machine(machine), m_onMain(std::move(onMain)), m_ask(std::move(ask)), m_progress(std::move(progress)) {}

JPCellConfig JPlacerJobMachine::config() const {
    JPCellConfig c;
    m_onMain([&] {
        if (const JPCell* cell = m_machine.cell()) c = cell->config();
    });
    return c;
}

JPCell* JPlacerJobMachine::cell(std::string& why) const {
    JPCell* c = nullptr;
    m_onMain([&] { c = m_machine.cell(); });
    if (!c) why = "no machine is open";
    return c;
}

std::string JPlacerJobMachine::headId(const JPCellConfig& c) const {
    for (const JPCameraConfig& cam : c.cameras)
        if (!cam.mount.headId.empty()) return cam.mount.headId;
    return c.heads.empty() ? std::string() : c.heads.front().id;
}

std::vector<JPJobMachine::Nozzle> JPlacerJobMachine::nozzles() const {
    const JPCellConfig c = config();
    const std::string head = headId(c);
    std::vector<Nozzle> out;
    for (const JPNozzleConfig& n : c.nozzles)
        if (n.mount.headId == head) out.push_back({ n.id, n.name.empty() ? n.id : n.name, n.tipId, n.tipIds });
    return out;
}

std::vector<std::pair<std::string, std::string>> JPlacerJobMachine::tips() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const JPNozzleTipConfig& t : config().nozzleTips) out.emplace_back(t.id, t.name.empty() ? t.id : t.name);
    return out;
}

std::optional<JPLocation> JPlacerJobMachine::cameraLocation() const {
    std::optional<JPLocation> l;
    m_onMain([&] { l = m_machine.toolLocation(JPSetupForm::Tool::Camera); });
    return l;
}

bool JPlacerJobMachine::safeZ(std::string& why) {
    JPCell* c = cell(why);
    return c && c->safeZAndWait(headId(config()), 1.0, why);
}

bool JPlacerJobMachine::changeTip(const std::string& nozzleId, const std::string& tipId, std::string& why) {
    ++m_motions;
    std::string refused;
    m_onMain([&] { refused = m_machine.tipChangeRefusal(nozzleId, tipId); });
    if (!refused.empty()) {
        why = refused;
        return false;
    }
    JPCell* c = cell(why);
    if (!c) return false;
    const JPCellConfig names = config();
    JPNozzleConfig nozzle;
    for (const JPNozzleConfig& n : names.nozzles)
        if (n.id == nozzleId) nozzle = n;
    auto tipOf = [&names](const std::string& id) -> const JPNozzleTipConfig* {
        for (const JPNozzleTipConfig& t : names.nozzleTips)
            if (t.id == id) return &t;
        return nullptr;
    };
    auto nameOf = [](const JPNozzleTipConfig* t) { return t ? (t->name.empty() ? t->id : t->name) : std::string("no tip"); };
    // The tip on it taken off, then the one wanted put on; each half kept when done.
    struct Half {
        std::string                what, after;
        std::vector<JPChangerStep> steps;
    };
    std::vector<Half> halves;
    if (const JPNozzleTipConfig* on = tipOf(nozzle.tipId))
        halves.push_back({ "Unloading " + nameOf(on) + " from " + nozzle.name, "", on->unloadingSteps() });
    if (const JPNozzleTipConfig* wanted = tipOf(tipId))
        halves.push_back({ "Loading " + nameOf(wanted) + " on " + nozzle.name, tipId, wanted->loadSteps });
    JPTipChanger::Hooks hooks;
    hooks.ask = m_ask;
    hooks.progress = m_progress;
    for (const Half& h : halves) {
        if (!JPTipChanger::run(*c, names, nozzle, h.steps, h.what, false, hooks, why)) {
            why = h.what + ": " + why + ". Look at " + nozzle.name +
                  " and say which tip is on it (the Jog panel's tip menu, Manual Change): that moves nothing.";
            return false;
        }
        m_onMain([&] { m_machine.setTipOn(nozzle.id, h.after); });
    }
    return true;
}

bool JPlacerJobMachine::rotate(const std::string& nozzleId, double angle, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    if (!c) return false;
    for (const JPNozzleConfig& n : config().nozzles)
        if (n.id == nozzleId) {
            if (n.mount.axisRotation.empty()) return true;
            return c->moveAxesAndWait({ { n.mount.axisRotation, angle } }, 1.0, why);
        }
    why = "no nozzle " + nozzleId;
    return false;
}

bool JPlacerJobMachine::pick(const std::string& nozzleId, const JPLocation& at, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    return c && c->pickAtAndWait(nozzleId, where(at), 1.0, why);
}

bool JPlacerJobMachine::place(const std::string& nozzleId, const JPLocation& at, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    return c && c->placeAtAndWait(nozzleId, where(at), 1.0, why);
}

bool JPlacerJobMachine::discard(const std::string& nozzleId, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    return c && c->discardAndWait(nozzleId, 1.0, why);
}

bool JPlacerJobMachine::positionNozzle(const std::string& nozzleId, const JPLocation& at, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    if (!c) return false;
    for (const JPNozzleConfig& n : config().nozzles)
        if (n.id == nozzleId) {
            const JPLocation m = at.convertToUnits(JPLengthUnit::Millimeters);
            return c->moveToolAndWait(n.mount, { m.x(), m.y(), std::nullopt, m.rotation() }, 1.0, why);
        }
    why = "no nozzle " + nozzleId;
    return false;
}

bool JPlacerJobMachine::actuate(const std::string& actuatorName, double value, std::string& why) {
    JPCell* c = cell(why);
    if (!c) return false;
    const JPCellConfig cfg = config();
    const JPActuatorConfig* actuator = cfg.actuatorNamed(actuatorName);
    if (!actuator) {
        why = "Unable to find an actuator named " + actuatorName;
        return false;
    }
    if (actuator->valueType == JPActuatorConfig::ValueType::Boolean) return c->switchActuatorAndWait(actuator->id, value != 0, why);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", value);
    return c->setActuatorAndWait(actuator->id, buf, why);
}

bool JPlacerJobMachine::actuateText(const std::string& actuatorName, const std::string& value, std::string& why) {
    JPCell* c = cell(why);
    if (!c) return false;
    const JPCellConfig cfg = config();
    const JPActuatorConfig* actuator = cfg.actuatorNamed(actuatorName);
    if (!actuator) {
        why = "Unable to find an actuator named " + actuatorName;
        return false;
    }
    return c->setActuatorAndWait(actuator->id, value, why);
}

bool JPlacerJobMachine::readActuator(const std::string& actuatorName, double parameter, std::string& value, std::string& why) {
    JPCell* c = cell(why);
    if (!c) return false;
    const JPCellConfig cfg = config();
    const JPActuatorConfig* actuator = cfg.actuatorNamed(actuatorName);
    if (!actuator) {
        why = "Unable to find an actuator named " + actuatorName;
        return false;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", parameter);
    return c->readActuatorAndWait(actuator->id, std::string(buf), value, why);
}

bool JPlacerJobMachine::moveActuator(const std::string& actuatorName, const JPLocation& at, bool withZ, double speed,
                                     std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    if (!c) return false;
    const JPCellConfig cfg = config();
    const JPActuatorConfig* actuator = cfg.actuatorNamed(actuatorName);
    if (!actuator || actuator->mount.headId.empty()) {
        why = "No Actuator found with name " + actuatorName + " on the head";
        return false;
    }
    const JPLocation m = at.convertToUnits(JPLengthUnit::Millimeters);
    return c->moveToolStraightAndWait(actuator->mount, { m.x(), m.y(), withZ ? std::optional(m.z()) : std::nullopt, std::nullopt },
                                      speed, why);
}

bool JPlacerJobMachine::matchTemplate(const JPLocation& at, const std::string& templatePath,
                                      const JPTemplateFinder::Area& area, JPLocation& offset, std::string& why) {
    ++m_motions;
    JPFrame frame;
    if (!JPImageFile::readPng(templatePath, frame, why)) return false;
    const JPGrayImage templ = JPGrayImage::fromRgba(frame.rgba.data(), frame.width, frame.height);
    JPCell* c = cell(why);
    if (!c) return false;
    JPCameraFeed* feed = nullptr;
    m_onMain([&] { feed = m_machine.headCameraFeed(); });
    if (!feed) {
        why = "No vision capable camera found on head.";
        return false;
    }
    prepare(*c, *feed);
    JPCameraCalibration cal;
    if (!JPCameraLook::calibration(*c, *feed, cal, why)) return false;
    const JPLocation m = at.convertToUnits(JPLengthUnit::Millimeters);
    if (!c->moveToolAndWait(feed->config().mount, { m.x(), m.y(), std::nullopt, std::nullopt }, 1.0, why)) return false;
    JPGrayImage img;
    if (!JPCameraLook::settled(*feed, img, why)) return false;
    const JPTemplateFinder::Result r = JPTemplateFinder::find(img, templ, area);
    if (!r.found) {
        why = r.why;
        return false;
    }
    double fx = 0, fy = 0;
    if (!cal.machinePoint(r.x + templ.width / 2.0, r.y + templ.height / 2.0, m.x(), m.y(), fx, fy)) {
        why = "the camera's calibration cannot place the match on the machine";
        return false;
    }
    JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "template matched at " << fx << ", " << fy << " (score " << r.score << ")";
    offset = JPLocation(JPLengthUnit::Millimeters, m.x() - fx, m.y() - fy, 0, 0);
    return true;
}

bool JPlacerJobMachine::park(std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    return c && c->parkAndWait(headId(config()), 1.0, why);
}

bool JPlacerJobMachine::locateFiducial(const JPLocation& nominal, double diameterMm, const FiducialLook& lookAt,
                                       JPLocation& found, std::string& why) {
    ++m_motions;
    const JPLocation start = nominal.convertToUnits(JPLengthUnit::Millimeters);
    double x = start.x(), y = start.y();
    // From either side with a parallax diameter, the nearer first; the two averaged.
    const double r = lookAt.parallaxDiameterMm / 2, a = lookAt.parallaxAngle * M_PI / 180;
    double dx = r * std::cos(a), dy = r * std::sin(a);
    if (r > 0)
        if (const auto cam = cameraLocation()) {
            const JPLocation m = cam->convertToUnits(JPLengthUnit::Millimeters);
            if (std::hypot(x + dx - m.x(), y + dy - m.y()) > std::hypot(x - dx - m.x(), y - dy - m.y())) {
                dx = -dx;
                dy = -dy;
            }
        }
    for (int pass = 0; pass < std::max(1, lookAt.passes); ++pass) {
        const double search = pass == 0 ? kFirstSearchMm : kSearchMm;
        double fx = 0, fy = 0;
        if (r == 0) {
            if (!look(x, y, x, y, diameterMm, search, fx, fy, why)) return false;
        } else {
            double ax = 0, ay = 0, bx = 0, by = 0;
            if (!look(x + dx, y + dy, x, y, diameterMm, search, ax, ay, why)) return false;
            if (!look(x - dx, y - dy, x, y, diameterMm, search, bx, by, why)) return false;
            fx = (ax + bx) / 2;
            fy = (ay + by) / 2;
        }
        const double moved = std::hypot(fx - x, fy - y);
        x = fx;
        y = fy;
        JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "fiducial pass " << pass + 1 << ": " << fx << ", " << fy << " (moved "
                                                  << moved << " mm)";
        if (moved < lookAt.maxLinearOffsetMm) break;
    }
    found = JPLocation(JPLengthUnit::Millimeters, x, y, start.z(), start.rotation());
    return true;
}

void JPlacerJobMachine::prepare(JPCell& cell, JPCameraFeed& feed) {
    const std::string id = feed.config().id;
    m_onMain([&] { m_machine.showCamera(id); });
    const std::string light = feed.config().lightActuator();
    std::string why;
    if (!light.empty() && feed.config().light.beforeCapture) cell.switchActuatorAndWait(light, true, why);
}

bool JPlacerJobMachine::look(double viewX, double viewY, double x, double y, double diameterMm, double searchMm,
                             double& foundX, double& foundY, std::string& why) {
    JPCell* c = cell(why);
    if (!c) return false;
    JPCameraFeed* feed = nullptr;
    m_onMain([&] { feed = m_machine.headCameraFeed(); });
    if (!feed) {
        why = "no camera on the head";
        return false;
    }
    prepare(*c, *feed);
    JPCameraCalibration cal;
    if (!JPCameraLook::calibration(*c, *feed, cal, why)) return false;
    if (!c->moveToolAndWait(feed->config().mount, { viewX, viewY, std::nullopt, std::nullopt }, 1.0, why)) return false;
    JPGrayImage img;
    if (!JPCameraLook::settled(*feed, img, why)) return false;
    JPRoundMarkFinder::Request rq;
    if (!cal.pixelFor(x, y, viewX, viewY, rq.expectedX, rq.expectedY)) {
        why = "the camera's calibration cannot place it in the picture";
        return false;
    }
    const double scale = std::sqrt(cal.scaleX() * cal.scaleY());
    rq.searchRadius = searchMm * scale;
    rq.diameter = diameterMm * scale;
    const JPRoundMark m = JPCameraLook::findTryingHarder(*c, *feed, img, rq);
    if (!m.found) {
        why = m.why;
        return false;
    }
    return cal.machinePoint(m.x, m.y, viewX, viewY, foundX, foundY);
}

bool JPlacerJobMachine::locateHole(const JPLocation& nominal, double diameterMm, double searchMm, double parallaxDiameterMm,
                                   double parallaxAngle, JPLocation& found, std::string& why) {
    ++m_motions;
    const JPLocation n = nominal.convertToUnits(JPLengthUnit::Millimeters);
    double x = 0, y = 0;
    if (parallaxDiameterMm == 0) {
        if (!look(n.x(), n.y(), n.x(), n.y(), diameterMm, searchMm, x, y, why)) return false;
    } else {
        // From either side, the nearer first; the two finds averaged.
        const double r = parallaxDiameterMm / 2, a = parallaxAngle * M_PI / 180;
        double dx = r * std::cos(a), dy = r * std::sin(a);
        if (const auto cam = cameraLocation()) {
            const JPLocation m = cam->convertToUnits(JPLengthUnit::Millimeters);
            if (std::hypot(n.x() + dx - m.x(), n.y() + dy - m.y()) > std::hypot(n.x() - dx - m.x(), n.y() - dy - m.y())) {
                dx = -dx;
                dy = -dy;
            }
        }
        double ax = 0, ay = 0, bx = 0, by = 0;
        if (!look(n.x() + dx, n.y() + dy, n.x(), n.y(), diameterMm, searchMm, ax, ay, why)) return false;
        if (!look(n.x() - dx, n.y() - dy, n.x(), n.y(), diameterMm, searchMm, bx, by, why)) return false;
        x = (ax + bx) / 2;
        y = (ay + by) / 2;
    }
    found = JPLocation(JPLengthUnit::Millimeters, x, y, n.z(), n.rotation());
    return true;
}

bool JPlacerJobMachine::alignPart(const std::string& nozzleId, const AlignRequest& rq, AlignResult& result,
                                  std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    if (!c) return false;
    JPCameraFeed* feed = nullptr;
    JPNozzleConfig nozzle;
    m_onMain([&] {
        feed = m_machine.upCameraFeed();
        if (const JPCell* cc = m_machine.cell())
            for (const JPNozzleConfig& n : cc->config().nozzles)
                if (n.id == nozzleId) nozzle = n;
    });
    if (!feed) {
        why = "no camera looking up to align parts with";
        return false;
    }
    if (nozzle.id.empty()) {
        why = "no nozzle " + nozzleId;
        return false;
    }
    prepare(*c, *feed);
    JPCameraCalibration cal;
    if (!JPCameraLook::calibration(*c, *feed, cal, why)) return false;
    // Where the camera looks; the part's bottom at its focus.
    const JPMountConfig& cm = feed->config().mount;
    const double camX = cm.offsetX, camY = cm.offsetY, camZ = cm.offsetZ;
    double nx = camX, ny = camY, nr = rq.imageAngle;
    for (int pass = 0; pass < std::max(1, rq.passes); ++pass) {
        if (!c->moveToolAndWait(nozzle.mount, { nx, ny, camZ + rq.partHeightMm, nr }, 1.0, why)) return false;
        JPGrayImage img;
        if (!JPCameraLook::settled(*feed, img, why)) return false;
        JPPartFinder::Request fr;
        if (!cal.pixelFor(nx, ny, camX, camY, fr.expectedX, fr.expectedY)) {
            why = "the camera's calibration cannot place the nozzle in its picture";
            return false;
        }
        // The part's angle as it sits: the nozzle's turn, less what is already known to be off.
        fr.angle = nr + (pass == 0 ? 0 : result.partAngle - result.nozzleAngle);
        fr.angleRange = pass == 0 ? rq.angleRange : std::min(rq.angleRange, 3.0);
        fr.toMachine = [&cal, camX, camY](double px, double py, double& mx, double& my) {
            return cal.machinePoint(px, py, camX, camY, mx, my);
        };
        const JPPartFinder::Result found = JPPartFinder::find(img, rq.shape, fr);
        if (!found.found) {
            why = "the part was not found: " + found.why;
            return false;
        }
        double px = 0, py = 0;
        if (!cal.machinePoint(found.x, found.y, camX, camY, px, py)) {
            why = "the camera's calibration cannot place the part";
            return false;
        }
        result.nozzleAngle = nr;
        result.dx = px - nx;
        result.dy = py - ny;
        result.partAngle = found.angle;
        JLOGC(JPlacerLog::kJob, JLogLevel::Info) << "aligned on " << nozzle.name << ": " << result.dx << ", " << result.dy
                                                 << " mm, " << (found.angle - nr) << " deg (score " << found.score << ")";
        // Centred and square on the camera: done; else the nozzle moved to put it there and looked at again.
        const double off = std::hypot(px - camX, py - camY), turn = std::abs(found.angle - rq.imageAngle);
        if (pass + 1 >= rq.passes || (off < rq.maxLinearOffsetMm && turn < 0.1)) break;
        nx = camX - result.dx;
        ny = camY - result.dy;
        nr = nr - (found.angle - rq.imageAngle);
    }
    return true;
}

} // inline namespace jf
