// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCellJobMachine.h"

#include "JPPipelineCamera.h"

#include "JPTipSlotVision.h"

#include <opencv2/objdetect.hpp>

#include "pipeline/JPStageUtil.h"
#include "tasks/JPAutoFocus.h"
#include "tasks/JPVisionPipelinePrep.h"

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

// How long a part's Auto-Tune may take (its automatic settings given their moment, then held), at most (ms).
constexpr int kPartTuneWaitMs = 15000;

// How far from where it should be a fiducial is looked for at first, then
// once centred on (mm).
constexpr double kFirstSearchMm = 4.0;
constexpr double kSearchMm      = 1.0;
// How long a pipeline's picture of a fiducial found is shown on the camera (OpenPnP's).
constexpr int kShownPipelineMs = 1500;
// A part height found by focusing no more than this is the nozzle tip's own (OpenPnP's 0.001 mm).
constexpr double kLeastFocusedHeightMm = 0.001;

using Where = std::array<std::optional<double>, 4>;

Where where(const JPLocation& l) {
    const JPLocation m = l.convertToUnits(JPLengthUnit::Millimeters);
    return { m.x(), m.y(), m.z(), m.rotation() };
}

} // namespace

JPCellJobMachine::JPCellJobMachine(JPJobMachineHost& host, JPConfiguration& config, OnMain onMain,
                                     std::function<bool(const std::string&)> ask, std::function<void(const std::string&)> progress)
    : m_host(host), m_config(config), m_onMain(std::move(onMain)), m_ask(std::move(ask)), m_progress(std::move(progress)) {}

JPCellConfig JPCellJobMachine::config() const {
    JPCellConfig c;
    m_onMain([&] {
        if (const JPCell* cell = m_host.cell()) c = cell->config();
    });
    return c;
}

JPCell* JPCellJobMachine::cell(std::string& why) const {
    JPCell* c = nullptr;
    m_onMain([&] { c = m_host.cell(); });
    if (!c) why = "no machine is open";
    return c;
}

std::string JPCellJobMachine::headId(const JPCellConfig& c) const {
    for (const JPCameraConfig& cam : c.cameras)
        if (!cam.mount.headId.empty()) return cam.mount.headId;
    return c.heads.empty() ? std::string() : c.heads.front().id;
}

std::vector<JPJobMachine::Nozzle> JPCellJobMachine::nozzles() const {
    const JPCellConfig c = config();
    const std::string head = headId(c);
    std::vector<Nozzle> out;
    for (const JPNozzleConfig& n : c.nozzles) {
        if (n.mount.headId != head) continue;
        Nozzle out1 { n.id, n.name.empty() ? n.id : n.name, n.tipId, n.tipIds, n.pickDwellMs, n.placeDwellMs };
        out1.offsetX = n.mount.offsetX;
        out1.offsetY = n.mount.offsetY;
        out1.rotationMode = n.rotationMode;
        out1.alignRotationWithPart = n.alignRotationWithPart;
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

void JPCellJobMachine::setRotationModeOffset(const std::string& nozzleId, std::optional<double> offset) {
    m_onMain([&] {
        if (JPCell* c = m_host.cell()) c->setRotationModeOffset(nozzleId, offset);
    });
}

std::optional<double> JPCellJobMachine::nozzleRotation(const std::string& nozzleId) const {
    const JPCellConfig c = config();
    for (const JPNozzleConfig& n : c.nozzles)
        if (n.id == nozzleId && !n.mount.axisRotation.empty()) {
            std::optional<double> at;
            m_onMain([&] {
                if (const JPCell* cell = m_host.cell()) {
                    const auto p = cell->positions();
                    if (const auto i = p.find(n.mount.axisRotation); i != p.end()) at = i->second;
                }
            });
            return at;
        }
    return std::nullopt;
}

std::vector<std::pair<std::string, std::string>> JPCellJobMachine::tips() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const JPNozzleTipConfig& t : config().nozzleTips) out.emplace_back(t.id, t.name.empty() ? t.id : t.name);
    return out;
}

std::optional<JPLocation> JPCellJobMachine::cameraLocation() const {
    std::optional<JPLocation> l;
    m_onMain([&] { l = m_host.cameraLocation(); });
    return l;
}

std::optional<JPTravel::Cost> JPCellJobMachine::travelCost() const {
    // As OpenPnP's TravelCost with no tool given: the default (first) head's first camera, its raw axes.
    const JPCellConfig c = config();
    const JPCameraConfig* camera = nullptr;
    for (const JPCameraConfig& cam : c.cameras)
        if (!camera && !c.heads.empty() && cam.mount.headId == c.heads.front().id) camera = &cam;
    if (!camera) return std::nullopt;
    auto raw = [&c](const std::string& id) -> const JPAxisConfig* {
        const JPAxisConfig* a = c.axis(id);
        while (a && a->transformed()) a = c.axis(a->inputAxisId);
        return a && a->kind == JPAxisConfig::Kind::Controller ? a : nullptr;
    };
    auto axis = [](const JPAxisConfig* a) -> std::optional<JPTravel::Axis> {
        if (!a || a->feedratePerSecond <= 0 || a->accelerationPerSecond2 <= 0) return std::nullopt;
        return JPTravel::Axis { a->feedratePerSecond, a->accelerationPerSecond2 };
    };
    const auto x = axis(raw(camera->mount.axisX)), y = axis(raw(camera->mount.axisY));
    if (!x || !y) return std::nullopt;
    return JPTravel::Cost { *x, *y, axis(raw(camera->mount.axisZ)) };
}

