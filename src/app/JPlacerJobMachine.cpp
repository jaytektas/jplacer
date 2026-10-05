// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerJobMachine.h"

#include <opencv2/objdetect.hpp>

#include "pipeline/JPStageUtil.h"
#include "tasks/JPVisionPipelinePrep.h"
#include "ui/JPCameraView.h"

#include <opencv2/imgproc.hpp>

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
// How long a pipeline's picture of a fiducial found is shown on the camera (OpenPnP's).
constexpr int kShownPipelineMs = 1500;
// OpenPnP's bottom vision takes a found rectangle's angle within this of the
// one wanted (Rotation: Adjust), the sides being alike to it.
constexpr double kAdjustRange = 45;
// A step along a found rectangle's angle, to turn it into the machine's angle.
constexpr double kAngleStepPx = 100;

using Where = std::array<std::optional<double>, 4>;

Where where(const JPLocation& l) {
    const JPLocation m = l.convertToUnits(JPLengthUnit::Millimeters);
    return { m.x(), m.y(), m.z(), m.rotation() };
}

} // namespace

JPlacerJobMachine::JPlacerJobMachine(JPlacerMachine& machine, JPConfiguration& config, OnMain onMain,
                                     std::function<bool(const std::string&)> ask, std::function<void(const std::string&)> progress)
    : m_machine(machine), m_config(config), m_onMain(std::move(onMain)), m_ask(std::move(ask)), m_progress(std::move(progress)) {}

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
    for (const JPNozzleConfig& n : c.nozzles) {
        if (n.mount.headId != head) continue;
        Nozzle out1 { n.id, n.name.empty() ? n.id : n.name, n.tipId, n.tipIds, n.pickDwellMs, n.placeDwellMs };
        out1.rotationMode = n.rotationMode;
        out1.tipChangeOnManualPick = n.tipChangeOnManualPick;
        out1.maxPickArticulation = n.maxPickArticulation;
        out1.maxAlignArticulation = n.maxAlignArticulation;
        // Its rotation axis's range, as OpenPnP's getRotationModeLimits: its soft limits when limited to range.
        if (const JPAxisConfig* r = c.axis(n.mount.axisRotation); r && r->limitRotation) {
            out1.rotationLow = r->softLimitLowEnabled ? r->softLimitLow : -180;
            out1.rotationHigh = r->softLimitHighEnabled ? r->softLimitHigh : 180;
            if (out1.rotationLow > out1.rotationHigh) std::swap(out1.rotationLow, out1.rotationHigh);
        }
        for (const JPNozzleTipConfig& t : c.nozzleTips)
            if (t.id == n.tipId) {
                out1.pickDwellMs += t.pickDwellMs;
                out1.placeDwellMs += t.placeDwellMs;
                out1.tipMaxPartHeightMm = t.maxPartHeightMm;
            }
        out1.contactProbe = n.contactProbe;
        out.push_back(std::move(out1));
    }
    return out;
}

