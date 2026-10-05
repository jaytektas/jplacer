// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPIssueChecks.h"

inline namespace jf {

namespace {

using Milestone = JPSolutions::Milestone;
using Severity = JPSolutions::Severity;
using State = JPSolutions::State;
using Issue = JPSolutions::Issue;
using S = JPSolutions;

constexpr const char* kWiki = "https://github.com/openpnp/openpnp/wiki/";
// OpenPnP's: a preview faster than this is suggested down to the other.
constexpr double kMostPreviewFps = 15;
constexpr double kSuggestedPreviewFps = 5;

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

// A Machine Setup change made by an issue: `edit` as it is solved, `undo` as it is not.
std::function<bool(State, std::string&)> changing(const JPIssueChecks::Context& c, std::string what,
                                                  std::function<void(JPCellConfig&, bool solved)> edit) {
    return [c, what = std::move(what), edit = std::move(edit)](State to, std::string&) {
        if (c.changeCell) c.changeCell(what, [&edit, to](JPCellConfig& cell) { edit(cell, to == State::Solved); });
        return true;
    };
}

JPAxisConfig* axisIn(JPCellConfig& cell, const std::string& id) {
    for (JPAxisConfig& a : cell.axes)
        if (a.id == id) return &a;
    return nullptr;
}

void welcome(JPSolutions& s, const JPIssueChecks::Context& c) {
    const JPCellConfig* cell = c.cell ? c.cell() : nullptr;
    if (!cell) return;
    for (const JPHeadConfig& h : cell->heads) {
        bool any = false;
        for (const JPNozzleConfig& n : cell->nozzles) any = any || n.mount.headId == h.id;
        if (any) continue;
        Issue i = plain("ReferenceHead " + h.name, "Create nozzles for this head.",
                        "Choose the number and type of your nozzles: add them in Machine Setup.", Severity::Fundamental,
                        std::string(kWiki) + "Issues-and-Solutions#welcome-milestone");
        const std::string id = h.id;
        i.activate = [c, id] {
            if (c.showSetup) c.showSetup("head:" + id);
        };
        s.add(std::move(i));
    }
}

void basics(JPSolutions& s, const JPIssueChecks::Context& c) {
    const JPCellConfig* cell = c.cell ? c.cell() : nullptr;
    if (!cell || !s.isTargeting(Milestone::Basics)) return;
    const std::string axesWiki = std::string(kWiki) + "Machine-Axes#controller-settings";
    for (const JPAxisConfig& a : cell->axes) {
        if (a.kind != JPAxisConfig::Kind::Controller) continue;
        const std::string subject = "ReferenceControllerAxis " + a.name, id = a.id;
        auto show = [c, id] {
            if (c.showSetup) c.showSetup("axis:" + id);
        };
        if (a.driverId.empty()) {
            Issue i = plain(subject, "Axis is not assigned to a driver.", "Assign a driver.", Severity::Fundamental, axesWiki);
            i.activate = show;
            s.add(std::move(i));
            continue;
        }
        // The letter, set right here (OpenPnP's AxisLetterIssue).
        auto letterIssue = [&](std::string issue, std::string solution, Severity severity) {
            Issue i;
            i.subject = subject;
            i.issue = std::move(issue);
            i.solution = std::move(solution);
            i.severity = severity;
            i.uri = axesWiki;
            i.activate = show;
            S::Property letter;
            letter.kind = S::Property::Kind::Text;
            letter.label = "Axis Letter";
            letter.tooltip = "The axis letter as the controller knows it.";
            letter.getText = [c, id] {
                const JPCellConfig* now = c.cell ? c.cell() : nullptr;
                if (now)
                    for (const JPAxisConfig& x : now->axes)
                        if (x.id == id) return x.letter;
                return std::string();
            };
            letter.setText = [c, id](const std::string& v) {
                if (c.changeCell)
                    c.changeCell("Axis letter", [id, v](JPCellConfig& cell) {
                        if (JPAxisConfig* x = axisIn(cell, id)) x->letter = v;
                    });
            };
            i.properties.push_back(std::move(letter));
            s.add(std::move(i));
        };
        if (a.letter.empty()) {
            letterIssue("Axis letter is missing. Assign the letter to continue.",
                        "Assign the axis letter (below), then press Accept.", Severity::Fundamental);
            continue;
        }
        if (a.letter == "E")
            letterIssue("Avoid axis letter E, if possible. Use proper rotation axes instead.",
                        "Assign a proper rotation axis letter (below), then press Accept.", Severity::Warning);
        std::vector<std::string> duplicates;
        for (const JPAxisConfig& b : cell->axes)
            if (b.kind == JPAxisConfig::Kind::Controller && b.driverId == a.driverId && b.letter == a.letter)
                duplicates.push_back(b.name);
        if (duplicates.size() > 1) {
            std::string names;
            for (const std::string& n : duplicates) names += (names.empty() ? "" : ", ") + n;
            letterIssue("Duplicate axis letter " + a.letter + " on axes " + names + ".",
                        "Assign the unique axis letter where wrong, press Accept, then press Find Issues & Solutions again to "
                        "clear the correct one.",
                        Severity::Error);
        }
    }
    // Each nozzle its own Z and a rotation axis.
    const std::string nozzleWiki = std::string(kWiki) + "Mapping-Axes";
    for (const JPNozzleConfig& n : cell->nozzles) {
        const std::string subject = "ReferenceNozzle " + n.name;
        if (n.mount.axisZ.empty())
            s.add(plain(subject, "Nozzle " + n.name + " does not have a Z axis assigned.",
                        "Please assign a proper Z axis. You might need to create one first.", Severity::Error, nozzleWiki));
        if (n.mount.axisRotation.empty())
            s.add(plain(subject, "Nozzle " + n.name + " does not have a Rotation axis assigned.",
                        "Please assign a proper Rotation axis. You might need to create one first.", Severity::Error,
                        nozzleWiki));
        for (const JPNozzleConfig& n2 : cell->nozzles) {
            if (&n2 == &n) break;   // each pair once
            if (n2.mount.headId != n.mount.headId) continue;
            const JPAxisConfig* z1 = nullptr;
            const JPAxisConfig* z2 = nullptr;
            for (const JPAxisConfig& a : cell->axes) {
                if (a.id == n.mount.axisZ) z1 = &a;
                if (a.id == n2.mount.axisZ) z2 = &a;
            }
            // A shared Z is fine through a negating or cam axis of its own.
            if (!n.mount.axisZ.empty() && n.mount.axisZ == n2.mount.axisZ && z1 && z2)
                s.add(plain(subject, "Nozzles " + n2.name + " and " + n.name + " have the same Z axis assigned.",
                            "Please assign a different Z axis.", Severity::Error, nozzleWiki));
            if (!n.mount.axisRotation.empty() && n.mount.axisRotation == n2.mount.axisRotation)
                s.add(plain(subject, "Nozzles " + n2.name + " and " + n.name + " have the same Rotation axis assigned.",
                            "It is OK to share rotation axes. If intentional, just dismiss this issue. Otherwise assign a "
                            "different Rotation axis.",
                            Severity::Information, nozzleWiki));
        }
    }
}

void kinematics(JPSolutions& s, const JPIssueChecks::Context& c) {
    const JPCellConfig* cell = c.cell ? c.cell() : nullptr;
    if (!cell || !s.isTargeting(Milestone::Kinematics)) return;
    if (c.homed && !c.homed()) {
        Issue i;
        i.subject = "ReferenceMachine";
        i.issue = "To continue, the machine must be enabled and homed.";
        i.solution = "Home the machine now.";
        i.severity = Severity::Fundamental;
        i.uri = std::string(kWiki) + "User-Manual#machine-controls";
        i.extendedDescription = "Connect the machine first, then press Accept to home it.";
        i.canBeUndone = false;
        i.apply = [c](State to, std::string&) {
            if (to == State::Solved && c.home) c.home();
            return true;
        };
        s.add(std::move(i));
    }
    auto where = [&c](const std::string& axisId) { return c.axisPosition ? c.axisPosition(axisId) : std::nullopt; };
    // Safe Z of each nozzle's Z axis.
    const std::string safeZWiki = std::string(kWiki) + "Kinematic-Solutions#capture-safe-z";
    for (const JPNozzleConfig& n : cell->nozzles) {
        const JPAxisConfig* z = nullptr;
        for (const JPAxisConfig& a : cell->axes)
            if (a.id == n.mount.axisZ) z = &a;
        if (!z) continue;
        const std::string zId = z->id, zName = z->name, name = n.name;
        if (z->safeZoneLowEnabled && z->safeZoneHighEnabled && z->safeZoneLow > z->safeZoneHigh) {
            Issue i;
            i.subject = "ReferenceControllerAxis " + zName;
            i.issue = "Invalid Safe Z Zone on " + zName + ".";
            i.solution = "The Safe Z Zone of " + zName + " is invalid (lower limit > higher limit). Start fresh configuration.";
            i.severity = Severity::Error;
            i.uri = safeZWiki;
            const JPAxisConfig old = *z;
            i.apply = changing(c, "Safe Z zone", [zId, old](JPCellConfig& cell, bool solved) {
                if (JPAxisConfig* x = axisIn(cell, zId)) {
                    x->safeZoneLowEnabled = solved ? false : old.safeZoneLowEnabled;
                    x->safeZoneHighEnabled = solved ? false : old.safeZoneHighEnabled;
                }
            });
            s.add(std::move(i));
            continue;
        }
        if (z->safeZoneLowEnabled || z->safeZoneHighEnabled) continue;
        Issue i;
        i.subject = "ReferenceNozzle " + name;
        i.issue = "Set Safe Z of " + name + ".";
        i.solution = "Jog " + name + " over the tallest obstacle and capture.";
        i.severity = Severity::Fundamental;
        i.uri = safeZWiki;
        i.forcedUnsolved = true;
        i.extendedDescription = "Jog " + name + " over the tallest obstacle on your machine, including the the tallest parts "
                                "that may be placed on the PCB.\n\nThen lower it down so it still has sufficient clearance.\n\n"
                                "Then press Accept to capture the Safe Z.";
        const JPAxisConfig old = *z;
        i.apply = [c, zId, old, where](State to, std::string& why) {
            const auto at = where(zId);
            if (to == State::Solved && !at) {
                why = "Where the Z axis is cannot be told: connect and home the machine first.";
                return false;
            }
            if (c.changeCell)
                c.changeCell("Safe Z", [&](JPCellConfig& cell) {
                    if (JPAxisConfig* x = axisIn(cell, zId)) {
                        x->safeZoneLow = to == State::Solved ? *at : old.safeZoneLow;
                        x->safeZoneLowEnabled = to == State::Solved ? true : old.safeZoneLowEnabled;
                        x->safeZoneHighEnabled = to == State::Solved ? false : old.safeZoneHighEnabled;
                    }
                });
            return true;
        };
        s.add(std::move(i));
    }
    // Soft limits, feed rates and accelerations, rotation.
    for (const JPAxisConfig& a : cell->axes) {
        if (a.kind != JPAxisConfig::Kind::Controller) continue;
        const std::string id = a.id, subject = "ReferenceControllerAxis " + a.name;
        auto show = [c, id] {
            if (c.showSetup) c.showSetup("axis:" + id);
        };
        if (a.type == JPAxisConfig::Type::X || a.type == JPAxisConfig::Type::Y) {
            for (const bool low : { true, false }) {
                if (low ? a.softLimitLowEnabled : a.softLimitHighEnabled) continue;
                const std::string side = low ? "low side" : "high side";
                Issue i;
                i.subject = subject;
                i.issue = "Set the " + side + " soft limit of " + a.name + ".";
                i.solution = "Move axis " + a.name + " to the " + side + " soft limit and capture.";
                i.severity = Severity::Suggestion;
                i.uri = std::string(kWiki) + "Kinematic-Solutions#capture-soft-limits";
                i.activate = show;
                i.extendedDescription = "Move axis " + a.name + " to the " + side + " soft limit.\n\nIf the axis has a limit "
                                        "switch, use a position close to it but still safe to not trigger the switch by "
                                        "accident.\n\nThen press Accept to capture the soft limit.";
                const JPAxisConfig old = a;
                i.apply = [c, id, old, low, where](State to, std::string& why) {
                    const auto at = where(id);
                    if (to == State::Solved && !at) {
                        why = "Where the axis is cannot be told: connect and home the machine first.";
                        return false;
                    }
                    if (c.changeCell)
                        c.changeCell("Soft limit", [&](JPCellConfig& cell) {
                            JPAxisConfig* x = axisIn(cell, id);
                            if (!x) return;
                            const bool solved = to == State::Solved;
                            (low ? x->softLimitLow : x->softLimitHigh) = solved ? *at : (low ? old.softLimitLow : old.softLimitHigh);
                            (low ? x->softLimitLowEnabled : x->softLimitHighEnabled) =
                                solved || (low ? old.softLimitLowEnabled : old.softLimitHighEnabled);
                        });
                    return true;
                };
                s.add(std::move(i));
            }
        }
        const std::string motionWiki = std::string(kWiki) + "Machine-Axes#kinematic-settings--axis-limits";
        if (a.feedratePerSecond <= 0) {
            Issue i = plain(subject, "A feed-rate must be set on axis " + a.name + ".",
                            "Go to Machine Setup / Axes / ReferenceControllerAxis " + a.name + " and set the Feed Rate.",
                            Severity::Error, motionWiki);
            i.activate = show;
            s.add(std::move(i));
        }
        if (a.accelerationPerSecond2 <= 0) {
            Issue i = plain(subject, "An acceleration limit must be set on axis " + a.name + ".",
                            "Go to Machine Setup / Axes / ReferenceControllerAxis " + a.name + " and set the Acceleration.",
                            Severity::Error, motionWiki);
            i.activate = show;
            s.add(std::move(i));
        }
        if (a.type != JPAxisConfig::Type::Rotation) continue;
        bool onNozzle = false;
        for (const JPNozzleConfig& n : cell->nozzles) onNozzle = onNozzle || n.mount.axisRotation == a.id;
        if (!onNozzle) continue;
        const std::string rotationWiki = std::string(kWiki) + "Machine-Axes#controller-settings-rotational-axis";
        if (!a.wrapAroundRotation) {
            Issue i;
            i.subject = subject;
            i.issue = "Rotation can be optimized by wrapping-around the shorter way. Best combined with Limit ±180°.";
            i.solution = "Enable Wrap Around.";
            i.severity = Severity::Suggestion;
            i.uri = rotationWiki;
            i.apply = changing(c, "Wrap Around", [id](JPCellConfig& cell, bool solved) {
                if (JPAxisConfig* x = axisIn(cell, id)) x->wrapAroundRotation = solved;
            });
            s.add(std::move(i));
        }
        if (!a.limitRotation) {
            Issue i;
            i.subject = subject;
            i.issue = "Rotation can be optimized by limiting angles to ±180°. Best combined with Wrap Around.";
            i.solution = "Enable Limit to Range.";
            i.severity = Severity::Suggestion;
            i.uri = rotationWiki;
            i.apply = changing(c, "Limit to Range", [id](JPCellConfig& cell, bool solved) {
                if (JPAxisConfig* x = axisIn(cell, id)) x->limitRotation = solved;
            });
            s.add(std::move(i));
        }
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
        // OpenPnP's NozzleTipSolutions: the pick tolerance and the part diameters agreeing.
        const std::string tipWiki = std::string(kWiki) + "Setup-and-Calibration_Nozzle-Setup#nozzle-tip-configuration";
        char mm[48];
        if (t.maxPickToleranceMm > 1.0) {
            std::snprintf(mm, sizeof mm, "%.3f mm", t.maxPickToleranceMm);
            s.add(plain("ReferenceNozzleTip " + t.name, "Nozzle tip " + t.name + " has a large Max. Pick Tolerance of " + mm + ".",
                        "Set the Max. Pick Tolerance to the actual pick errors you expect. Press the blue info button (below) "
                        "for more information.", Severity::Error, tipWiki));
        } else if (t.minPartDiameterMm <= 2 * t.maxPickToleranceMm) {
            std::snprintf(mm, sizeof mm, "%.3f mm", t.minPartDiameterMm);
            char tol[48];
            std::snprintf(tol, sizeof tol, "%.3f mm", t.maxPickToleranceMm);
            s.add(plain("ReferenceNozzleTip " + t.name, "Nozzle tip " + t.name + " has an invalid Min. Part Diameter of " + mm + ".",
                        std::string("Make the Min. Part Diameter at least as big as the nozzle tip air bore plus two times the Max. "
                                    "Pick Tolerance of ") + tol + ". Press the blue info button (below) for more information.",
                        Severity::Error, tipWiki));
        } else if (t.minPartDiameterMm >= t.maxPartDiameterMm) {
            s.add(plain("ReferenceNozzleTip " + t.name,
                        "Nozzle tip " + t.name + " has a Max. Part Diameter that is not larger than the Min. Part Diameter.",
                        "Make sure the Max. Part Diameter is larger than the Min. Part Diameter. Press the blue info button "
                        "(below) for more information.", Severity::Error, tipWiki));
        }
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

// OpenPnP's ActuatorSolutions: what a holder (a nozzle, the head, a camera)
// needs of the actuator it uses for something (`qualifier`): one assigned, a
// controller for it (unless HTTP or a script works it), and the commands
// that do what is asked of it: switch it, set it, or read it.
enum class Use { Actuate, Read };
void actuatorIssues(JPSolutions& s, const JPIssueChecks::Context& c, const JPCellConfig& cell, const std::string& holder,
                    const std::string& actuatorId, const std::string& qualifier, Use use, const std::string& uri,
                    bool nested = false) {
    const JPActuatorConfig* a = nullptr;
    for (const JPActuatorConfig& x : cell.actuators)
        if (x.id == actuatorId) a = &x;
    if (!a) {
        s.add(plain(holder, holder + " is missing a " + qualifier + " actuator.",
                    "Create and assign a " + qualifier + " actuator as described in the Wiki.", Severity::Warning, uri));
        return;
    }
    const std::string subject = "ReferenceActuator " + a->name;
    if (!a->http.on && a->scriptName.empty() && a->driverId.empty()) {
        s.add(plain(subject, "The " + qualifier + " actuator " + a->name + " has no driver assigned.",
                    "Assign a driver as described in the Wiki.", Severity::Warning,
                    std::string(kWiki) + "Setup-and-Calibration%3A-Actuators#driver-assignment"));
        return;
    }
    if (a->http.on || !a->scriptName.empty()) return;   // worked by HTTP or a script, not by commands
    std::string driver = a->driverId;
    for (const JPDriverConfig& d : cell.drivers)
        if (d.id == a->driverId) driver = d.name;
    using V = JPActuatorConfig::ValueType;
    // A command it lacks: given with the issue, and set as it is accepted.
    auto missing = [&](const std::string& command, const std::string& label, std::string JPActuatorConfig::*field,
                       bool regex) {
        Issue i;
        i.subject = subject;
        i.issue = "The " + qualifier + " actuator " + a->name + " has no " + command + " assigned.";
        i.solution = "Assign the " + std::string(regex ? "regular expression" : "command") + " to driver " + driver
                   + " as described in the Wiki.";
        i.severity = Severity::Warning;
        i.uri = uri;
        auto text = std::make_shared<std::string>();
        JPSolutions::Property p;
        p.kind = JPSolutions::Property::Kind::Text;
        p.label = label;
        p.tooltip = "The " + label + " for the " + qualifier + " actuator, as its controller takes it.";
        p.getText = [text] { return *text; };
        p.setText = [text](const std::string& v) { *text = v; };
        i.properties.push_back(std::move(p));
        const std::string id = a->id;
        i.apply = [c, id, field, text](State to, std::string& why) {
            if (to == State::Solved && text->empty()) {
                why = "type the command first";
                return false;
            }
            if (c.changeCell)
                c.changeCell("Actuator command", [&](JPCellConfig& cell) {
                    for (JPActuatorConfig& x : cell.actuators)
                        if (x.id == id) x.*field = to == State::Solved ? *text : std::string();
                });
            return true;
        };
        s.add(std::move(i));
    };
    if (use == Use::Read) {
        if (a->readCommand.empty()) missing("ACTUATOR_READ_COMMAND", "Read Command", &JPActuatorConfig::readCommand, false);
        if (a->readPattern.empty()) missing("ACTUATOR_READ_REGEX", "Read Reply Pattern", &JPActuatorConfig::readPattern, true);
        return;
    }
    switch (a->valueType) {
        case V::Boolean:
            if (a->onCommand.empty() && a->offCommand.empty()) {
                missing("ACTUATE_BOOLEAN_COMMAND", "On Command", &JPActuatorConfig::onCommand, false);
                missing("ACTUATE_BOOLEAN_COMMAND (off)", "Off Command", &JPActuatorConfig::offCommand, false);
            }
            break;
        case V::Number:
            if (a->valueCommand.empty()) missing("ACTUATE_DOUBLE_COMMAND", "Set Value Command", &JPActuatorConfig::valueCommand, false);
            break;
        case V::Text:
            if (a->valueCommand.empty()) missing("ACTUATE_STRING_COMMAND", "Set Value Command", &JPActuatorConfig::valueCommand, false);
            break;
        case V::Profile:
            // Each of its profile's actuators, not further down (a profile of profiles would not end).
            if (!nested)
                for (const std::string& other : a->profileActuators)
                    if (!other.empty() && other != a->id)
                        actuatorIssues(s, c, cell, subject, other, qualifier, use, uri, true);
            break;
    }
}

void actuators(JPSolutions& s, const JPIssueChecks::Context& c) {
    const JPCellConfig* cell = c.cell ? c.cell() : nullptr;
    if (!cell || !s.isTargeting(Milestone::Basics)) return;
    const std::string vacuum = std::string(kWiki) + "Setup-and-Calibration%3A-Vacuum-Setup";
    // A nozzle tip that senses the vacuum needs something to read it with.
    bool sensing = false;
    for (const JPNozzleTipConfig& t : cell->nozzleTips)
        if (t.partOn.method != "None" || t.partOff.method != "None") sensing = true;
    for (const JPNozzleConfig& n : cell->nozzles) {
        const std::string holder = "ReferenceNozzle " + n.name;
        actuatorIssues(s, c, *cell, holder, n.vacuumActuatorId, "vacuum valve", Use::Actuate, vacuum);
        if (!n.blowOffActuatorId.empty()) actuatorIssues(s, c, *cell, holder, n.blowOffActuatorId, "blow off", Use::Actuate, vacuum);
        if (sensing)
            actuatorIssues(s, c, *cell, holder, n.vacuumSenseActuatorId, "vacuum sensing", Use::Read,
                           std::string(kWiki) + "Setup-and-Calibration%3A-Vacuum-Sensing#actuator-setup");
    }
    for (const JPHeadConfig& h : cell->heads) {
        const std::string holder = "ReferenceHead " + h.name;
        actuatorIssues(s, c, *cell, holder, h.pumpActuatorId, "pump control", Use::Actuate, vacuum + "#pump-control-setup");
        if (!h.zProbeActuatorId.empty())
            actuatorIssues(s, c, *cell, holder, h.zProbeActuatorId, "Z probe", Use::Read, std::string(kWiki) + "Z-Probing");
    }
    for (const JPCameraConfig& cam : cell->cameras) {
        const std::string holder = "Camera " + cam.name;
        actuatorIssues(s, c, *cell, holder, cam.lightActuator(), "camera light", Use::Actuate,
                       std::string(kWiki) + "Setup-and-Calibration%3A-Camera-Lighting");
        if (cam.device["backend"].str() == "switcher")
            actuatorIssues(s, c, *cell, holder, cam.device["actuator"].str(), "camera switcher", Use::Actuate,
                           std::string(kWiki) + "SwitcherCamera#configuration");
    }
}

// OpenPnP's CameraSolutions on how the cameras show: the preview's rate,
// suspended in tasks, brought forward, and drawn smoothed.
void cameraViews(JPSolutions& s, const JPIssueChecks::Context& c) {
    const JPCellConfig* cell = c.cell ? c.cell() : nullptr;
    if (!cell || !s.isTargeting(Milestone::Vision)) return;
    const std::string general = std::string(kWiki) + "Setup-and-Calibration_General-Camera-Setup#general-configuration";
    for (const JPCameraConfig& cam : cell->cameras) {
        const std::string id = cam.id, subject = "Camera " + cam.name;
        if (cam.previewFps > kMostPreviewFps) {
            Issue i;
            i.subject = subject;
            i.issue = "A high Preview FPS value might create undue CPU load.";
            i.solution = "Set to 5 FPS.";
            i.severity = Severity::Suggestion;
            i.uri = general;
            const double old = cam.previewFps;
            i.apply = changing(c, "Camera preview rate", [id, old](JPCellConfig& cell, bool solved) {
                for (JPCameraConfig& x : cell.cameras)
                    if (x.id == id) x.previewFps = solved ? kSuggestedPreviewFps : old;
            });
            s.add(std::move(i));
        }
        if (!cam.suspendDuringTasks) {
            const bool switcher = cam.device["backend"].str() == "switcher";
            Issue i;
            i.subject = subject;
            i.issue = std::string(switcher ? "For a SwitcherCamera it is mandatory" : "It is recommended")
                    + " to suspend camera preview during machine tasks / Jobs.";
            i.solution = "Enable Suspend during tasks.";
            i.severity = switcher ? Severity::Error : Severity::Suggestion;
            i.uri = general;
            i.apply = changing(c, "Camera suspended during tasks", [id](JPCellConfig& cell, bool solved) {
                for (JPCameraConfig& x : cell.cameras)
                    if (x.id == id) x.suspendDuringTasks = solved;
            });
            s.add(std::move(i));
        }
        if (!cam.autoCameraView) {
            Issue i;
            i.subject = subject;
            i.issue = "In single camera preview jplacer can automatically switch the camera for you.";
            i.solution = "Enable Auto Camera View.";
            i.severity = Severity::Suggestion;
            i.uri = general;
            i.apply = changing(c, "Auto Camera View", [id](JPCellConfig& cell, bool solved) {
                for (JPCameraConfig& x : cell.cameras)
                    if (x.id == id) x.autoCameraView = solved;
            });
            s.add(std::move(i));
        }
        if (c.renderingSmooth && c.setRenderingSmooth && !c.renderingSmooth(id)) {
            Issue i;
            i.subject = subject;
            i.issue = "The preview rendering quality can be improved.";
            i.solution = "Set to Rendering Quality to High (right click the Camera View to see other options).";
            i.severity = Severity::Suggestion;
            i.uri = std::string(kWiki) + "Setup-and-Calibration_General-Camera-Setup#camera-view-configuration";
            i.apply = [c, id](State to, std::string&) {
                c.setRenderingSmooth(id, to == State::Solved);
                return true;
            };
            s.add(std::move(i));
        }
    }
}

std::vector<JPSolutions::Check> JPIssueChecks::all(const Context& c) {
    return {
        [c](JPSolutions& s) { setupProblems(s, c); },
        [c](JPSolutions& s) { welcome(s, c); },
        [c](JPSolutions& s) { connect(s, c); },
        [c](JPSolutions& s) { basics(s, c); },
        [c](JPSolutions& s) { actuators(s, c); },
        [c](JPSolutions& s) { kinematics(s, c); },
        [c](JPSolutions& s) { vision(s, c); },
        [c](JPSolutions& s) { cameraViews(s, c); },
        [c](JPSolutions& s) { calibration(s, c); },
        [c](JPSolutions& s) { production(s, c); },
    };
}

} // inline namespace jf