bool JPCellJobMachine::cameraReaches(const JPLocation& at) const {
    bool reaches = true;
    m_onMain([&] {
        const JPCell* c = m_host.cell();
        const JPCameraFeed* feed = m_host.headCameraFeed();
        if (!c || !feed) return;
        const JPLocation m = at.convertToUnits(JPLengthUnit::Millimeters);
        reaches = c->reaches(feed->config().mount, m.x(), m.y());
    });
    return reaches;
}

JPJobMachine::TipPush JPCellJobMachine::tipPush(const std::string& tipId) const {
    for (const JPNozzleTipConfig& t : config().nozzleTips)
        if (t.id == tipId) return { t.pushAndDragAllowed, t.diameterLowMm };
    return {};
}

std::string JPCellJobMachine::holdingPart(const std::string& nozzleId) const {
    std::string part;
    m_onMain([&] { part = m_host.nozzlePart(nozzleId); });
    return part;
}

std::string JPCellJobMachine::chosenNozzle() const {
    std::string id;
    m_onMain([&] { id = m_host.chosenNozzleId(); });
    return id;
}

bool JPCellJobMachine::safeZ(std::string& why) {
    JPCell* c = cell(why);
    return c && c->safeZAndWait(headId(config()), 1.0, why);
}

bool JPCellJobMachine::changeTip(const std::string& nozzleId, const std::string& tipId, std::string& why) {
    ++m_motions;
    std::string refused;
    m_onMain([&] { refused = m_host.tipChangeRefusal(nozzleId, tipId); });
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
        const JPNozzleTipConfig*   tip;
        bool                       slotOccupied;   // as its slot is then: the tip in it to load, not to unload
    };
    std::vector<Half> halves;
    if (const JPNozzleTipConfig* on = tipOf(nozzle.tipId))
        halves.push_back({ "Unloading " + nameOf(on) + " from " + nozzle.name, "", on->unloadingSteps(), on, false });
    if (const JPNozzleTipConfig* wanted = tipOf(tipId))
        halves.push_back({ "Loading " + nameOf(wanted) + " on " + nozzle.name, tipId, wanted->loadSteps, wanted, true });
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
        for (const Half& h : halves) m_onMain([&] { m_host.setTipOn(nozzle.id, h.after); });
        return true;
    }
    JPTipChanger::Hooks hooks;
    hooks.ask = m_ask;
    hooks.progress = m_progress;
    std::string cellPath;
    m_onMain([&] { cellPath = m_host.cellPath(); });
    for (Half& h : halves) {
        // OpenPnP's changer slot vision calibration: every place moved by how far off the slot was found.
        std::array<double, 2> offset {};
        std::optional<double> score;
        if (!JPTipSlotVision(*this, *c, cellPath).calibrate(*h.tip, true, h.slotOccupied, offset, score, why)) {
            why = h.what + ": " + why;
            return false;
        }
        if (score) m_onMain([&] { m_host.slotScored(h.tip->id, *score); });
        for (JPChangerStep& s : h.steps) {
            if (s.kind != JPChangerStep::Kind::Move) continue;
            if (s.x) *s.x += offset[0];
            if (s.y) *s.y += offset[1];
        }
        if (!JPTipChanger::run(*c, names, nozzle, h.steps, h.what, false, hooks, why)) {
            why = h.what + ": " + why + ". Look at " + nozzle.name +
                  " and say which tip is on it (the Jog panel's tip menu, Manual Change): that moves nothing.";
            return false;
        }
        m_onMain([&] { m_host.setTipOn(nozzle.id, h.after); });
    }
    return true;
}

bool JPCellJobMachine::rotate(const std::string& nozzleId, double angle, std::string& why) {
    JPCell* c = cell(why);
    if (!c) return false;
    c->rotateWithNextMove(nozzleId, angle);
    return true;
}

bool JPCellJobMachine::pick(const std::string& nozzleId, const JPLocation& at, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    return c && c->pickAtAndWait(nozzleId, where(at), 1.0, why);
}

bool JPCellJobMachine::place(const std::string& nozzleId, const JPLocation& at, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    return c && c->placeAtAndWait(nozzleId, where(at), 1.0, why);
}

bool JPCellJobMachine::vacuumChecked(const std::string& nozzleId, VacuumStep step) const {
    std::string why;
    const JPCell* c = cell(why);
    if (!c) return false;
    using S = JPCell::VacuumStep;
    switch (step) {
        case VacuumStep::AfterPick:   return c->vacuumChecked(nozzleId, S::AfterPick);
        case VacuumStep::Align:       return c->vacuumChecked(nozzleId, S::Align);
        case VacuumStep::BeforePlace: return c->vacuumChecked(nozzleId, S::BeforePlace);
        case VacuumStep::AfterPlace:  return c->vacuumChecked(nozzleId, S::AfterPlace);
        case VacuumStep::BeforePick:  return c->vacuumChecked(nozzleId, S::BeforePick);
    }
    return false;
}

bool JPCellJobMachine::partOn(const std::string& nozzleId, bool& on, std::string& why) {
    JPCell* c = cell(why);
    return c && c->partOnAndWait(nozzleId, on, why);
}

bool JPCellJobMachine::partOff(const std::string& nozzleId, bool& off, std::string& why) {
    ++m_motions;   // the valve pulsed
    JPCell* c = cell(why);
    return c && c->partOffAndWait(nozzleId, off, why);
}

bool JPCellJobMachine::tipCalibrated(const std::string& nozzleId) const {
    const JPCellConfig c = config();
    for (const JPNozzleConfig& n : c.nozzles)
        if (n.id == nozzleId)
            for (const JPNozzleTipConfig& t : c.nozzleTips)
                if (t.id == n.tipId) return !t.runoutCalibration.enabled || t.runout.count(nozzleId) > 0;
    return true;
}

