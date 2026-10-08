// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerVisionTests.h"

#include "camera/JPCameraFeed.h"
#include "tasks/JPAlignRequests.h"
#include "tasks/JPFiducialLocator.h"
#include "ui/JPCameraView.h"

#include <j/core/Dialog.h>

#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

using OnMain = std::function<void(const std::function<void()>&)>;

// How long the result of a test alignment is shown on the camera (OpenPnP's displayResult).
constexpr int kResultShownMs = 2000;

} // namespace

JPlacerVisionTests::JPlacerVisionTests(JPlacerJob& job, JPlacerMachine& machine, JPlacerJobRun& run)
    : m_job(job), m_machine(machine), m_run(run) {}

JPVisionForms::Tests JPlacerVisionTests::tests() {
    JPVisionForms::Tests t;
    t.angle = [this] { return m_machine.cell() ? m_machine.cell()->config().vision.testAlignmentAngle : 0.0; };
    t.setAngle = [this](double a) {
        m_machine.changeSetup("Bottom Vision: Placement Angle", [a](JPCellConfig& c) { c.vision.testAlignmentAngle = a; });
    };
    t.center = [this] { return m_center; };
    t.setCenter = [this](bool on) { m_center = on; };
    return t;
}

void JPlacerVisionTests::run(const std::string& settingsId, const JPVisionForms::Holder& holder, const std::string& test,
                             std::function<void()> changed) {
    m_changed = std::move(changed);
    if (test == "testFiducial") testFiducial(holder);
    else align(settingsId, holder, test == "detectOffsets");
}

bool JPlacerVisionTests::nozzleWithPart(const std::string& settingsId, const JPVisionForms::Holder& holder, std::string& nozzleId,
                                        std::string& partId, std::string& why) const {
    JPConfiguration& config = m_job.configuration();
    nozzleId = m_machine.chosenNozzleId();
    std::string nozzleName = nozzleId;
    if (const JPCell* c = m_machine.cell())
        for (const JPNozzleConfig& n : c->config().nozzles)
            if (n.id == nozzleId && !n.name.empty()) nozzleName = n.name;
    partId = m_machine.nozzlePart(nozzleId);
    const JPPart* part = config.part(partId);
    if (!part) {
        why = "Nozzle " + nozzleName + " does not have a part loaded";
        return false;
    }
    if (holder.kind == JPVisionForms::Holder::Kind::Part && part->id != holder.id) {
        why = "Wrong part " + part->id + " on Nozzle " + nozzleName;
        return false;
    }
    if (holder.kind == JPVisionForms::Holder::Kind::Package && part->packageId != holder.id) {
        why = "Wrong package " + part->packageId + " on Nozzle " + nozzleName;
        return false;
    }
    const JPVisionSettings* v = config.visionSettings(settingsId);
    const JPVisionConfig vision = m_machine.cell() ? m_machine.cell()->config().vision : JPVisionConfig {};
    if (!vision.bottomVisionEnabled) {
        why = "Bottom Vision for vision settings " + (v ? v->name : settingsId) + " not enabled.";
        return false;
    }
    if (config.inheritedVision(*part, JPVisionSettings::Kind::Bottom, vision.bottomVisionId) != v) {
        why = "Present Bottom Vision Settings are not effective for part " + part->id + " on Nozzle " + nozzleName + ". "
              + "Assign to Package of Part.";
        return false;
    }
    return true;
}