std::optional<double> JPlacerJobMachine::nozzleRotation(const std::string& nozzleId) const {
    const JPCellConfig c = config();
    for (const JPNozzleConfig& n : c.nozzles)
        if (n.id == nozzleId && !n.mount.axisRotation.empty()) {
            std::optional<double> at;
            m_onMain([&] {
                if (const JPCell* cell = m_machine.cell()) {
                    const auto p = cell->positions();
                    if (const auto i = p.find(n.mount.axisRotation); i != p.end()) at = i->second;
                }
            });
            return at;
        }
    return std::nullopt;
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

bool JPlacerJobMachine::cameraReaches(const JPLocation& at) const {
    bool reaches = true;
    m_onMain([&] {
        const JPCell* c = m_machine.cell();
        const JPCameraFeed* feed = m_machine.headCameraFeed();
        if (!c || !feed) return;
        const JPLocation m = at.convertToUnits(JPLengthUnit::Millimeters);
        reaches = c->reaches(feed->config().mount, m.x(), m.y());
    });
    return reaches;
}

JPJobMachine::TipPush JPlacerJobMachine::tipPush(const std::string& tipId) const {
    for (const JPNozzleTipConfig& t : config().nozzleTips)
        if (t.id == tipId) return { t.pushAndDragAllowed, t.diameterLowMm };
    return {};
}

std::string JPlacerJobMachine::holdingPart(const std::string& nozzleId) const {
    std::string part;
    m_onMain([&] { part = m_machine.nozzlePart(nozzleId); });
    return part;
}

std::string JPlacerJobMachine::chosenNozzle() const {
    std::string id;
    m_onMain([&] { id = m_machine.chosenNozzleId(); });
    return id;
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
    // OpenPnP's manual change: with the tool changer off (or a tip with no steps), asked to be done by hand, in its words.
    if (!nozzle.changerEnabled || std::any_of(halves.begin(), halves.end(), [](const Half& h) { return h.steps.empty(); })) {
        std::string instructions;
        if (const JPNozzleTipConfig* on = tipOf(nozzle.tipId))
            instructions += "\na manual nozzle tip " + nameOf(on) + " unload from nozzle " + nozzle.name + " and";
        if (const JPNozzleTipConfig* wanted = tipOf(tipId))
            instructions += "\na manual nozzle tip " + nameOf(wanted) + " load on nozzle " + nozzle.name + " now.";
        if (!c->safeZAndWait(nozzle.mount.headId, 1.0, why)) return false;
        if (const auto& l = nozzle.manualChangeLocation; l && !c->moveToolAndWait(nozzle.mount, { l->x, l->y, l->z, l->rotation }, 1.0, why))
            return false;
        if (!m_ask || !m_ask("Please perform" + instructions)) {
            why = "the nozzle tip change by hand was not done";
            return false;
        }
        for (const Half& h : halves) m_onMain([&] { m_machine.setTipOn(nozzle.id, h.after); });
        return true;
    }
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

bool JPlacerJobMachine::contactProbe(const std::string& nozzleId, bool forward, double depthMm, double& probedZ, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    return c && c->contactProbeAndWait(nozzleId, forward, depthMm, probedZ, why);
}

std::optional<double> JPlacerJobMachine::probedOffset(const std::string& nozzleId, bool feeder, const std::string& key) const {
    std::optional<double> out;
    m_onMain([&] {
        if (const JPCell* c = m_machine.cell()) out = c->probedOffset(nozzleId, feeder, key);
    });
    return out;
}

void JPlacerJobMachine::setProbedOffset(const std::string& nozzleId, bool feeder, const std::string& key, double offsetMm) {
    m_onMain([&] {
        if (JPCell* c = m_machine.cell()) c->setProbedOffset(nozzleId, feeder, key, offsetMm);
    });
}

void JPlacerJobMachine::holding(const std::string& nozzleId, const std::string& partId) {
    m_onMain([&] { m_machine.setNozzlePart(nozzleId, partId); });
}

bool JPlacerJobMachine::discard(const std::string& nozzleId, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    return c && c->discardAndWait(nozzleId, 1.0, why);
}

bool JPlacerJobMachine::positionCamera(const JPLocation& at, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    if (!c) return false;
    JPCameraFeed* feed = nullptr;
    m_onMain([&] { feed = m_machine.headCameraFeed(); });
    if (!feed) {
        why = "no camera on the head";
        return false;
    }
    const JPLocation m = at.convertToUnits(JPLengthUnit::Millimeters);
    return c->moveToolAndWait(feed->config().mount, { m.x(), m.y(), std::nullopt, std::nullopt }, 1.0, why);
}

bool JPlacerJobMachine::moveNozzle(const std::string& nozzleId, std::array<std::optional<double>, 4> to, double speed,
                                   bool safeZFirst, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    if (!c) return false;
    for (const JPNozzleConfig& n : config().nozzles)
        if (n.id == nozzleId)
            return safeZFirst ? c->moveToolAndWait(n.mount, to, speed, why) : c->moveToolStraightAndWait(n.mount, to, speed, why);
    why = "no nozzle " + nozzleId;
    return false;
}

bool JPlacerJobMachine::vacuumOn(const std::string& nozzleId, std::string& why) {
    JPCell* c = cell(why);
    return c && c->vacuumOnAndWait(nozzleId, why);
}

bool JPlacerJobMachine::pickHere(const std::string& nozzleId, std::string& why) {
    JPCell* c = cell(why);
    return c && c->pickAndWait(nozzleId, why);
}

bool JPlacerJobMachine::readVacuum(const std::string& nozzleId, double& level, std::string& why) {
    JPCell* c = cell(why);
    return c && c->readVacuumAndWait(nozzleId, level, why);
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
    // A switch, or a profile actuator (its Default ON or OFF profile), switched by whether it is 0.
    if (actuator->valueType == JPActuatorConfig::ValueType::Boolean || actuator->valueType == JPActuatorConfig::ValueType::Profile)
        return c->switchActuatorAndWait(actuator->id, value != 0, why);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", value);
    return c->setActuatorAndWait(actuator->id, buf, why);
}

bool JPlacerJobMachine::isHomed() const {
    bool homed = false;
    m_onMain([&] { homed = m_machine.cell() && m_machine.cell()->isHomed(); });
    return homed;
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

bool JPlacerJobMachine::readActuator(const std::string& actuatorName, const std::string& parameter, std::string& value,
                                     std::string& why) {
    JPCell* c = cell(why);
    if (!c) return false;
    const JPCellConfig cfg = config();
    const JPActuatorConfig* actuator = cfg.actuatorNamed(actuatorName);
    if (!actuator) {
        why = "Unable to find an actuator named " + actuatorName;
        return false;
    }
    return c->readActuatorAndWait(actuator->id, parameter, value, why);
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

bool JPlacerJobMachine::positionActuator(const std::string& actuatorName, std::array<std::optional<double>, 4> to, double speed,
                                         bool safeZFirst, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    if (!c) return false;
    const JPCellConfig cfg = config();
    const JPActuatorConfig* actuator = cfg.actuatorNamed(actuatorName);
    if (!actuator || actuator->mount.headId.empty()) {
        why = "No Actuator found with name " + actuatorName + " on the head";
        return false;
    }
    return safeZFirst ? c->moveToolAndWait(actuator->mount, to, speed, why) : c->moveToolStraightAndWait(actuator->mount, to, speed, why);
}

bool JPlacerJobMachine::zeroActuatorRotation(const std::string& actuatorName, std::string& why) {
    JPCell* c = cell(why);
    if (!c) return false;
    const JPCellConfig cfg = config();
    const JPActuatorConfig* actuator = cfg.actuatorNamed(actuatorName);
    if (!actuator || actuator->mount.axisRotation.empty()) return true;
    const std::map<std::string, double> now = c->positions();
    const auto at = now.find(actuator->mount.axisRotation);
    if (at == now.end() || at->second == 0) return true;
    return c->correctPosition({ { at->first, at->second } }, why);
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
    const JPTemplateFinder::Result r = JPTemplateFinder::find(img, templ, area.placed(img.width, img.height));
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
    // One look from a view point: by the pipeline, else by jplacer's finder.
    const double nx = start.x(), ny = start.y();
    auto once = [&](double vx, double vy, double search, double& fx, double& fy) {
        return lookAt.pipeline ? lookByPipeline(vx, vy, nx, ny, lookAt, fx, fy, why)
                               : look(vx, vy, x, y, diameterMm, search, fx, fy, why);
    };
    // OpenPnP's averaging: every pass after the first kept, then averaged.
    double sumX = 0, sumY = 0;
    int kept = 0;
    for (int pass = 0; pass < std::max(1, lookAt.passes); ++pass) {
        const double search = pass == 0 ? kFirstSearchMm : kSearchMm;
        double fx = 0, fy = 0;
        if (r == 0) {
            if (!once(x, y, search, fx, fy)) return false;
        } else {
            double ax = 0, ay = 0, bx = 0, by = 0;
            if (!once(x + dx, y + dy, search, ax, ay)) return false;
            if (!once(x - dx, y - dy, search, bx, by)) return false;
            fx = (ax + bx) / 2;
            fy = (ay + by) / 2;
            // The next pass from the other side first.
            dx = -dx;
            dy = -dy;
        }
        const double moved = std::hypot(fx - x, fy - y);
        x = fx;
        y = fy;
        JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "fiducial pass " << pass + 1 << ": " << fx << ", " << fy << " (moved "
                                                  << moved << " mm)";
        if (moved < lookAt.maxLinearOffsetMm) break;
        if (pass > 0) {
            sumX += x;
            sumY += y;
            ++kept;
        }
    }
    if (lookAt.averaging && kept >= 2) {
        x = sumX / kept;
        y = sumY / kept;
        JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "fiducial averaged at " << x << ", " << y;
    }
    found = JPLocation(JPLengthUnit::Millimeters, x, y, start.z(), start.rotation());
    return true;
}

bool JPlacerJobMachine::headCameraPipeline(double viewX, double viewY, JPPipeline& p, JPCameraCalibration& cal, JPCameraFeed*& feed,
                                           std::string& why) {
    JPCell* c = cell(why);
    if (!c) return false;
    feed = nullptr;
    m_onMain([&] { feed = m_machine.headCameraFeed(); });
    if (!feed) {
        why = "no camera on the head";
        return false;
    }
    prepare(*c, *feed);
    if (!JPCameraLook::calibration(*c, *feed, cal, why)) return false;
    if (!c->moveToolAndWait(feed->config().mount, { viewX, viewY, std::nullopt, std::nullopt }, 1.0, why)) return false;
    JPPipeline::Context& ctx = p.context();
    ctx.capture = [feed](const std::string& settle, const std::string&, cv::Mat& bgr, std::string& w) {
        JPGrayImage settled;
        if (settle != "Skip" && !JPCameraLook::settled(*feed, settled, w)) return false;
        JPFrame frame;
        if (!feed->latest(frame, 0) || frame.width <= 0) {
            w = feed->config().name + " gives no picture";
            return false;
        }
        cv::Mat rgba(frame.height, frame.width, CV_8UC4, frame.rgba.data());
        cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
        return true;
    };
    ctx.pixelsPerMmX = cal.scaleX();
    ctx.pixelsPerMmY = cal.scaleY();
    ctx.cameraWidth = cal.width;
    ctx.cameraHeight = cal.height;
    ctx.locationToPixel = [cal, viewX, viewY](double mx, double my, double& px, double& py) {
        return cal.pixelFor(mx, my, viewX, viewY, px, py);
    };
    return true;
}

void JPlacerJobMachine::showWorking(JPPipeline& p, const JPCameraFeed* feed, const std::string& text, int ms) {
    cv::Mat rgba;
    std::string ignored;
    if (!JPStageUtil::toRgba(p.workingImage(), p.workingColorSpace(), true, rgba, ignored)) return;
    JPFrame shown;
    shown.width = rgba.cols;
    shown.height = rgba.rows;
    shown.rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
    m_onMain([&] {
        if (JPCameraView* view = m_machine.cameraViewOf(feed)) view->showPicture(shown, text, ms);
    });
}

bool JPlacerJobMachine::seeRects(const JPLocation& at, JPPipeline& p, int showMs, SeenRects& seen, std::string& why) {
    ++m_motions;
    const JPLocation m = at.convertToUnits(JPLengthUnit::Millimeters);
    JPCameraCalibration cal;
    JPCameraFeed* feed = nullptr;
    if (!headCameraPipeline(m.x(), m.y(), p, cal, feed, why)) return false;
    if (!p.process(why)) return false;
    seen = {};
    seen.halfWidthMm = cal.width / 2.0 / cal.scaleX();
    seen.halfHeightMm = cal.height / 2.0 / cal.scaleY();
    const JPPipeline::Result* r = p.result("results");
    if (!r) {
        why = "Stage \"results\" is missing in the pipeline.";
        return false;
    }
    if (const auto* f = r->model.failure()) {
        why = f->message;
        return false;
    }
    std::vector<cv::RotatedRect> rects;
    if (const auto* l = std::get_if<std::vector<cv::RotatedRect>>(&r->model.value)) rects = *l;
    else if (!r->model.empty()) {
        why = "Pipeline stage \"results\" returned a " + r->model.kind() + " but expected a RotatedRect list.";
        return false;
    }
    for (const cv::RotatedRect& rect : rects) {
        // Where it is, and its angle through the calibration (the picture's own sense, as OpenPnP reads it).
        const double a = rect.angle * M_PI / 180;
        double ax = 0, ay = 0, bx = 0, by = 0;
        if (!cal.machinePoint(rect.center.x, rect.center.y, m.x(), m.y(), ax, ay)
            || !cal.machinePoint(rect.center.x + kAngleStepPx * std::cos(a), rect.center.y + kAngleStepPx * std::sin(a), m.x(), m.y(), bx,
                                 by))
            continue;
        seen.rects.push_back({ ax, ay, -std::atan2(by - ay, bx - ax) * 180 / M_PI });
    }
    if (showMs > 0) showWorking(p, feed, "", showMs);
    return true;
}

bool JPlacerJobMachine::seeCircles(const JPLocation& at, JPPipeline& p, SeenCircles& seen, std::string& why) {
    ++m_motions;
    const JPLocation m = at.convertToUnits(JPLengthUnit::Millimeters);
    JPCameraCalibration cal;
    JPCameraFeed* feed = nullptr;
    if (!headCameraPipeline(m.x(), m.y(), p, cal, feed, why)) return false;
    if (!p.process(why)) return false;
    seen = {};
    if (!cal.pixelFor(m.x(), m.y(), m.x(), m.y(), seen.centreX, seen.centreY)) {
        why = "the camera's calibration cannot place its centre in its picture";
        return false;
    }
    seen.pixelsPerMm = (cal.scaleX() + cal.scaleY()) / 2;
    const double vx = m.x(), vy = m.y();
    seen.toMachine = [cal, vx, vy](double px, double py, double& x, double& y) { return cal.machinePoint(px, py, vx, vy, x, y); };
    const JPPipeline::Result* r = p.result("results");
    if (!r) {
        why = "Stage \"results\" is missing in the pipeline.";
        return false;
    }
    if (const auto* f = r->model.failure()) {
        why = f->message;
        return false;
    }
    if (const auto* l = std::get_if<std::vector<JPPipelineModel::Circle>>(&r->model.value))
        for (const JPPipelineModel::Circle& c : *l) seen.circles.push_back({ c.x, c.y, c.diameter });
    else if (!r->model.empty()) {
        why = "Pipeline stage \"results\" returned a " + r->model.kind() + " but expected a Circle list.";
        return false;
    }
    return true;
}

bool JPlacerJobMachine::lookThrough(const JPLocation& at, JPPipeline& p, Sight& sight, std::string& why) {
    ++m_motions;
    const JPLocation m = at.convertToUnits(JPLengthUnit::Millimeters);
    JPCameraCalibration cal;
    JPCameraFeed* feed = nullptr;
    if (!headCameraPipeline(m.x(), m.y(), p, cal, feed, why)) return false;
    if (!p.process(why)) return false;
    sight = {};
    sight.at = JPLocation(JPLengthUnit::Millimeters, m.x(), m.y(), 0, 0);
    sight.mmPerPixelX = 1 / cal.scaleX();
    sight.mmPerPixelY = 1 / cal.scaleY();
    sight.width = cal.width;
    sight.height = cal.height;
    sight.twoHeights = cal.twoHeights();
    const double vx = m.x(), vy = m.y();
    sight.toMachine = [cal, vx, vy](double px, double py) {
        double x = 0, y = 0;
        cal.machinePoint(px, py, vx, vy, x, y);
        return JPLocation(JPLengthUnit::Millimeters, x, y, 0, 0);
    };
    sight.toPixel = [cal, vx, vy](const JPLocation& l, double& px, double& py) {
        const JPLocation mm = l.convertToUnits(JPLengthUnit::Millimeters);
        return cal.pixelFor(mm.x(), mm.y(), vx, vy, px, py);
    };
    return true;
}

bool JPlacerJobMachine::cameraSight(Sight& sight, std::string& why) {
    JPCell* c = cell(why);
    if (!c) return false;
    JPCameraFeed* feed = nullptr;
    m_onMain([&] { feed = m_machine.headCameraFeed(); });
    if (!feed) {
        why = "no camera on the head";
        return false;
    }
    JPCameraCalibration cal;
    if (!JPCameraLook::calibration(*c, *feed, cal, why)) return false;
    sight = {};
    sight.mmPerPixelX = 1 / cal.scaleX();
    sight.mmPerPixelY = 1 / cal.scaleY();
    sight.width = cal.width;
    sight.height = cal.height;
    sight.twoHeights = cal.twoHeights();
    return true;
}

bool JPlacerJobMachine::readQrCodes(const JPLocation& at, std::vector<QrCode>& codes, std::string& why) {
    ++m_motions;
    const JPLocation m = at.convertToUnits(JPLengthUnit::Millimeters);
    JPPipeline capture;
    JPCameraCalibration cal;
    JPCameraFeed* feed = nullptr;
    if (!headCameraPipeline(m.x(), m.y(), capture, cal, feed, why)) return false;
    cv::Mat bgr;
    if (!capture.context().capture("Settle", "", bgr, why)) return false;
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    std::vector<std::string> texts;
    std::vector<cv::Point2f> corners;
    codes.clear();
    if (!cv::QRCodeDetector().detectAndDecodeMulti(gray, texts, corners)) return true;
    for (size_t i = 0; i < texts.size(); ++i) {
        if (texts[i].empty() || corners.size() < 4 * (i + 1)) continue;
        // Its middle: the average of its corners.
        double px = 0, py = 0;
        for (size_t k = 0; k < 4; ++k) {
            px += corners[4 * i + k].x / 4;
            py += corners[4 * i + k].y / 4;
        }
        double x = 0, y = 0;
        if (cal.machinePoint(px, py, m.x(), m.y(), x, y)) codes.push_back({ texts[i], JPLocation(JPLengthUnit::Millimeters, x, y, 0, 0) });
    }
    return true;
}

void JPlacerJobMachine::showOnCamera(const cv::Mat& bgr, int ms) {
    cv::Mat rgba;
    std::string ignored;
    if (!JPStageUtil::toRgba(bgr, "Bgr", true, rgba, ignored)) return;
    JPFrame shown;
    shown.width = rgba.cols;
    shown.height = rgba.rows;
    shown.rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
    m_onMain([&] {
        if (JPCameraView* view = m_machine.headCameraView()) view->showPicture(shown, "", ms);
    });
}

bool JPlacerJobMachine::lookByPipeline(double viewX, double viewY, double x, double y, const FiducialLook& lookAt,
                                       double& foundX, double& foundY, std::string& why) {
    JPPipeline& p = *lookAt.pipeline;
    JPCameraCalibration cal;
    JPCameraFeed* feed = nullptr;
    if (!headCameraPipeline(viewX, viewY, p, cal, feed, why)) return false;
    // As OpenPnP: the fiducial's place told to the stages that look round it.
    p.setProperty("fiducial.center", JPPipelineValue { JPPipelineValue::LocationMm { x, y } });
    p.setProperty("MaskCircle.center", JPPipelineValue { JPPipelineValue::LocationMm { x, y } });
    if (!p.process(why)) return false;
    // The results: key points, the one nearest where it should be.
    const JPPipeline::Result* r = p.result("results");
    if (!r) {
        why = "Stage \"results\" is missing in the pipeline.";
        return false;
    }
    if (const auto* f = r->model.failure()) {
        why = f->message;
        return false;
    }
    const auto* points = std::get_if<std::vector<cv::KeyPoint>>(&r->model.value);
    if (!points) {
        why = "Pipeline stage \"results\" returned a " + r->model.kind() + " but expected a KeyPoint list.";
        return false;
    }
    if (points->empty()) {
        why = lookAt.partId + " no matches found.";
        return false;
    }
    double best = INFINITY;
    for (const cv::KeyPoint& k : *points) {
        double mx = 0, my = 0;
        if (!cal.machinePoint(k.pt.x, k.pt.y, viewX, viewY, mx, my)) continue;
        if (const double d = std::hypot(mx - x, my - y); d < best) {
            best = d;
            foundX = mx;
            foundY = my;
        }
    }
    if (!std::isfinite(best)) {
        why = "the camera's calibration cannot place the match on the machine";
        return false;
    }
    // What it saw, on the camera's view.
    char text[64];
    std::snprintf(text, sizeof text, "%.3f, %.3f mm", foundX, foundY);
    showWorking(p, feed, text, kShownPipelineMs);
    JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << lookAt.partId << " located at " << foundX << ", " << foundY;
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
    // The nozzle turned so the part is at the look's angle (its Rotation Mode offset taken off).
    double nx = camX, ny = camY, nr = rq.imageAngle - rq.partOffset;
    // By its pipeline: given the camera looking up, prepared for the part (as OpenPnP's preparePipeline).
    std::optional<JPNozzleTipConfig> tip;
    std::shared_ptr<JPVisionComposite> composite;
    if (rq.pipeline) {
        JPPipeline::Context& ctx = rq.pipeline->context();
        ctx.capture = [feed](const std::string& settle, const std::string&, cv::Mat& bgr, std::string& w) {
            JPGrayImage settled;
            if (settle != "Skip" && !JPCameraLook::settled(*feed, settled, w)) return false;
            JPFrame frame;
            if (!feed->latest(frame, 0) || frame.width <= 0) {
                w = feed->config().name + " gives no picture";
                return false;
            }
            cv::Mat rgba(frame.height, frame.width, CV_8UC4, frame.rgba.data());
            cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
            return true;
        };
        ctx.pixelsPerMmX = cal.scaleX();
        ctx.pixelsPerMmY = cal.scaleY();
        ctx.cameraWidth = cal.width;
        ctx.cameraHeight = cal.height;
        ctx.locationToPixel = [cal, camX, camY](double mx, double my, double& px, double& py) {
            return cal.pixelFor(mx, my, camX, camY, px, py);
        };
        bool prepared = false;
        m_onMain([&] {
            const JPVisionSettings* v = m_config.visionSettings(rq.settingsId);
            JPCellConfig cellConfig;
            if (const JPCell* cc = m_machine.cell()) cellConfig = cc->config();
            for (const JPNozzleTipConfig& t : cellConfig.nozzleTips)
                if (t.id == nozzle.tipId) tip = t;
            prepared = v && JPVisionPipelinePrep::bottom(*rq.pipeline, m_config, *v, rq.partId, "", rq.imageAngle, tip ? &*tip : nullptr,
                                                         &feed->config(), why, &composite);
            if (!v) why = "no bottom vision settings " + rq.settingsId;
        });
        if (!prepared) return false;
    }
    for (int pass = 0; pass < std::max(1, rq.passes); ++pass) {
        // A part seen in several shots (OpenPnP's vision compositing).
        if (composite && JPVisionComposite::isAdvanced(composite->solution())) {
            const double angle = nr + (pass == 0 ? rq.partOffset : result.partAngle - result.nozzleAngle);
            double px = 0, py = 0, found = 0;
            if (!alignComposite(*c, nozzle.mount, *rq.pipeline, *composite, tip ? &*tip : nullptr, feed->config().roamingRadiusMm,
                                cal, camX, camY, camZ + rq.partHeightMm, nx, ny, nr, angle, rq.partId, px, py, found, why))
                return false;
            result.nozzleAngle = nr;
            result.cameraX = camX;
            result.cameraY = camY;
            result.dx = px - nx;
            result.dy = py - ny;
            result.partAngle = found;
            JLOGC(JPlacerLog::kJob, JLogLevel::Info) << "aligned on " << nozzle.name << " in " << composite->shots().size()
                                                     << " shots (" << JPVisionComposite::solutionName(composite->solution())
                                                     << "): " << result.dx << ", " << result.dy << " mm, " << (found - nr) << " deg";
            const double off = std::hypot(px - camX, py - camY), turn = std::abs(found - rq.imageAngle);
            if (pass + 1 >= rq.passes || (off < rq.maxLinearOffsetMm && turn < 0.1)) break;
            nx = camX - result.dx;
            ny = camY - result.dy;
            nr = nr - (found - rq.imageAngle);
            continue;
        }
        if (!c->moveToolAndWait(nozzle.mount, { nx, ny, camZ + rq.partHeightMm, nr }, 1.0, why)) return false;
        JPGrayImage img;
        if (!JPCameraLook::settled(*feed, img, why)) return false;
        JPPartFinder::Request fr;
        if (!cal.pixelFor(nx, ny, camX, camY, fr.expectedX, fr.expectedY)) {
            why = "the camera's calibration cannot place the nozzle in its picture";
            return false;
        }
        // The part's angle as it sits: the nozzle's turn, less what is already known to be off.
        fr.angle = nr + (pass == 0 ? rq.partOffset : result.partAngle - result.nozzleAngle);
        fr.angleRange = pass == 0 ? rq.angleRange : std::min(rq.angleRange, 3.0);
        fr.toMachine = [&cal, camX, camY](double px, double py, double& mx, double& my) {
            return cal.machinePoint(px, py, camX, camY, mx, my);
        };
        JPPartFinder::Result found;
        if (rq.pipeline) {
            if (!findByPipeline(*rq.pipeline, rq.partId, cal, camX, camY, fr.expectedX, fr.expectedY, fr.angle,
                                rq.angleRange >= 180 ? 180 : kAdjustRange, found.x, found.y, found.angle, why))
                return false;
            found.found = true;
            found.score = 1;
        } else {
            found = JPPartFinder::find(img, rq.shape, fr);
            if (!found.found) {
                why = "the part was not found: " + found.why;
                return false;
            }
        }
        double px = 0, py = 0;
        if (!cal.machinePoint(found.x, found.y, camX, camY, px, py)) {
            why = "the camera's calibration cannot place the part";
            return false;
        }
        result.nozzleAngle = nr;
        result.cameraX = camX;
        result.cameraY = camY;
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

bool JPlacerJobMachine::findByPipeline(JPPipeline& p, const std::string& partId, const JPCameraCalibration& cal, double camX,
                                       double camY, double expectedX, double expectedY, double angle, double range, double& x,
                                       double& y, double& foundAngle, std::string& why) {
    // Where it should be in the picture, and turned how (OpenPnP's wanted location).
    const JPPipelineValue centre { JPPipelineValue::Pixel { expectedX, expectedY } };
    p.setProperty("MinAreaRect.center", centre);
    p.setProperty("DetectRectlinearSymmetry.center", centre);
    p.setProperty("MinAreaRect.expectedAngle", JPPipelineValue { angle });
    p.setProperty("DetectRectlinearSymmetry.expectedAngle", JPPipelineValue { angle });
    cv::RotatedRect rect;
    if (!pipelineRect(p, partId, rect, why)) return false;
    x = rect.center.x;
    y = rect.center.y;
    // Its angle on the machine: a step along its angle in the picture, through
    // the camera's calibration (which knows how the camera looking up is turned and mirrored).
    const double a = rect.angle * M_PI / 180;
    double ax = 0, ay = 0, bx = 0, by = 0;
    if (!cal.machinePoint(x, y, camX, camY, ax, ay)
        || !cal.machinePoint(x + kAngleStepPx * std::cos(a), y + kAngleStepPx * std::sin(a), camX, camY, bx, by)) {
        why = "the camera's calibration cannot place the part";
        return false;
    }
    // The rectangle knows no side from another: the turn taken nearest the one wanted.
    const double seen = std::atan2(by - ay, bx - ax) * 180 / M_PI;
    foundAngle = angle + JPStageUtil::angleNorm(seen - angle, range);
    return true;
}

bool JPlacerJobMachine::alignComposite(JPCell& cell, const JPMountConfig& nozzle, JPPipeline& pipeline, JPVisionComposite& composite,
                                       const JPNozzleTipConfig* tip, double roamingRadiusMm, const JPCameraCalibration& cal,
                                       double camX, double camY, double z, double nx, double ny, double nr, double angle,
                                       const std::string& partId, double& px, double& py, double& foundAngle, std::string& why) {
    const double c = std::cos(angle * M_PI / 180), s = std::sin(angle * M_PI / 180);
    auto turned = [c, s](double x, double y) { return std::pair { c * x - s * y, s * x + c * y }; };
    // Where the nozzle is, as the part's frame sees it (from the part's centre over the camera).
    const auto at = cell.jogBase();
    auto where = [&at](const std::string& axis, double offset) {
        const auto p = at.find(axis);
        return p == at.end() ? 0.0 : p->second + offset;
    };
    const double hereX = where(nozzle.axisX, nozzle.offsetX), hereY = where(nozzle.axisY, nozzle.offsetY);
    const double fromX = c * (hereX - nx) + s * (hereY - ny), fromY = -s * (hereX - nx) + c * (hereY - ny);
    const JPNozzleTipConfig defaults;
    const double tipTolerance = (tip ? *tip : defaults).maxPickToleranceMm;
    composite.restart();
    for (const JPVisionComposite::Shot* shot : composite.travel(fromX, fromY)) {
        // The nozzle so the shot's middle is over the camera: at camera Z within the roaming radius, else by way of safe Z.
        const auto [sx, sy] = turned(shot->x, shot->y);
        const auto nowAt = cell.jogBase();
        auto now = [&nowAt](const std::string& axis, double offset) {
            const auto p = nowAt.find(axis);
            return p == nowAt.end() ? 0.0 : p->second + offset;
        };
        const double off = std::hypot(now(nozzle.axisX, nozzle.offsetX) - camX, now(nozzle.axisY, nozzle.offsetY) - camY);
        const std::array<std::optional<double>, 4> to { nx - sx, ny - sy, z, nr };
        const bool moved = off > roamingRadiusMm + tipTolerance ? cell.moveToolAndWait(nozzle, to, 1.0, why)
                                                                  : cell.moveToolStraightAndWait(nozzle, to, 1.0, why);
        if (!moved) return false;
        // Where the part's centre is in the picture: the shot's middle away from the camera's centre.
        double partX = 0, partY = 0;
        if (!cal.pixelFor(camX - sx, camY - sy, camX, camY, partX, partY)) {
            why = "the camera's calibration cannot place the part";
            return false;
        }
        JPVisionPipelinePrep::shot(pipeline, composite, *shot, tip, partX, partY);
        cv::RotatedRect rect;
        if (!pipelineRect(pipeline, partId, rect, why)) return false;
        // Its corners on the machine, from where the part's centre should be; each told apart
        // (left or right, upper or lower) in the part's own frame.
        cv::Point2f corners[4];
        rect.points(corners);
        std::array<JPVisionComposite::Point, 4> rel {};
        double mx = 0, my = 0;
        for (int i = 0; i < 4; ++i) {
            double x = 0, y = 0;
            if (!cal.machinePoint(corners[i].x, corners[i].y, camX, camY, x, y)) {
                why = "the camera's calibration cannot place the part";
                return false;
            }
            rel[size_t(i)] = { x - (camX - sx), y - (camY - sy) };
            mx += rel[size_t(i)].x / 4;
            my += rel[size_t(i)].y / 4;
        }
        std::array<JPVisionComposite::Point, 4> points {};
        std::array<bool, 4> taken {};
        for (const auto& p : rel) {
            const double qx = c * (p.x - mx) + s * (p.y - my), qy = -s * (p.x - mx) + c * (p.y - my);
            const size_t idx = size_t((qx < 0 ? 0 : 1) + (qy > 0 ? 0 : 2));
            if (taken[idx]) {
                why = "ReferenceBottomVision (" + partId + "): the shot's rectangle is turned too far to tell its corners apart";
                return false;
            }
            taken[idx] = true;
            points[idx] = p;
        }
        composite.accumulate(*shot, points);
    }
    JPVisionComposite::Detected d;
    if (!composite.interpret(angle, d, why)) {
        why += " for part " + partId;
        return false;
    }
    // The part's centre with the nozzle where it is meant to be.
    px = camX + d.center.x;
    py = camY + d.center.y;
    foundAngle = d.angle;
    return true;
}

bool JPlacerJobMachine::pipelineRect(JPPipeline& p, const std::string& partId, cv::RotatedRect& rect, std::string& why) {
    if (!p.process(why)) return false;
    // Its results ("result" in older pipelines): one rectangle.
    const JPPipeline::Result* r = p.result("results");
    if (!r) r = p.result("result");
    char buf[200];
    if (!r) {
        std::snprintf(buf, sizeof buf, "ReferenceBottomVision (%s): Pipeline error. Pipeline must contain a result named '%s'.",
                      partId.c_str(), "results");
        why = buf;
        return false;
    }
    if (const auto* f = r->model.failure()) {
        why = f->message;
        return false;
    }
    if (r->model.empty()) {
        std::snprintf(buf, sizeof buf, "ReferenceBottomVision (%s): No result found.", partId.c_str());
        why = buf;
        return false;
    }
    const auto* found = std::get_if<cv::RotatedRect>(&r->model.value);
    if (!found) {
        std::snprintf(buf, sizeof buf, "ReferenceBottomVision (%s): Incorrect pipeline result type (%s). Expected RotatedRect.",
                      partId.c_str(), r->model.kind().c_str());
        why = buf;
        return false;
    }
    rect = *found;
    // What it saw, on the camera's view.
    cv::Mat rgba;
    std::string ignored;
    if (JPStageUtil::toRgba(p.workingImage(), p.workingColorSpace(), true, rgba, ignored)) {
        JPFrame shown;
        shown.width = rgba.cols;
        shown.height = rgba.rows;
        shown.rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
        m_onMain([&] {
            if (JPCameraView* view = m_machine.cameraViewOf(m_machine.upCameraFeed())) view->showPicture(shown, partId, kShownPipelineMs);
        });
    }
    return true;
}

} // inline namespace jf
