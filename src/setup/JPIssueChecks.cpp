// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPIssueChecks.h"

inline namespace jf {

namespace {

using Milestone = JPSolutions::Milestone;
using Severity = JPSolutions::Severity;
using State = JPSolutions::State;
using Issue = JPSolutions::Issue;

constexpr const char* kWiki = "https://github.com/openpnp/openpnp/wiki/";

// What OpenPnP's PlainIssue is: only to be dismissed (or looked up).
Issue plain(std::string subject, std::string issue, std::string solution, Severity severity, std::string uri) {
    Issue i;
    i.subject = std::move(subject);
    i.issue = std::move(issue);
    i.solution = std::move(solution);
    i.severity = severity;
    i.uri = std::move(uri);
    i.canBeAccepted = false;
    return i;
}

void setupProblems(JPSolutions& s, const JPIssueChecks::Context& c) {
    const JPCellConfig* cell = c.cell ? c.cell() : nullptr;
    if (!cell) return;
    for (const std::string& p : cell->problems()) {
        Issue i = plain("Machine Setup", p, "Correct it in Machine Setup.", Severity::Error, "");
        i.activate = [c] {
            if (c.showSetup) c.showSetup("");
        };
        s.add(std::move(i));
    }
}

void connect(JPSolutions& s, const JPIssueChecks::Context& c) {
    const JPCellConfig* cell = c.cell ? c.cell() : nullptr;
    if (!cell || !s.isTargeting(Milestone::Connect)) return;
    for (const JPDriverConfig& d : cell->drivers) {
        if (d.link["type"].str() != "simulated") continue;
        Issue i = plain("Controller " + d.name, "Controller not connected to jplacer",
                        "On the controller's Machine Setup page choose its port, then connect: it is simulated now.",
                        Severity::Fundamental, std::string(kWiki) + "Issues-and-Solutions#connect-milestone");
        const std::string id = d.id;
        i.activate = [c, id] {
            if (c.showSetup) c.showSetup("driver:" + id);
        };
        s.add(std::move(i));
    }
    for (const JPCameraConfig& cam : cell->cameras) {
        if (cam.device["backend"].str() != "simulated") continue;
        Issue i = plain("Camera " + cam.name, "Camera not connected to jplacer",
                        "On the camera's Machine Setup page select the correct Device and Format. An image from the camera "
                        "should appear in the camera's view pane.",
                        Severity::Fundamental, std::string(kWiki) + "OpenPnpCaptureCamera");
        const std::string id = cam.id;
        i.activate = [c, id] {
            if (c.showSetup) c.showSetup("camera:" + id);
        };
        s.add(std::move(i));
    }
}

void vision(JPSolutions& s, const JPIssueChecks::Context& c) {
    const JPCellConfig* cell = c.cell ? c.cell() : nullptr;
    if (!cell || !s.isTargeting(Milestone::Vision)) return;
    for (const JPCameraConfig& cam : cell->cameras) {
        const std::string id = cam.id, name = cam.name;
        auto show = [c, id] {
            if (c.showSetup) c.showSetup("camera:" + id);
        };
        if (cam.settle.method == "FixedTime" || cam.settle.method.empty()) {
            Issue i;
            i.subject = "Camera " + name;
            i.issue = "Use an adaptive camera settling method.";
            i.solution = "Set a suitable camera settling method automatically.";
            i.severity = Severity::Fundamental;
            i.uri = std::string(kWiki) + "Camera-Settling";
            i.activate = show;
            i.extendedDescription =
                "For precision in computer vision it is very important that the camera image has settled down after the "
                "machine has moved the camera or the subject. Some cameras exhibit a slight lag, where the frames might still "
                "show the camera in motion. Motion might also cause vibration that must abate sufficently before computer "
                "vision is performed.\n\nThe simple solution is just to wait for a fixed amount of time. However, this is "
                "wasteful if there was no motion in the first place. Using an adaptive settling method can reduce the wait "
                "time in these cases, i.e. one does not need to set a large worst case settle wait time.\n\nAccept sets the "
                "Euclidean method (the difference of the whole picture from the last); the camera's Settle Test on its "
                "Machine Setup page shows how it settles.";
            const std::string old = cam.settle.method;
            i.apply = [c, id, old](State to, std::string&) {
                if (c.changeCell)
                    c.changeCell("Camera settling", [id, old, to](JPCellConfig& cell) {
                        for (JPCameraConfig& x : cell.cameras)
                            if (x.id == id) x.settle.method = to == State::Solved ? "Euclidean" : old;
                    });
                return true;
            };
            s.add(std::move(i));
        }
        if (c.calibrated && !c.calibrated(id)) {
            Issue i = plain("Camera " + name, "Camera " + name + " is not calibrated.",
                            "Calibrate it with the Calibrate button on its picture's tab: jplacer then knows its scale, its "
                            "lens and where it is.",
                            Severity::Error, std::string(kWiki) + "Camera-Calibration");
            i.activate = show;
            s.add(std::move(i));
        }
        if (cam.whiteBalance.neutral()) {
            Issue i = plain("Camera " + name, "Calibrate static white balance for camera " + name + ".",
                            "For best results with color-keyed computer vision, it is recommended to use static white balance.",
                            Severity::Suggestion, std::string(kWiki) + "Camera-White-Balance");
            i.activate = show;
            s.add(std::move(i));
        }
    }
}

void calibration(JPSolutions& s, const JPIssueChecks::Context& c) {
    const JPCellConfig* cell = c.cell ? c.cell() : nullptr;
    if (!cell || !s.isTargeting(Milestone::Calibration)) return;
    for (const JPNozzleTipConfig& t : cell->nozzleTips) {
        bool fits = false;
        for (const JPNozzleConfig& n : cell->nozzles) fits = fits || n.fits(t.id);
        if (!fits)
            s.add(plain("ReferenceNozzleTip " + t.name, "Nozzle tip " + t.name + " has no compatible nozzle.",
                        "Go to the nozzle(s) and enable the Compatible switches where appropriate.", Severity::Error,
                        std::string(kWiki) + "Setup-and-Calibration_Nozzle-Setup#nozzle-to-nozzle-tip-compatibility"));
    }
}

void production(JPSolutions& s, const JPIssueChecks::Context& c) {
    if (!c.config) return;
    for (const JPFeeder& f : c.config->feeders()) {
        if (!f.isPhoton() || f.text("hardware-id").empty()) continue;
        const std::string subject = "PhotonFeeder " + f.name();
        if (f.photonSlot && !f.photonSlotLocation)
            s.add(plain(subject, "Feeder slot has no configured location",
                        "Select the feeder in the Feeders tab and make sure the slot has a set location", Severity::Error,
                        std::string(kWiki) + "Photon-Feeder#slots-and-feeder-locations"));
        if (!f.has("offset"))
            s.add(plain(subject, "Feeder has no configured offset",
                        "Select the feeder in the Feeders tab and make sure the feeder has an offset location from the slot",
                        Severity::Error, std::string(kWiki) + "Photon-Feeder#slots-and-feeder-locations"));
    }
}

} // namespace

std::vector<JPSolutions::Check> JPIssueChecks::all(const Context& c) {
    return {
        [c](JPSolutions& s) { setupProblems(s, c); },
        [c](JPSolutions& s) { connect(s, c); },
        [c](JPSolutions& s) { vision(s, c); },
        [c](JPSolutions& s) { calibration(s, c); },
        [c](JPSolutions& s) { production(s, c); },
    };
}

} // inline namespace jf
