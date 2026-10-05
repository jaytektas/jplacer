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
// OpenPnP's: a Safe Z above this (mm) is unconventional (Z=0 retracted, the boards below).
constexpr double kConventionalSafeZMm = 2;
// OpenPnP's: the calibration rig's two fiducials at least this far apart in Z (mm).
constexpr double kLeastRigZGapMm = 2;

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

// An issue solved by work on the machine (`start`, in the background): Accept
// starts it; when it fails, the issue is open again, as OpenPnP restores it.
using Work = std::function<void(std::function<void(bool ok)> finished)>;
void solvedByWork(JPSolutions& s, Issue& i, Work start) {
    const std::string fingerprint = i.fingerprint();
    i.canBeAccepted = true;
    i.apply = [sp = &s, fingerprint, start = std::move(start)](State to, std::string& why) {
        if (to != State::Solved) return true;   // what it did stays: done again on another Accept
        // Failing at once (the machine not ready): Accept fails. Failing later: open again.
        auto starting = std::make_shared<bool>(true);
        auto failedAtOnce = std::make_shared<bool>(false);
        start([sp, fingerprint, starting, failedAtOnce](bool ok) {
            if (ok) return;
            if (*starting) {
                *failedAtOnce = true;
                return;
            }
            for (const auto& issue : sp->issues())
                if (issue->fingerprint() == fingerprint && issue->state == State::Solved) {
                    std::string w;
                    sp->setState(*issue, State::Open, w);
                    sp->publish();
                }
        });
        *starting = false;
        if (*failedAtOnce) {
            why = "it could not be started (the status bar says why)";
            return false;
        }
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
    // OpenPnP's: each nozzle's Safe Z decided first, dynamic (lifted by the part's height) or fixed.
    for (const JPNozzleConfig& n : cell->nozzles) {
        Issue i;
        i.subject = "ReferenceNozzle " + n.name;
        i.issue = "Dynamic Safe Z for " + n.name + ".";
        i.solution = "Decide whether " + n.name + " has dynamic Safe Z or not.";
        i.severity = Severity::Fundamental;
        i.uri = std::string(kWiki) + "Kinematic-Solutions#dynamic-safe-z";
        i.choices = { { "Dynamic Safe Z", "If a part is on the nozzle, the nozzle is lifted to Safe Z + part height. Safe Z must "
                                          "only account for the tallest obstacle. This will result in faster, more optimized "
                                          "machine motion. Recommended." },
                      { "Fixed Safe Z", "Safe Z is always at a fixed level. Safe Z must account for both the tallest obstacle "
                                        "and the tallest part on the nozzle. This will result in slower, less optimized machine "
                                        "motion." } };
        i.choice = n.dynamicSafeZ ? "Dynamic Safe Z" : "Fixed Safe Z";
        const std::string id = n.id, text = i.issue;
        const bool old = n.dynamicSafeZ;
        // The choice taken on Accept: the published issue's (as the panel sets it).
        i.apply = [c, sp = &s, id, text, old](State to, std::string&) {
            std::string choice;
            for (const auto& p : sp->issues())
                if (p->issue == text) choice = p->choice;
            if (c.changeCell)
                c.changeCell("Dynamic Safe Z", [&](JPCellConfig& cell) {
                    for (JPNozzleConfig& x : cell.nozzles)
                        if (x.id == id) x.dynamicSafeZ = to == State::Solved ? choice == "Dynamic Safe Z" : old;
                });
            return true;
        };
        s.add(std::move(i));
    }
    // OpenPnP's NozzleTipSolutions: where a tip is changed by hand, captured from where the nozzle is.
    for (const JPNozzleConfig& n : cell->nozzles) {
        if (n.manualChangeLocation) continue;
        Issue i;
        i.subject = "ReferenceNozzle " + n.name;
        i.issue = "Set the manual nozzle tip change location for " + n.name + ".";
        i.solution = "Jog " + n.name + " to the manual nozzle tip changing location, then press Accept.";
        i.severity = Severity::Suggestion;
        i.uri = std::string(kWiki) + "Kinematic-Solutions#capture-safe-z";
        i.extendedDescription = "Jog " + n.name + " to a suitable location where you can manually exchange the nozzle tips.\n\n"
                                "Even if you (plan to) use an automatic nozzle tip changer, it is useful to have this location "
                                "defined, in case you want to disable automatic changing temporarily.\n\nOften it is best to move "
                                "Z all the way up so the tip is well reachable.\n\nPress Accept to store the location.";
        const std::string id = n.id;
        const JPMountConfig m = n.mount;
        i.apply = [c, id, m, where](State to, std::string& why) {
            std::optional<JPMachineLocation> at;
            if (to == State::Solved) {
                const auto x = where(m.axisX), y = where(m.axisY), z = where(m.axisZ), r = where(m.axisRotation);
                if (!x || !y) {
                    why = "Where the nozzle is cannot be told: connect and home the machine first.";
                    return false;
                }
                at = JPMachineLocation { *x + m.offsetX, *y + m.offsetY, z ? *z + m.offsetZ : 0.0, r.value_or(0.0) };
            }
            if (c.changeCell)
                c.changeCell("Manual tip change location", [&](JPCellConfig& cell) {
                    for (JPNozzleConfig& x : cell.nozzles)
                        if (x.id == id) x.manualChangeLocation = at;
                });
            return true;
        };
        s.add(std::move(i));
    }
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
        // The nozzle's Safe Z: where its Z axis's safe zone begins, plus its offset.
        const double safeZ = z->safeZoneLow + n.mount.offsetZ;
        if (z->safeZoneLowEnabled && z->safeZoneHighEnabled && safeZ > kConventionalSafeZMm)
            s.add(plain("ReferenceNozzle " + name, "Unconventional Z Axis on " + name + ".",
                        "The Safe Z of " + name + " is positive, which is unconventional. jplacer, as OpenPnP, typically uses Z "
                        "coordinates that have Z=0 when the nozzle is retracted, with the PCB surface in the negative Z range. "
                        "Please read the Wiki to understand the implications of not following this convention.",
                        Severity::Warning, std::string(kWiki) + "Machine-Axes#a-word-about-z-coordinates"));
        // Lifted by the tallest part a tip takes, the nozzle must stay within the safe zone.
        if (n.dynamicSafeZ && z->safeZoneLowEnabled && z->safeZoneHighEnabled)
            for (const JPNozzleTipConfig& t : cell->nozzleTips)
                if (n.fits(t.id) && safeZ + t.maxPartHeightMm > z->safeZoneHigh + n.mount.offsetZ)
                    s.add(plain("ReferenceNozzleTip " + t.name, "Nozzle " + name + " with tip " + t.name + " Safe Z Zone violation.",
                                "With dynamic safe Z, the Max. Part Height of each compatible nozzle tip must be smaller than the "
                                "axis " + zName + " Safe Z Zone.",
                                Severity::Error, std::string(kWiki) + "Kinematic-Solutions#dynamic-safe-z-zone"));
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
                            "Calibrate it (as the Calibrate button on its picture's tab does): jplacer then knows its "
                            "scale, its lens and where it is.",
                            Severity::Error, std::string(kWiki) + "Camera-Calibration");
            i.activate = show;
            i.extendedDescription = cam.mount.headId.empty()
                ? "CAUTION: a nozzle's tip goes down over the camera " + name + " and moves about in a grid a few "
                  "millimetres across (asked first). The nozzle must hold no part.\n\nWhen ready, press Accept."
                : "CAUTION: the camera " + name + " moves over the head's homing mark and through a grid of places "
                  "across its picture.\n\nWhen ready, press Accept.";
            if (c.calibrateCamera)
                solvedByWork(s, i, [c, id](std::function<void(bool)> finished) { c.calibrateCamera(id, std::move(finished)); });
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
    // OpenPnP's CalibrationSolutions: each head's X and Y backlash, measured by its camera over its homing mark.
    for (const JPHeadConfig& h : cell->heads) {
        const JPCameraConfig* camera = nullptr;
        for (const JPCameraConfig& cam : cell->cameras)
            if (!camera && cam.mount.headId == h.id && !cam.mount.axisX.empty()) camera = &cam;
        if (!camera || !h.homingFiducial || !c.calibrateBacklash || (c.calibrated && !c.calibrated(camera->id))) continue;
        for (const std::string& axisId : { camera->mount.axisX, camera->mount.axisY }) {
            const JPAxisConfig* a = cell->axis(axisId);
            if (!a || a->kind != JPAxisConfig::Kind::Controller) continue;
            Issue i;
            i.subject = "Camera " + camera->name;
            i.issue = "Calibrate backlash compensation for axis " + a->name + ".";
            i.solution = "Automatically calibrates the backlash compensation for " + a->name
                       + " using the primary calibration fiducial.";
            i.severity = Severity::Fundamental;
            i.uri = std::string(kWiki) + "Calibration-Solutions#calibrating-backlash-compensation";
            i.extendedDescription = "Backlash compensation is used to avoid the effects of any looseness or play in the "
                                    "mechanical linkages of machine axes.\n\nCAUTION: The camera " + camera->name
                                  + " will move over the primary fiducial and then perform a calibration motion pattern "
                                    "on the axis " + a->name + ".\n\nWhen ready, press Accept.";
            const std::string cameraId = camera->id;
            i.activate = [c, cameraId] {
                if (c.showSetup) c.showSetup("camera:" + cameraId);
            };
            solvedByWork(s, i, [c, axisId](std::function<void(bool)> finished) { c.calibrateBacklash(axisId, std::move(finished)); });
            s.add(std::move(i));
        }
    }
    // OpenPnP's NozzleTipSolutions: a tip's background calibration, chosen and then calibrated on its nozzle.
    for (const JPNozzleTipConfig& t : cell->nozzleTips) {
        if (t.background.method != "None" || !c.calibrateTip) continue;
        Issue i;
        i.subject = "ReferenceNozzleTip " + t.name;
        i.issue = "Set background calibration method for " + t.name + ".";
        i.solution = "Depending on the type of nozzle tip or shade, select the proper background calibration.";
        i.severity = Severity::Suggestion;
        i.uri = std::string(kWiki) + "Nozzle-Tip-Background-Calibration";
        i.choices = { { "BrightnessAndKeyColor", "Brightness and Key-Color: the nozzle tip and/or background (shade) is "
                                                 "color-keyed so computer vision can robustly distinguish background pixels "
                                                 "from foreground pixels (\"green-screening\"). Use for green Juki style nozzles." },
                      { "Brightness", "Brightness: the background is just dark, the foreground is distinguished by brightness only." } };
        i.choice = "BrightnessAndKeyColor";
        i.extendedDescription = "Select the proper background calibration.\n\nCAUTION: the nozzle the tip " + t.name
                              + " is loaded on will move over the up-looking camera and perform a new nozzle tip calibration, "
                                "including the background calibration.\n\nWhen ready, press Accept.";
        const std::string tipId = t.id, text = i.issue;
        Work work = [c, tipId, text, sp = &s](std::function<void(bool)> finished) {
            std::string choice = "BrightnessAndKeyColor";
            for (const auto& p : sp->issues())
                if (p->issue == text) choice = p->choice;
            if (c.changeCell)
                c.changeCell("Background calibration method", [&](JPCellConfig& cell) {
                    for (JPNozzleTipConfig& x : cell.nozzleTips)
                        if (x.id == tipId) x.background.method = choice;
                });
            c.calibrateTip(tipId, std::move(finished));
        };
        solvedByWork(s, i, std::move(work));
        s.add(std::move(i));
    }
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
    // OpenPnP's VisionSolutions in production: the tables linked.
    if (s.isTargeting(Milestone::Production) && c.tablesLinked && c.setTablesLinked && !c.tablesLinked()) {
        Issue i;
        i.subject = "ReferenceMachine";
        i.issue = "Link the Placements/Parts/Packages/Vision Settings/Feeders tables between tabs.";
        i.solution = "When a table row is selected on one tab, automatically select the corresponding ones on the other tabs. "
                     "For instance, if a placement is selected, the corresponding part will be selected on the Parts tab, "
                     "the package on the Packages tab, the vision settings on the Vision tab, and the feeder on the Feeders "
                     "tab, if one is present for the part.";
        i.severity = Severity::Suggestion;
        i.uri = std::string(kWiki) + "User-Manual#the-tabs";
        i.apply = [c](State to, std::string&) {
            c.setTablesLinked(to == State::Solved);
            return true;
        };
        s.add(std::move(i));
    }
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

// OpenPnP's HeadSolutions: on a head, every nozzle (and actuator or other
// camera with an X or Y axis) on the X and Y axes of its first camera.
void headAxes(JPSolutions& s, const JPIssueChecks::Context& c) {
    const JPCellConfig* cell = c.cell ? c.cell() : nullptr;
    if (!cell || !s.isTargeting(Milestone::Basics)) return;
    for (const JPHeadConfig& h : cell->heads) {
        const JPCameraConfig* camera = nullptr;
        for (const JPCameraConfig& cam : cell->cameras)
            if (!camera && cam.mount.headId == h.id && !cam.mount.axisX.empty() && !cam.mount.axisY.empty()) camera = &cam;
        if (!camera) continue;
        // `kind`: "nozzle", "actuator" or "camera"; `id` its id.
        auto check = [&](const std::string& subject, const std::string& kind, const std::string& id, const JPMountConfig& m) {
            for (const bool x : { true, false }) {
                const std::string& axis = x ? m.axisX : m.axisY;
                const std::string& wanted = x ? camera->mount.axisX : camera->mount.axisY;
                if ((kind != "nozzle" && axis.empty()) || axis == wanted) continue;
                const JPAxisConfig* had = cell->axis(axis);
                const JPAxisConfig* want = cell->axis(wanted);
                const char* type = x ? "X" : "Y";
                Issue i;
                i.subject = subject;
                i.issue = std::string("Inconsistent ") + type + " axis assignment " + (had ? had->name : std::string("null"))
                        + " (not the same as default camera " + camera->name + ").";
                i.solution = "Assign " + (want ? want->name : wanted) + " as the " + type + " axis.";
                i.severity = kind == "nozzle" ? Severity::Error : Severity::Warning;
                i.uri = std::string(kWiki) + "Mapping-Axes";
                const std::string old = axis;
                i.apply = changing(c, "Axis assignment", [kind, id, x, wanted, old](JPCellConfig& cell, bool solved) {
                    auto fix = [&](JPMountConfig& mount) { (x ? mount.axisX : mount.axisY) = solved ? wanted : old; };
                    if (kind == "nozzle")
                        for (JPNozzleConfig& n : cell.nozzles) { if (n.id == id) fix(n.mount); }
                    else if (kind == "actuator")
                        for (JPActuatorConfig& a : cell.actuators) { if (a.id == id) fix(a.mount); }
                    else
                        for (JPCameraConfig& cam : cell.cameras) { if (cam.id == id) fix(cam.mount); }
                });
                s.add(std::move(i));
            }
        };
        for (const JPNozzleConfig& n : cell->nozzles)
            if (n.mount.headId == h.id) check("ReferenceNozzle " + n.name, "nozzle", n.id, n.mount);
        for (const JPActuatorConfig& a : cell->actuators)
            if (a.mount.headId == h.id) check("ReferenceActuator " + a.name, "actuator", a.id, a.mount);
        for (const JPCameraConfig& cam : cell->cameras)
            if (cam.mount.headId == h.id && &cam != camera) check("Camera " + cam.name, "camera", cam.id, cam.mount);
    }
}

// OpenPnP's GcodeDriverSolutions, as far as they are not about firmwares
// jplacer meets through its profiles: serial flow control for a Grbl, pre-move
// commands and letter variables, the driver's maximum feed rate, and G-code
// compression and comments (the faster at Advanced, the safer before it).
void drivers(JPSolutions& s, const JPIssueChecks::Context& c) {
    const JPCellConfig* cell = c.cell ? c.cell() : nullptr;
    if (!cell) return;
    const std::string asyncWiki = std::string(kWiki) + "GcodeAsyncDriver#gcodedriver-new-settings";
    for (const JPDriverConfig& d : cell->drivers) {
        const std::string id = d.id, subject = "GcodeDriver " + d.name;
        auto set = [c, id](const char* what, std::function<void(JPDriverConfig&, bool solved)> edit) {
            return changing(c, what, [id, edit](JPCellConfig& cell, bool solved) {
                for (JPDriverConfig& x : cell.drivers)
                    if (x.id == id) edit(x, solved);
            });
        };
        bool hasAxes = false;
        for (const JPAxisConfig& a : cell->axes) hasAxes = hasAxes || a.driverId == d.id;
        // A Grbl takes no serial flow control (OpenPnP's FirmwareType.Grbl).
        const bool grbl = d.profile == "grbl" || d.profile == "grblhal";
        const std::string flow = d.link["flowControl"].str();
        if (s.isTargeting(Milestone::Connect) && grbl && d.link["type"].str() == "serial" && !flow.empty() && flow != "none") {
            Issue i;
            i.subject = subject;
            i.issue = "Change of serial port Flow Control recommended.";
            i.solution = "Set Flow Control to Off on serial port. The detected Grbl controller is known to not (reliably) "
                         "support serial flow-control.";
            i.severity = Severity::Warning;
            i.uri = "https://en.wikipedia.org/wiki/Flow_control_(data)#Hardware_flow_control";
            i.apply = set("Serial flow control", [flow](JPDriverConfig& x, bool solved) { x.link["flowControl"] = solved ? std::string() : flow; });
            s.add(std::move(i));
        }
        if (hasAxes && s.isTargeting(Milestone::Basics)) {
            Issue i;
            i.subject = subject;
            i.severity = Severity::Fundamental;
            i.uri = std::string(kWiki) + "Advanced-Motion-Control#migration-from-a-previous-version";
            if (d.supportingPreMove) {
                i.issue = "Disallow Pre-Move Commands for automatic G-code setup and other advanced features. Accept or Dismiss to continue.";
                i.solution = "Disable Allow Letter Pre-Move Commands.";
                i.apply = set("Pre-move commands", [](JPDriverConfig& x, bool solved) { x.supportingPreMove = !solved; });
                s.add(std::move(i));
            } else if (!d.usingLetterVariables) {
                i.issue = "Use Axis Letter Variables for simpler use, automatic G-code setup, and other advanced features.";
                i.solution = "Enable Letter Variables.";
                i.apply = set("Letter variables", [](JPDriverConfig& x, bool solved) { x.usingLetterVariables = solved; });
                s.add(std::move(i));
            }
        }
        if (hasAxes && s.isTargeting(Milestone::Kinematics) && d.maxFeedRate > 0) {
            Issue i;
            i.subject = subject;
            i.issue = "Axis velocity limited by driver Maximum Feed Rate. ";
            i.solution = "Remove driver Maximum Feed Rate.";
            i.severity = Severity::Suggestion;
            i.uri = asyncWiki;
            const double old = d.maxFeedRate;
            i.apply = set("Maximum feed rate", [old](JPDriverConfig& x, bool solved) { x.maxFeedRate = solved ? 0 : old; });
            s.add(std::move(i));
        }
        if (s.isTargeting(Milestone::Advanced)) {
            if (!d.compressGcode) {
                Issue i;
                i.subject = subject;
                i.issue = "Compress Gcode for superior communications speed.";
                i.solution = "Enable Compress Gcode.";
                i.severity = Severity::Suggestion;
                i.uri = asyncWiki;
                i.apply = set("Compress G-code", [](JPDriverConfig& x, bool solved) { x.compressGcode = solved; });
                s.add(std::move(i));
            }
            if (!d.removeComments) {
                Issue i;
                i.subject = subject;
                i.issue = "Remove Gcode comments for superior communications speed.";
                i.solution = "Enable Remove Comments.";
                i.severity = Severity::Suggestion;
                i.uri = asyncWiki;
                i.apply = set("Remove G-code comments", [](JPDriverConfig& x, bool solved) { x.removeComments = solved; });
                s.add(std::move(i));
            }
        } else {
            // Conservative: offered, never counted as unhandled.
            if (d.compressGcode) {
                Issue i;
                i.subject = subject;
                i.issue = "Disable G-code compression for trouble-free operation with incompatible controllers.";
                i.solution = "Disable Compress G-code.";
                i.severity = Severity::Information;
                i.uri = asyncWiki;
                i.neverUnhandled = true;
                i.extendedDescription = "CAUTION: This is a troubleshooting option, you should only disable G-code "
                                        "compression if it causes problems.";
                i.apply = set("Compress G-code", [](JPDriverConfig& x, bool solved) { x.compressGcode = !solved; });
                s.add(std::move(i));
            }
            if (d.removeComments) {
                Issue i;
                i.subject = subject;
                i.issue = "Keep G-code comments for better debugging.";
                i.solution = "Disable Remove Comments.";
                i.severity = Severity::Information;
                i.uri = asyncWiki;
                i.neverUnhandled = true;
                i.apply = set("Remove G-code comments", [](JPDriverConfig& x, bool solved) { x.removeComments = !solved; });
                s.add(std::move(i));
            }
        }
    }
}

// OpenPnP's CameraSolutions on a capture device's own settings: none
// automatic, and each at the value that gives the sensor's raw picture.
void cameraProperties(JPSolutions& s, const JPIssueChecks::Context& c) {
    const JPCellConfig* cell = c.cell ? c.cell() : nullptr;
    if (!cell || !c.cameraControls || !s.isTargeting(Milestone::Vision)) return;
    const std::string uri = std::string(kWiki) + "OpenPnpCaptureCamera#camera-properties";
    for (const JPCameraConfig& cam : cell->cameras) {
        if (cam.device["backend"].str() != "v4l2") continue;
        const JJson have = c.cameraControls(cam.id);
        const std::string id = cam.id, subject = "Camera " + cam.name;
        // Set by hand to `value` on Accept; back as it was on Reopen.
        auto apply = [c, id](const std::string& key, const JJson& was, int value) {
            return changing(c, "Camera " + key, [id, key, was, value](JPCellConfig& cell, bool solved) {
                for (JPCameraConfig& x : cell.cameras) {
                    if (x.id != id) continue;
                    if (!solved) {
                        if (was.isObject()) x.device["controls"][key] = was;
                        else {
                            JJson rest = JJson::object();
                            for (const auto& [n, v] : std::as_const(x.device)["controls"].obj()) if (n != key) rest[n] = v;
                            x.device["controls"] = rest;
                        }
                        continue;
                    }
                    x.device["controls"][key]["auto"] = false;
                    x.device["controls"][key]["value"] = value;
                }
            });
        };
        // OpenPnP's property, its name in words, what to do, and the value wanted (its default unless said).
        struct Wanted { const char* key; const char* words; const char* what; bool minimum; const char* uri; };
        static const Wanted kWanted[] = {
            { "brightness", "brightness", "revert to the default setting", false, nullptr },
            { "contrast", "contrast", "revert to the default setting", false, nullptr },
            { "gamma", "gamma", "revert to the default setting", false, nullptr },
            { "gain", "gain", "revert to the default setting", false, nullptr },
            { "sharpness", "sharpness", "set to the minimum", true, nullptr },
            { "hue", "hue", "revert to the default setting", false, nullptr },
            { "saturation", "saturation", "revert to the default setting", false, nullptr },
            { "white-balance", "white balance",
              "revert to the default setting. Issues & Solutions will propose calibrating static white balance instead", false,
              "Camera-White-Balance#problems-with-device-white-balance" },
        };
        for (const Wanted& w : kWanted) {
            const JJson& p = have[w.key];
            if (!p.isObject()) continue;
            const int wanted = int(p[w.minimum ? "min" : "default"].number());
            const bool automatic = p["auto"].boolean();
            if (!automatic && int(p["value"].number()) == wanted) continue;
            Issue i;
            i.subject = subject;
            i.issue = std::string("The ") + w.words + " of camera " + cam.name + " should be set to " + std::to_string(wanted) + ".";
            i.solution = std::string("Computer vision works best with raw information from the camera sensor, even if the images "
                                     "look less appealing to humans. ")
                       + (automatic ? std::string("Switch off the Auto ") + w.words + " and " : std::string("Therefore, ")) + w.what + ".";
            i.severity = Severity::Suggestion;
            i.uri = w.uri ? std::string(kWiki) + w.uri : uri;
            i.apply = apply(w.key, std::as_const(cam.device)["controls"][w.key], wanted);
            s.add(std::move(i));
        }
        // Exposure: not automatic; kept at what the camera chose for what it sees now.
        if (const JJson& e = have["exposure"]; e.isObject() && e["auto"].boolean()) {
            Issue i;
            i.subject = subject;
            i.issue = "The exposure of camera " + cam.name + " should not be set to Auto.";
            i.solution = "Computer vision can only be robust and repeatable if the effect of exposure is stable. Switch off the "
                         "Auto exposure and set to a static exposure value: the one it has chosen now. The camera should look at "
                         "a representative, rather bright subject when you press Accept.";
            i.severity = Severity::Suggestion;
            i.uri = uri;
            i.apply = apply("exposure", std::as_const(cam.device)["controls"]["exposure"], int(e["value"].number()));
            s.add(std::move(i));
        }
    }
}

// OpenPnP's VisionSolutions, as far as they are not jplacer's own
// calibration: visual homing, and the calibration rig's heights.
void visionSetup(JPSolutions& s, const JPIssueChecks::Context& c) {
    const JPCellConfig* cell = c.cell ? c.cell() : nullptr;
    if (!cell || !s.isTargeting(Milestone::Vision)) return;
    const std::string nozzleOffsets = std::string(kWiki) + "Vision-Solutions#nozzle-offsets";
    for (const JPHeadConfig& h : cell->heads) {
        const JPCameraConfig* camera = nullptr;
        for (const JPCameraConfig& cam : cell->cameras)
            if (!camera && cam.mount.headId == h.id && !cam.mount.axisX.empty()) camera = &cam;
        if (camera && !h.visualHoming && c.enableVisualHoming && (!c.calibrated || c.calibrated(camera->id))) {
            Issue i;
            i.subject = "ReferenceHead " + h.name;
            i.issue = "Enable Visual Homing.";
            i.solution = "Mount a permanent fiducial to your machine and use it for repeatable precision X/Y homing.";
            i.severity = Severity::Suggestion;
            i.uri = std::string(kWiki) + "Visual-Homing";
            i.extendedDescription =
                "Mount a permanent fiducial to your machine table. Choose a mounting point that is mechanically coupled to "
                "the most important parts of your machine table. Make sure it is very unlikely you will ever need to change "
                "this new frame of reference. The fiducial must be at the same Z level as the PCB surface.\n\nHome the "
                "machine. Jog camera " + camera->name + " over the fiducial, roughly on the cross-hairs.\n\nThen press "
                "Accept to detect the precise position of the fiducial and set it up for visual homing.\n\nNote: This will "
                "not change your present machine coordinate system, but rather pin it down to the fiducial.";
            const std::string cameraId = camera->id, headId = h.id;
            i.activate = [c, cameraId] {
                if (c.showSetup) c.showSetup("camera:" + cameraId);
            };
            solvedByWork(s, i, [c, headId](std::function<void(bool)> finished) { c.enableVisualHoming(headId, std::move(finished)); });
            s.add(std::move(i));
        }
        if (h.rigPrimary && h.rigSecondary && std::abs(h.rigPrimary->z - h.rigSecondary->z) < kLeastRigZGapMm) {
            char gap[32];
            std::snprintf(gap, sizeof gap, "%g", kLeastRigZGapMm);
            s.add(plain("ReferenceHead " + h.name, "Primary/secondary calibration fiducial Z too close together.",
                        "Head " + h.name + " primary and secondary calibration fiducial Z coordinates must be at least "
                            + gap + "\u00A0mm apart.",
                        Severity::Error, nozzleOffsets));
        }
        // The head's first nozzle comes down to the rig's fiducials from its Safe Z.
        const JPNozzleConfig* nozzle = nullptr;
        for (const JPNozzleConfig& n : cell->nozzles)
            if (!nozzle && n.mount.headId == h.id) nozzle = &n;
        const JPAxisConfig* z = nozzle ? cell->axis(nozzle->mount.axisZ) : nullptr;
        if (!z || !z->safeZoneLowEnabled) continue;
        const double safeZ = z->safeZoneLow + nozzle->mount.offsetZ;
        for (const auto& [rig, qualifier] : { std::pair { h.rigPrimary, "primary" }, std::pair { h.rigSecondary, "secondary" } })
            if (rig && rig->z >= safeZ)
                s.add(plain("ReferenceNozzle " + nozzle->name, "Safe Z of Nozzle " + nozzle->name + " lower than " + qualifier + " fiducial Z.",
                            "Safe Z of Nozzle " + nozzle->name + " is lower than the calibration " + qualifier + " fiducial Z. "
                                + "Please change the calibration rig " + qualifier + " height or adjust Safe Z.",
                            Severity::Error, nozzleOffsets));
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
        [c](JPSolutions& s) { headAxes(s, c); },
        [c](JPSolutions& s) { drivers(s, c); },
        [c](JPSolutions& s) { kinematics(s, c); },
        [c](JPSolutions& s) { vision(s, c); },
        [c](JPSolutions& s) { cameraViews(s, c); },
        [c](JPSolutions& s) { cameraProperties(s, c); },
        [c](JPSolutions& s) { visionSetup(s, c); },
        [c](JPSolutions& s) { calibration(s, c); },
        [c](JPSolutions& s) { production(s, c); },
    };
}

} // inline namespace jf