bool JPCellJobMachine::calibrateTip(const std::string& nozzleId, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    if (!c) return false;
    JPCameraFeed* feed = nullptr;
    std::optional<JPNozzleConfig> nozzle;
    std::optional<JPNozzleTipConfig> tip;
    m_onMain([&] {
        feed = m_host.upCameraFeed();
        for (const JPNozzleConfig& n : c->config().nozzles)
            if (n.id == nozzleId) nozzle = n;
        if (nozzle)
            for (const JPNozzleTipConfig& t : c->config().nozzleTips)
                if (t.id == nozzle->tipId) tip = t;
    });
    if (!feed) {
        why = "no camera looking up to calibrate the nozzle tip with";
        return false;
    }
    if (!nozzle || !tip) {
        why = "no tip on nozzle " + nozzleId;
        return false;
    }
    prepare(*c, *feed);
    std::optional<JPBackgroundCalibration::Result> background;
    const auto r = m_host.measureRunout(*c, *feed, *nozzle, *tip, why, background);
    if (!r) return false;
    m_onMain([&] { m_host.keepRunout(tip->id, nozzleId, *r, background); });
    return true;
}

bool JPCellJobMachine::contactProbe(const std::string& nozzleId, bool forward, double depthMm, double& probedZ, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    return c && c->contactProbeAndWait(nozzleId, forward, depthMm, probedZ, why);
}

std::optional<double> JPCellJobMachine::probedOffset(const std::string& nozzleId, bool feeder, const std::string& key) const {
    std::optional<double> out;
    m_onMain([&] {
        if (const JPCell* c = m_host.cell()) out = c->probedOffset(nozzleId, feeder, key);
    });
    return out;
}

void JPCellJobMachine::setProbedOffset(const std::string& nozzleId, bool feeder, const std::string& key, double offsetMm) {
    m_onMain([&] {
        if (JPCell* c = m_host.cell()) c->setProbedOffset(nozzleId, feeder, key, offsetMm);
    });
}

void JPCellJobMachine::holding(const std::string& nozzleId, const std::string& partId) {
    m_onMain([&] { m_host.setNozzlePart(nozzleId, partId); });
}

bool JPCellJobMachine::discard(const std::string& nozzleId, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    return c && c->discardAndWait(nozzleId, 1.0, why);
}

bool JPCellJobMachine::positionCamera(const JPLocation& at, std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    if (!c) return false;
    JPCameraFeed* feed = nullptr;
    m_onMain([&] { feed = m_host.headCameraFeed(); });
    if (!feed) {
        why = "no camera on the head";
        return false;
    }
    const JPLocation m = at.convertToUnits(JPLengthUnit::Millimeters);
    return c->moveToolAndWait(feed->config().mount, { m.x(), m.y(), std::nullopt, std::nullopt }, 1.0, why);
}