void JPlacerVisionTests::align(const std::string& settingsId, const JPVisionForms::Holder& holder, bool detectOffsets) {
    std::string nozzleId, partId, why;
    if (!nozzleWithPart(settingsId, holder, nozzleId, partId, why)) {
        JDialog::message("Error", why);
        return;
    }
    // Detect Offsets: at 0°, and always centred; Test Alignment as the page says.
    const double angle = detectOffsets ? 0 : tests().angle();
    const bool center = detectOffsets || m_center;
    m_run.machineTask([this, settingsId, nozzleId, partId, angle, center, detectOffsets](JPJobMachine& machine, const OnMain& onMain,
                                                                                         std::string& why) {
        JPJobMachine::AlignRequest rq;
        bool ok = false;
        double heightMm = 0;
        onMain([&] {
            JPConfiguration& config = m_job.configuration();
            const JPPart* part = config.part(partId);
            if (!part) return;
            heightMm = part->height.convertToUnits(JPLengthUnit::Millimeters).value();
            const JPVisionConfig vision = m_machine.cell() ? m_machine.cell()->config().vision : JPVisionConfig {};
            ok = JPAlignRequests::forPart(config, vision, *part, heightMm, angle, rq);
        });
        if (!ok) {
            why = "Bottom vision is not enabled for " + partId + ".";
            return false;
        }
        // Where the chosen nozzle is: before (centred by hand, for Detect Offsets), and after.
        auto nozzleAt = [&]() -> std::optional<JPLocation> {
            JPlacerMachine::Where w;
            onMain([&] { w = m_machine.whereIs(JPSetupForm::Tool::Nozzle); });
            if (!w[0] || !w[1]) return std::nullopt;
            return JPLocation(JPLengthUnit::Millimeters, *w[0], *w[1], 0, 0);
        };
        const std::optional<JPLocation> before = nozzleAt();
        JPJobMachine::AlignResult r;
        if (!machine.alignPart(nozzleId, rq, r, why)) return false;
        // As found: its offsets on the nozzle, and its turn from the angle wanted.
        const double turn = angle - r.partAngle;
        char text[160];
        std::snprintf(text, sizeof text, "%s  |  X:%.3f Y:%.3f C:%.3f \xCE\x94:%.3f", partId.c_str(), r.dx, r.dy, -turn,
                      std::hypot(r.dx, r.dy));
        if (center) {
            // Centred over the camera and turned to the angle.
            const double t = turn * M_PI / 180;
            const double ox = r.dx * std::cos(t) - r.dy * std::sin(t), oy = r.dx * std::sin(t) + r.dy * std::cos(t);
            const JPLocation at(JPLengthUnit::Millimeters, r.cameraX - ox, r.cameraY - oy, 0, r.nozzleAngle + turn);
            if (!machine.positionNozzle(nozzleId, at, why)) return false;
        }
        // What it looks like now, the result over it.
        onMain([&] {
            JPCameraFeed* feed = m_machine.upCameraFeed();
            JPFrame frame;
            if (JPCameraView* view = m_machine.cameraViewOf(feed); view && feed && feed->latest(frame, 0))
                view->showPicture(frame, text, kResultShownMs);
        });
        if (!detectOffsets) return true;
        // The part centred by hand less where bottom vision centres it: the settings' offsets, added to theirs.
        const std::optional<JPLocation> after = nozzleAt();
        if (!before || !after) {
            why = "The nozzle's place is not known.";
            return false;
        }
        onMain([&] {
            JPVisionSettings* v = m_job.configuration().visionSettings(settingsId);
            if (!v) return;
            const JPLocation was = v->locationOf("vision-offset").convertToUnits(JPLengthUnit::Millimeters);
            const JPLocation b = before->convertToUnits(JPLengthUnit::Millimeters), a = after->convertToUnits(JPLengthUnit::Millimeters);
            v->setLocationOf("vision-offset", JPLocation(JPLengthUnit::Millimeters, b.x() - a.x() + was.x(), b.y() - a.y() + was.y(),
                                                         was.z(), was.rotation()));
            if (m_changed) m_changed();
        });
        return true;
    });
}

void JPlacerVisionTests::testFiducial(const JPVisionForms::Holder& holder) {
    const std::string partId = holder.kind == JPVisionForms::Holder::Kind::Part ? holder.id : std::string();
    const std::string packageId = holder.kind == JPVisionForms::Holder::Kind::Package ? holder.id : std::string();
    m_run.machineTask([this, partId, packageId](JPJobMachine& machine, const OnMain& onMain, std::string& why) {
        double diameter = 0;
        JPJobMachine::FiducialLook look;
        std::string settings;
        JPFiducialLocator::PartProblem problem = JPFiducialLocator::PartProblem::None;
        onMain([&] {
            const JPVisionConfig vision = m_machine.cell() ? m_machine.cell()->config().vision : JPVisionConfig {};
            problem = JPFiducialLocator::lookFor(m_job.configuration(), partId, packageId, vision, diameter, look, settings);
        });
        if (problem == JPFiducialLocator::PartProblem::NoSize) {
            why = "Package " + packageId + " does not have a valid footprint. See https://github.com/openpnp/openpnp/wiki/Fiducials.";
            return false;
        }
        if (problem == JPFiducialLocator::PartProblem::Disabled) {
            why = "Fiducial vision settings " + settings + " are disabled.";
            return false;
        }
        // From where the camera is, as OpenPnP's testFiducialLocation.
        const std::optional<JPLocation> at = machine.cameraLocation();
        if (!at) {
            why = "The head camera's place is not known.";
            return false;
        }
        JPLocation found(JPLengthUnit::Millimeters);
        machine.startFiducialCheck();
        return machine.locateFiducial(*at, diameter, look, found, why) && machine.positionCamera(found, why);
    });
}

} // inline namespace jf