bool JPCellJobMachine::moveNozzle(const std::string& nozzleId, std::array<std::optional<double>, 4> to, double speed,
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

bool JPCellJobMachine::vacuumOn(const std::string& nozzleId, std::string& why) {
    JPCell* c = cell(why);
    return c && c->vacuumOnAndWait(nozzleId, why);
}

bool JPCellJobMachine::pickHere(const std::string& nozzleId, std::string& why) {
    JPCell* c = cell(why);
    return c && c->pickAndWait(nozzleId, why);
}

bool JPCellJobMachine::readVacuum(const std::string& nozzleId, double& level, std::string& why) {
    JPCell* c = cell(why);
    return c && c->readVacuumAndWait(nozzleId, level, why);
}

bool JPCellJobMachine::positionNozzle(const std::string& nozzleId, const JPLocation& at, std::string& why) {
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

bool JPCellJobMachine::actuate(const std::string& actuatorName, double value, std::string& why) {
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

bool JPCellJobMachine::isHomed() const {
    bool homed = false;
    m_onMain([&] { homed = m_host.cell() && m_host.cell()->isHomed(); });
    return homed;
}

bool JPCellJobMachine::actuateText(const std::string& actuatorName, const std::string& value, std::string& why) {
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

bool JPCellJobMachine::readActuator(const std::string& actuatorName, const std::string& parameter, std::string& value,
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

bool JPCellJobMachine::moveActuator(const std::string& actuatorName, const JPLocation& at, bool withZ, double speed,
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

bool JPCellJobMachine::positionActuator(const std::string& actuatorName, std::array<std::optional<double>, 4> to, double speed,
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

bool JPCellJobMachine::zeroActuatorRotation(const std::string& actuatorName, std::string& why) {
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

bool JPCellJobMachine::matchTemplate(const JPLocation& at, const std::string& templatePath,
                                      const JPTemplateFinder::Area& area, JPLocation& offset, std::string& why) {
    ++m_motions;
    JPFrame frame;
    if (!JPImageFile::readPng(templatePath, frame, why)) return false;
    const JPGrayImage templ = JPGrayImage::fromRgba(frame.rgba.data(), frame.width, frame.height);
    JPCell* c = cell(why);
    if (!c) return false;
    JPCameraFeed* feed = nullptr;
    m_onMain([&] { feed = m_host.headCameraFeed(); });
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

bool JPCellJobMachine::park(std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    return c && c->parkAndWait(headId(config()), 1.0, why);
}

bool JPCellJobMachine::locateFiducial(const JPLocation& nominal, double diameterMm, const FiducialLook& lookAt,
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
                               : look(vx, vy, x, y, diameterMm, search, fx, fy, why, true);
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

bool JPCellJobMachine::headCameraPipeline(double viewX, double viewY, JPPipeline& p, JPCameraCalibration& cal, JPCameraFeed*& feed,
                                           std::string& why, bool forFiducial, std::optional<double> atZ) {
    JPCell* c = cell(why);
    if (!c) return false;
    feed = nullptr;
    m_onMain([&] { feed = m_host.headCameraFeed(); });
    if (!feed) {
        why = "no camera on the head";
        return false;
    }
    prepare(*c, *feed);
    if (!JPCameraLook::calibration(*c, *feed, cal, why)) return false;
    if (atZ) cal = cal.atHeight(*atZ);   // at another height: its scale there (as it is, calibrated at one)
    // A tune put back before the move, its picture changing while the head travels; one to be made, over what it
    // looks at, after it.
    if (forFiducial && !feed->config().autoTuneFiducials) applyHead(*feed, "", std::nullopt);   // the camera's own
    else if (!forFiducial && !m_headTuneFirst) tuneHead(*feed);
    if (!c->moveToolAndWait(feed->config().mount, { viewX, viewY, std::nullopt, std::nullopt }, 1.0, why)) return false;
    if (!forFiducial && m_headTuneFirst) tuneHead(*feed);
    // Its pictures straightened, as OpenPnP's pipelines get them; `cal` then theirs, to place what is found.
    cal = JPPipelineCamera::give(p.context(), *feed, cal, [viewX, viewY](double& x, double& y) {
        x = viewX;
        y = viewY;
        return true;
    });
    return true;
}

bool JPCellJobMachine::cameraPipeline(const std::string& camera, JPPipeline& p, std::string& why) {
    JPCell* c = cell(why);
    if (!c) return false;
    JPCameraFeed* feed = nullptr;
    m_onMain([&] { feed = m_host.cameraFeed(camera); });
    if (!feed) {
        why = "no camera " + camera;
        return false;
    }
    prepare(*c, *feed);
    JPCameraCalibration cal;
    if (!JPCameraLook::calibration(*c, *feed, cal, why)) return false;
    // Where it looks: a camera on a head where its axes are, a fixed one where it is.
    const JPMountConfig& m = feed->config().mount;
    const auto at = c->positions();
    auto axis = [&at](const std::string& id, double offset) {
        const auto i = at.find(id);
        return (i == at.end() ? 0.0 : i->second) + offset;
    };
    const double vx = m.headId.empty() ? m.offsetX : axis(m.axisX, m.offsetX);
    const double vy = m.headId.empty() ? m.offsetY : axis(m.axisY, m.offsetY);
    JPPipelineCamera::give(p.context(), *feed, cal, [vx, vy](double& x, double& y) {
        x = vx;
        y = vy;
        return true;
    });
    return p.process(why);
}

void JPCellJobMachine::showOn(const std::string& camera, const cv::Mat& bgr, const std::string& text, int ms) {
    cv::Mat rgba;
    cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);
    JPFrame shown;
    shown.width = rgba.cols;
    shown.height = rgba.rows;
    shown.rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
    shown.straightened = true;   // a pipeline's picture (JPPipelineCamera)
    m_onMain([&] {
        m_host.showPicture(m_host.cameraFeed(camera), shown, text, ms);
    });
}

void JPCellJobMachine::showWorking(JPPipeline& p, const JPCameraFeed* feed, const std::string& text, int ms) {
    cv::Mat rgba;
    std::string ignored;
    if (!JPStageUtil::toRgba(p.workingImage(), p.workingColorSpace(), true, rgba, ignored)) return;
    JPFrame shown;
    shown.width = rgba.cols;
    shown.height = rgba.rows;
    shown.rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
    shown.straightened = true;   // a pipeline's picture (JPPipelineCamera)
    m_onMain([&] {
        m_host.showPicture(feed, shown, text, ms);
    });
}

bool JPCellJobMachine::seeRects(const JPLocation& at, JPPipeline& p, int showMs, SeenRects& seen, std::string& why) {
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
            || !cal.machinePoint(rect.center.x + JPBottomVision::kAngleStepPx * std::cos(a),
                                 rect.center.y + JPBottomVision::kAngleStepPx * std::sin(a), m.x(), m.y(), bx, by))
            continue;
        seen.rects.push_back({ ax, ay, -std::atan2(by - ay, bx - ax) * 180 / M_PI });
    }
    if (showMs > 0) showWorking(p, feed, "", showMs);
    return true;
}

bool JPCellJobMachine::seeCircles(const JPLocation& at, JPPipeline& p, SeenCircles& seen, std::string& why) {
    return seeCirclesAt(at, std::nullopt, p, seen, why);
}

bool JPCellJobMachine::seeCirclesAt(const JPLocation& at, std::optional<double> atZ, JPPipeline& p, SeenCircles& seen, std::string& why,
                                    const std::function<void(JPPipeline&)>& configure) {
    ++m_motions;
    const JPLocation m = at.convertToUnits(JPLengthUnit::Millimeters);
    JPCameraCalibration cal;
    JPCameraFeed* feed = nullptr;
    if (!headCameraPipeline(m.x(), m.y(), p, cal, feed, why, false, atZ)) return false;
    if (configure) configure(p);
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

bool JPCellJobMachine::lookThrough(const JPLocation& at, JPPipeline& p, Sight& sight, std::string& why) {
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

bool JPCellJobMachine::cameraSight(Sight& sight, std::string& why) {
    JPCell* c = cell(why);
    if (!c) return false;
    JPCameraFeed* feed = nullptr;
    m_onMain([&] { feed = m_host.headCameraFeed(); });
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

bool JPCellJobMachine::lookAt(double x, double y, double z, cv::Mat& bgr, JPCameraCalibration& cal, std::string& why) {
    useHeadTune("", std::nullopt, false);   // not a feeder's look: the camera's own settings
    ++m_motions;
    JPPipeline capture;
    JPCameraFeed* feed = nullptr;
    if (!headCameraPipeline(x, y, capture, cal, feed, why) || !capture.context().capture("Settle", "", bgr, why)) return false;
    cal = cal.atHeight(z);
    return true;
}

bool JPCellJobMachine::readQrCodes(const JPLocation& at, std::vector<QrCode>& codes, std::string& why) {
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

void JPCellJobMachine::showOnCamera(const cv::Mat& bgr, int ms) {
    cv::Mat rgba;
    std::string ignored;
    if (!JPStageUtil::toRgba(bgr, "Bgr", true, rgba, ignored)) return;
    JPFrame shown;
    shown.width = rgba.cols;
    shown.height = rgba.rows;
    shown.rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
    shown.straightened = true;   // a pipeline's picture (JPPipelineCamera)
    m_onMain([&] {
        m_host.showPicture(m_host.headCameraFeed(), shown, "", ms);
    });
}

bool JPCellJobMachine::lookByPipeline(double viewX, double viewY, double x, double y, const FiducialLook& lookAt,
                                       double& foundX, double& foundY, std::string& why) {
    JPPipeline& p = *lookAt.pipeline;
    JPCameraCalibration cal;
    JPCameraFeed* feed = nullptr;
    if (!headCameraPipeline(viewX, viewY, p, cal, feed, why, true)) return false;
    // The camera's Auto-Tune for fiducial checks?: tuned on a check's first fiducial, that tune for the rest of it.
    if (feed->config().autoTuneFiducials && !tuneForFiducials(*feed, why)) return false;
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

bool JPCellJobMachine::tuneForPart(JPCameraFeed& feed, const std::string& partId, std::string& why) {
    if (const auto kept = m_partTunes.find(partId); kept != m_partTunes.end()) {
        feed.setControls(kept->second);   // put back: the picture then settles as any does
        return true;
    }
    auto told = std::make_shared<std::promise<std::optional<JJson>>>();
    std::future<std::optional<JJson>> tuned = told->get_future();
    feed.autoTune(JPCameraFeed::kAutoTuneMs, [told](std::optional<JJson> t) { told->set_value(std::move(t)); });
    if (tuned.wait_for(std::chrono::milliseconds(kPartTuneWaitMs)) != std::future_status::ready) {
        why = feed.config().name + " was not tuned on " + partId + " within " + std::to_string(kPartTuneWaitMs / 1000) + " s";
        return false;
    }
    const std::optional<JJson> controls = tuned.get();
    if (!controls) {
        why = feed.config().name + " could not be tuned on " + partId + " (see the log)";
        return false;
    }
    m_partTunes[partId] = *controls;
    JLOGC(JPlacerLog::kJob, JLogLevel::Info) << feed.config().name << ": tuned on the first " << partId
                                             << "; kept for every " << partId << " this run";
    return true;
}

bool JPCellJobMachine::tuneForFiducials(JPCameraFeed& feed, std::string& why) {
    if (m_fiducialTune) {
        if (m_headApplied != "fiducials") feed.setControls(*m_fiducialTune);   // put back: the picture then settles as any does
        m_headApplied = "fiducials";
        return true;
    }
    m_headApplied.reset();   // tuning: what it arrives at, or (failing) not known
    auto told = std::make_shared<std::promise<std::optional<JJson>>>();
    std::future<std::optional<JJson>> tuned = told->get_future();
    feed.autoTune(JPCameraFeed::kAutoTuneMs, [told](std::optional<JJson> t) { told->set_value(std::move(t)); });
    if (tuned.wait_for(std::chrono::milliseconds(kPartTuneWaitMs)) != std::future_status::ready) {
        why = feed.config().name + " was not tuned on the first fiducial within " + std::to_string(kPartTuneWaitMs / 1000) + " s";
        return false;
    }
    m_fiducialTune = tuned.get();
    if (!m_fiducialTune) {
        why = feed.config().name + " could not be tuned on the first fiducial (see the log)";
        return false;
    }
    m_headApplied = "fiducials";
    JLOGC(JPlacerLog::kJob, JLogLevel::Info) << feed.config().name << ": tuned on the first fiducial; kept for the rest of the check";
    return true;
}

void JPCellJobMachine::tuneHead(JPCameraFeed& feed) {
    // A feeder's first look this run, its Auto-Tune? on: tuned over it, that tune the feeder's for the run. Not
    // tuned (a camera without settings of its own, say): its looks at the camera's own settings, as said.
    if (m_headTuneFirst) {
        m_headTuneFirst = false;
        auto told = std::make_shared<std::promise<std::optional<JJson>>>();
        std::future<std::optional<JJson>> tuned = told->get_future();
        feed.autoTune(JPCameraFeed::kAutoTuneMs, [told](std::optional<JJson> t) { told->set_value(std::move(t)); });
        std::optional<JJson> made;
        if (tuned.wait_for(std::chrono::milliseconds(kPartTuneWaitMs)) == std::future_status::ready) made = tuned.get();
        if (made) {
            m_headTune = made;
            m_headApplied = m_headKey;
            m_onMain([&] {
                if (JPFeeder* f = m_config.feeder(m_headKey)) f->cameraTune = made;
            });
            JLOGC(JPlacerLog::kJob, JLogLevel::Info) << feed.config().name << ": tuned on feeder " << m_headKey
                                                     << "'s first look; kept for its looks this run";
            return;
        }
        JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << feed.config().name << " could not be tuned on feeder " << m_headKey
                                                 << "'s first look (see the log); its looks at the camera's own settings";
        m_headKey.clear();
        m_headApplied.reset();
    }
    applyHead(feed, m_headKey, m_headTune);
}

void JPCellJobMachine::applyHead(JPCameraFeed& feed, const std::string& key, std::optional<JJson> controls) {
    if (m_headApplied == key) return;
    // None of its own: the camera's own settings, as kept in the cell (its Auto-Tune's, homing's or calibration's).
    if (!controls) {
        const JPCellConfig c = config();
        for (const JPCameraConfig& cam : c.cameras)
            if (cam.id == feed.config().id && cam.device["controls"].isObject()) controls = cam.device["controls"];
    }
    if (controls) feed.setControls(*controls);   // the picture then settles as any does
    m_headApplied = key;
    JLOGC(JPlacerLog::kCamera, JLogLevel::Debug) << feed.config().name << ": at "
                                                 << (key.empty() ? std::string("its own settings") : "feeder " + key + "'s tune");
}

void JPCellJobMachine::prepare(JPCell& cell, JPCameraFeed& feed) {
    const std::string id = feed.config().id;
    m_onMain([&] { m_host.showCamera(id); });
    const std::string light = feed.config().lightActuator();
    std::string why;
    if (!light.empty() && feed.config().light.beforeCapture) cell.switchActuatorAndWait(light, true, why);
}

bool JPCellJobMachine::look(double viewX, double viewY, double x, double y, double diameterMm, double searchMm,
                             double& foundX, double& foundY, std::string& why, bool fiducial) {
    JPCell* c = cell(why);
    if (!c) return false;
    JPCameraFeed* feed = nullptr;
    m_onMain([&] { feed = m_host.headCameraFeed(); });
    if (!feed) {
        why = "no camera on the head";
        return false;
    }
    prepare(*c, *feed);
    JPCameraCalibration cal;
    if (!JPCameraLook::calibration(*c, *feed, cal, why)) return false;
    // A tune put back before the move (the camera's own for a fiducial without its own; a feeder's), its picture
    // changing while the head travels; one to be made, over what it looks at, after it.
    if (fiducial && !feed->config().autoTuneFiducials) applyHead(*feed, "", std::nullopt);
    else if (!fiducial && !m_headTuneFirst) tuneHead(*feed);
    if (!c->moveToolAndWait(feed->config().mount, { viewX, viewY, std::nullopt, std::nullopt }, 1.0, why)) return false;
    // The camera's Auto-Tune for fiducial checks?: tuned on a check's first fiducial, that tune for the rest of it.
    if (fiducial && feed->config().autoTuneFiducials && !tuneForFiducials(*feed, why)) return false;
    if (!fiducial && m_headTuneFirst) tuneHead(*feed);
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

bool JPCellJobMachine::locateHole(const JPLocation& nominal, JPPipeline& pipeline, double searchMm, const std::vector<JPLocation>& from,
                                   const std::function<void(JPPipeline&)>& configure, JPLocation& found, std::string& why) {
    const JPLocation n = nominal.convertToUnits(JPLengthUnit::Millimeters);
    std::vector<JPLocation> views = from;
    if (views.empty()) views.push_back(n);
    double sx = 0, sy = 0;
    for (const JPLocation& v : views) {
        // The round mark nearest the hole's place among those the pipeline finds from there, at the tape's scale.
        SeenCircles seen;
        if (!seeCirclesAt(v.convertToUnits(JPLengthUnit::Millimeters), n.z(), pipeline, seen, why, configure)) return false;
        double best = searchMm, x = 0, y = 0;
        bool any = false;
        for (const SeenCircles::Circle& c : seen.circles) {
            double cx = 0, cy = 0;
            if (!seen.toMachine(c.x, c.y, cx, cy)) continue;
            if (const double d = std::hypot(cx - n.x(), cy - n.y()); d <= best) {
                best = d;
                x = cx;
                y = cy;
                any = true;
            }
        }
        if (!any) {
            why = seen.circles.empty() ? "no round mark" : "no round mark within " + std::to_string(searchMm) + " mm";
            return false;
        }
        sx += x;
        sy += y;
    }
    found = JPLocation(JPLengthUnit::Millimeters, sx / double(views.size()), sy / double(views.size()), n.z(), n.rotation());
    return true;
}

bool JPCellJobMachine::alignPart(const std::string& nozzleId, const AlignRequest& rq, AlignResult& result,
                                  std::string& why) {
    ++m_motions;
    JPCell* c = cell(why);
    if (!c) return false;
    JPCameraFeed* feed = nullptr;
    JPNozzleConfig nozzle;
    m_onMain([&] {
        feed = m_host.upCameraFeed();
        if (const JPCell* cc = m_host.cell())
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
    // The nozzle turned so the part is at the look's angle (the nozzle's
    // rotation is the part's: its rotation mode offset is the cell's), over
    // the camera as it is for this nozzle (OpenPnP's camera.getLocation(nozzle):
    // less its tip's camera offset); what is seen measured from where it is set.
    double cameraDx = 0, cameraDy = 0;
    c->cameraOffsetFor(nozzleId, cameraDx, cameraDy);
    JPCameraCalibration pipelineCal = cal;   // the pipeline's pictures' (straightened), given it below
    double nx = camX - cameraDx, ny = camY - cameraDy, nr = rq.imageAngle;
    // By its pipeline: given the camera looking up, prepared for the part (as OpenPnP's preparePipeline).
    std::optional<JPNozzleTipConfig> tip;
    std::shared_ptr<JPVisionComposite> composite;
    if (rq.pipeline) {
        // Its pictures straightened, as OpenPnP's pipelines get them; what it finds placed through pipelineCal.
        pipelineCal = JPPipelineCamera::give(rq.pipeline->context(), *feed, cal, [camX, camY](double& x, double& y) {
            x = camX;
            y = camY;
            return true;
        });
        bool prepared = false;
        m_onMain([&] {
            const JPVisionSettings* v = m_config.visionSettings(rq.settingsId);
            JPCellConfig cellConfig;
            if (const JPCell* cc = m_host.cell()) cellConfig = cc->config();
            for (const JPNozzleTipConfig& t : cellConfig.nozzleTips)
                if (t.id == nozzle.tipId) tip = t;
            prepared = v && JPVisionPipelinePrep::bottom(*rq.pipeline, m_config, *v, rq.partId, "", rq.imageAngle, tip ? &*tip : nullptr,
                                                         &feed->config(), why, &composite);
            if (!v) why = "no bottom vision settings " + rq.settingsId;
        });
        if (!prepared) return false;
    }
    // A part of unknown height: found by focusing on it (OpenPnP's auto focus), over each shot (the
    // centre, without compositing), as high as the tip's tallest part above the camera down to it.
    double partHeight = rq.partHeightMm;
    if (!tip)
        m_onMain([&] {
            if (const JPCell* cc = m_host.cell())
                for (const JPNozzleTipConfig& t : cc->config().nozzleTips)
                    if (t.id == nozzle.tipId) tip = t;
        });
    if (partHeight <= 0) {
        if (feed->config().focusSensingMethod != "AutoFocus" || !tip) {
            why = "Part height unknown and camera " + feed->config().name + " does not support part height sensing.";
            return false;
        }
        std::vector<std::pair<double, double>> shots;
        if (composite && JPVisionComposite::isAdvanced(composite->solution())) {
            const double a = nr * M_PI / 180;
            for (const JPVisionComposite::Shot* s : composite->travel(0, 0))
                shots.push_back({ std::cos(a) * s->x - std::sin(a) * s->y, std::sin(a) * s->x + std::cos(a) * s->y });
        } else {
            shots.push_back({ 0, 0 });
        }
        double sum = 0;
        for (const auto& [sx, sy] : shots) {
            JPAutoFocus::Request af;
            af.tool = nozzle.mount;
            af.x = nx - sx;
            af.y = ny - sy;
            af.rotation = nr;
            af.z1 = camZ;
            af.z0 = camZ + tip->maxPartHeightMm;
            af.subjectMaxSizeMm = tip->maxPartDiameterMm + 2 * tip->maxPickToleranceMm;
            af.mmPerPixel = cal.scale() > 0 ? 1 / cal.scale() : 0;
            af.settings = feed->config().autoFocus;
            af.machineSpeed = c->speed();
            const auto z = JPAutoFocus::run(*c, *feed, af, [this](const JPFrame& frame, const std::string& text) {
                m_onMain([&] {
                    m_host.showPicture(m_host.upCameraFeed(), frame, text, kShownPipelineMs);
                });
            }, why);
            if (!z) return false;
            sum += *z - camZ;
        }
        partHeight = sum / double(shots.size());
        if (partHeight <= kLeastFocusedHeightMm) {
            why = "Auto focus part height determination failed. Camera seems to have focused on nozzle tip.";
            return false;
        }
        JLOGC(JPlacerLog::kJob, JLogLevel::Info) << "Part " << rq.partId << " height set to " << partHeight << " by camera focus provider.";
        result.measuredPartHeightMm = partHeight;
    }
    // OpenPnP's findOffsets, each look: the nozzle put there, the part found as its settings say.
    JPBottomVision::Settings settings = rq.offsets;
    if (tip) {
        settings.maxPickToleranceMm = tip->maxPickToleranceMm;
        settings.tipName = tip->name;
    }
    const double z = camZ + partHeight;
    const bool composited = composite && JPVisionComposite::isAdvanced(composite->solution());
    // jplacer's own finder matches the footprint's shape: its size is the shape's.
    double shapeX0 = 0, shapeY0 = 0, shapeX1 = 0, shapeY1 = 0;
    for (const JPPartFinder::Rect& r : rq.shape) {
        const double a = r.rotation * M_PI / 180, hw = r.width / 2, hh = r.height / 2;
        const double ex = std::abs(hw * std::cos(a)) + std::abs(hh * std::sin(a)), ey = std::abs(hw * std::sin(a)) + std::abs(hh * std::cos(a));
        shapeX0 = std::min(shapeX0, r.x - ex);
        shapeX1 = std::max(shapeX1, r.x + ex);
        shapeY0 = std::min(shapeY0, r.y - ey);
        shapeY1 = std::max(shapeY1, r.y + ey);
    }
    bool partTuned = false;   // this alignment's part tuned on, or its kept values put back
    const JPBottomVision::Look look = [&](const JPLocation& at, double expected, int pass, JPBottomVision::Seen& seen,
                                          std::string& w) {
        const double nx = at.x(), ny = at.y(), nr = at.rotation();
        // A part seen in several shots (OpenPnP's vision compositing).
        if (composited) {
            if (!alignComposite(*c, nozzle.mount, *rq.pipeline, *composite, tip ? &*tip : nullptr, feed->config().roamingRadiusMm,
                                pipelineCal, camX, camY, z, nx, ny, nr, expected, rq.partId, seen, w))
                return false;
            JLOGC(JPlacerLog::kJob, JLogLevel::Info) << "seen on " << nozzle.name << " in " << composite->shots().size() << " shots ("
                                                     << JPVisionComposite::solutionName(composite->solution()) << "): "
                                                     << seen.x - nx << ", " << seen.y - ny << " mm, " << (seen.angle - nr) << " deg";
            return true;
        }
        if (!c->moveToolAndWait(nozzle.mount, { nx, ny, z, nr }, 1.0, w)) return false;
        // The camera's Auto-Tune for each part?: once, the part over it.
        if (feed->config().autoTuneEachPart && !partTuned && !rq.partId.empty()) {
            if (!tuneForPart(*feed, rq.partId, w)) return false;
            partTuned = true;
        }
        JPGrayImage img;
        if (!JPCameraLook::settled(*feed, img, w)) return false;
        if (rq.pipeline) {
            if (!JPBottomVision::findByPipeline(*rq.pipeline, rq.partId, pipelineCal, camX, camY, nx, ny, expected,
                                                settings.fullRotation ? 180 : JPBottomVision::kAdjustRange, seen, w))
                return false;
            showWorking(*rq.pipeline, feed, rq.partId, kShownPipelineMs);
            return true;
        }
        JPPartFinder::Request fr;
        if (!cal.pixelFor(nx, ny, camX, camY, fr.expectedX, fr.expectedY)) {
            w = "the camera's calibration cannot place the nozzle in its picture";
            return false;
        }
        fr.angle = expected;
        fr.angleRange = pass == 0 ? rq.angleRange : std::min(rq.angleRange, 3.0);
        fr.toMachine = [&cal, camX, camY](double px, double py, double& mx, double& my) {
            return cal.machinePoint(px, py, camX, camY, mx, my);
        };
        const JPPartFinder::Result found = JPPartFinder::find(img, rq.shape, fr);
        if (!found.found) {
            w = "the part was not found: " + found.why;
            return false;
        }
        if (!cal.machinePoint(found.x, found.y, camX, camY, seen.x, seen.y)) {
            w = "the camera's calibration cannot place the part";
            return false;
        }
        seen.angle = found.angle;
        seen.widthMm = shapeX1 - shapeX0;
        seen.heightMm = shapeY1 - shapeY0;
        JLOGC(JPlacerLog::kJob, JLogLevel::Info) << "seen on " << nozzle.name << ": " << seen.x - nx << ", " << seen.y - ny << " mm, "
                                                 << (seen.angle - nr) << " deg (score " << found.score << ")";
        return true;
    };
    JPBottomVision::Offset offset;
    if (!JPBottomVision::findOffsets(settings, camX, camY, look, offset, why)) return false;
    // As jplacer places by it: the nozzle's turn, the part's centre off its axis, the part's angle.
    const JPLocation& o = offset.location;
    result.cameraX = camX;
    result.cameraY = camY;
    result.dx = o.x();
    result.dy = o.y();
    result.nozzleAngle = offset.preRotated ? rq.imageAngle - o.rotation() : 0;
    result.partAngle = offset.preRotated ? rq.imageAngle : o.rotation();
    JLOGC(JPlacerLog::kJob, JLogLevel::Info) << "aligned " << rq.partId << " on " << nozzle.name << (offset.preRotated ? " (pre-rotated)" : "")
                                             << ": offsets " << o.x() << ", " << o.y() << " mm, " << o.rotation() << " deg";
    return true;
}

bool JPCellJobMachine::alignComposite(JPCell& cell, const JPMountConfig& nozzle, JPPipeline& pipeline, JPVisionComposite& composite,
                                       const JPNozzleTipConfig* tip, double roamingRadiusMm, const JPCameraCalibration& cal,
                                       double camX, double camY, double z, double nx, double ny, double nr, double angle,
                                       const std::string& partId, JPBottomVision::Seen& seen, std::string& why) {
    auto nozzleAt = [&cell, &nozzle](double& x, double& y) {
        const auto at = cell.jogBase();
        auto where = [&at](const std::string& axis, double offset) {
            const auto p = at.find(axis);
            return p == at.end() ? 0.0 : p->second + offset;
        };
        x = where(nozzle.axisX, nozzle.offsetX);
        y = where(nozzle.axisY, nozzle.offsetY);
        return true;
    };
    const JPNozzleTipConfig defaults;
    const double tipTolerance = (tip ? *tip : defaults).maxPickToleranceMm;
    // At camera Z within the roaming radius, else by way of safe Z.
    auto moveTo = [&](double x, double y, std::string& w) {
        double hereX = 0, hereY = 0;
        nozzleAt(hereX, hereY);
        const std::array<std::optional<double>, 4> to { x, y, z, nr };
        return std::hypot(hereX - camX, hereY - camY) > roamingRadiusMm + tipTolerance ? cell.moveToolAndWait(nozzle, to, 1.0, w)
                                                                                         : cell.moveToolStraightAndWait(nozzle, to, 1.0, w);
    };
    JPCameraFeed* feed = nullptr;
    m_onMain([&] { feed = m_host.upCameraFeed(); });
    const JPBottomVision::Composite k { pipeline, composite, tip, cal, camX, camY, partId, nozzleAt, moveTo,
                                        [this, feed, &partId](JPPipeline& p) { showWorking(p, feed, partId, kShownPipelineMs); } };
    return JPBottomVision::seeComposite(k, nx, ny, angle, seen, why);
}

} // inline namespace jf
