// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederActions.h"

#include "JPBlindsFeeder.h"
#include "JPFiducialLocator.h"
#include "JPHeapFeeder.h"
#include "JPPhotonFeeders.h"
#include "JPRapidScan.h"
#include "JPVisionTapeFeeder.h"

#include "common/JPlacerLog.h"
#include "model/JPFeederTape.h"

#include <j/core/Log.h>

#include <cctype>
#include <cstdio>

inline namespace jf {

namespace {

// What a Schultz feeder's buttons use: the action, the actuator's
// attribute (and OpenPnP's name for it in the log), whether it is read
// (else actuated), and what is read after.
struct SchultzButton {
    const char* action;
    const char* actuator;
    const char* logName;
    bool        read;
    const char* then;
};
constexpr SchultzButton kSchultz[] = {
    { "getId", "id-actuator-name", "getIdActuatorName", true, nullptr },
    { "testFeed", "actuator-name", "actuatorName", false, nullptr },
    { "testPostPick", "post-pick-actuator-name", "postPickActuatorName", false, "getFeedCount" },
    { "getFeedCount", "feed-count-actuator-name", "feedCountActuatorName", true, nullptr },
    { "clearFeedCount", "clear-count-actuator-name", "clearCountActuatorName", false, nullptr },
    { "getPitch", "pitch-actuator-name", "pitchActuatorName", true, nullptr },
    { "togglePitch", "toggle-pitch-actuator-name", "togglePitchActuatorName", false, "getPitch" },
    { "getStatus", "status-actuator-name", "statusActuatorName", true, nullptr },
};

// "Failed, " and why, its first letter small (OpenPnP's "Failed, unable to find…").
std::string failed(const std::string& prefix, std::string why) {
    if (!why.empty()) why[0] = char(std::tolower(static_cast<unsigned char>(why[0])));
    return prefix + why;
}

} // namespace

bool JPFeederActions::run(JPConfiguration& config, const std::string& feederId, const std::string& action,
                          JPJobMachine& machine, const OnMain& onMain, const JPVisionConfig& vision, Outcome& outcome,
                          std::string& why, const std::function<void(int address, int state)>& progress) {
    auto main = [&onMain](const std::function<void()>& fn) {
        if (onMain) onMain(fn);
        else fn();
    };
    std::string kind, name;
    double feederNumber = 0;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId)) {
            kind = f->feedsAs();
            name = f->name();
            feederNumber = f->real("actuator-value", 0);
        }
    });
    if (kind == "RapidFeeder" && action == "rapidScan") {
        int found = 0;
        outcome.changed = true;
        if (!JPRapidScan::scan(config, feederId, machine, onMain, found, why)) return false;
        JLOGC(JPlacerLog::kJob, JLogLevel::Info) << "Rapid scan: " << found << " feeder(s) found";
        return true;
    }
    if (kind == "BlindsFeeder" && action.rfind("blinds", 0) == 0) {
        // With the Jog panel's chosen nozzle, where one is pushing.
        const std::string nozzle = machine.chosenNozzle();
        outcome.changed = true;
        if (action == "blindsOcrDetect") return JPBlindsFeeder::performOcr(config, feederId, "ChangePart", machine, onMain, why);
        if (action == "blindsShowFeatures") return JPBlindsFeeder::showFeatures(config, feederId, machine, onMain, why);
        if (action == "blindsAutoSetup") return JPBlindsFeeder::autoSetup(config, feederId, machine, onMain, why);
        if (action == "blindsOpenCover" || action == "blindsCloseCover")
            return JPBlindsFeeder::actuateCover(config, feederId, nozzle, action == "blindsOpenCover", machine, onMain, why);
        if (action == "blindsOpenAll" || action == "blindsCloseAll")
            return JPBlindsFeeder::actuateAllCovers(config, nozzle, action == "blindsOpenAll", machine, onMain, why);
        if (action == "blindsCalibrateEdges") return JPBlindsFeeder::calibrateCoverEdges(config, feederId, machine, onMain, why);
        if (action == "blindsCalibrateFiducials") return JPBlindsFeeder::calibrateFiducials(config, feederId, machine, onMain, why);
    }
    if (kind == "ReferenceHeapFeeder" && (action == "cleanDropBox" || action == "getSamples")) {
        // With the head's first nozzle, as OpenPnP's (its default).
        const std::vector<JPJobMachine::Nozzle> nozzles = machine.nozzles();
        if (nozzles.empty()) {
            why = "no nozzle on the head";
            return false;
        }
        outcome.changed = true;
        return action == "cleanDropBox" ? JPHeapFeeder::cleanDropBox(config, feederId, nozzles.front().id, machine, onMain, why)
                                        : JPHeapFeeder::getSamples(config, feederId, nozzles.front().id, machine, onMain, why);
    }
    if ((kind == "BambooFeederAutoVision" || kind == "ReferencePushPullFeeder") && action == "showVisionFeatures")
        return JPVisionTapeFeeder::showFeatures(config, feederId, machine, onMain, why);
    if ((kind == "BambooFeederAutoVision" || kind == "ReferencePushPullFeeder") && action == "autoSetupTape") {
        outcome.changed = true;
        return JPVisionTapeFeeder::autoSetup(config, feederId, machine, onMain, why);
    }
    if (kind == "ReferencePushPullFeeder" && action == "autoSetupInRow") {
        std::optional<JPLocation> at;
        main([&] {
            if (const JPFeeder* f = config.feeder(feederId))
                at = JPFeederTape::partLocation(0, std::nullopt, JPFeederTape::of(*f), f->real("rotation-in-feeder", 0));
        });
        outcome.changed = true;
        return at && machine.positionCamera(*at, why) && JPVisionTapeFeeder::autoSetup(config, feederId, machine, onMain, why);
    }
    if (kind == "ReferencePushPullFeeder" && (action == "partByOcr" || action == "allFeederOcr")) {
        outcome.changed = true;
        bool ok = false;
        if (action == "partByOcr") {
            std::optional<JPLocation> at;
            main([&] {
                if (const JPFeeder* f = config.feeder(feederId)) at = JPVisionTapeFeeder::ocrLocation(*f);
            });
            ok = at && machine.positionCamera(*at, why)
                 && JPVisionTapeFeeder::performOcr(config, feederId, machine, onMain, { "ChangePart", false, &outcome.report }, why);
        } else {
            ok = JPVisionTapeFeeder::performOcrOnAll(config, feederId, machine, onMain, {}, false, outcome.report, why);
        }
        if (ok && outcome.report.empty()) outcome.report = "No action taken.";
        return ok;
    }
    if (kind == "ReferencePushPullFeeder" && action == "resetRotation") {
        // Its feed actuator's rotation axis called 0 where it is, in additive mode.
        std::string actuator;
        bool additive = false;
        main([&] {
            if (const JPFeeder* f = config.feeder(feederId)) {
                actuator = f->text("actuator-name");
                additive = f->flag("additive-rotation", true);
            }
        });
        return !additive || actuator.empty() || machine.zeroActuatorRotation(actuator, why);
    }
    if (kind == "BambooFeederAutoVision") {
        // Test feed and Test post pick: one actuation with its value (not a whole feed).
        const std::string key = action == "testFeed" ? "feed-actuator" : "post-pick-actuator";
        std::string actuator;
        double value = 0;
        main([&] {
            if (const JPFeeder* f = config.feeder(feederId)) {
                actuator = f->text(key + "-name");
                value = f->real(key + "-value", 0);
            }
        });
        if (actuator.empty()) {
            why = std::string("No ") + (action == "testFeed" ? "feedActuatorName" : "postPickActuatorName") + " specified for feeder "
                  + name + ".";
            return false;
        }
        if (machine.actuate(actuator, value, why)) return true;
        why = "Feed failed. " + why;
        return false;
    }
    if (kind == "ReferenceAutoFeeder") {
        std::string actuator;
        double value = 0;
        const std::string key = action == "testFeed" ? "actuator" : "post-pick-actuator";
        main([&] {
            if (const JPFeeder* f = config.feeder(feederId)) {
                actuator = f->text(key + "-name");
                value = f->real(key + "-value", 0);
            }
        });
        if (actuator.empty()) {
            why = "No actuator is set for it.";
            return false;
        }
        return machine.actuate(actuator, value, why);
    }
    if (action == "photonSearch") {
        outcome.changed = true;
        return JPPhotonFeeders::findAll(config, machine, onMain,
                                        [&progress](int address, JPPhotonFeeders::SearchState state) {
                                            if (progress) progress(address, int(state));
                                        },
                                        why);
    }
    if (kind == "PhotonFeeder" && action == "photonFind") {
        outcome.changed = true;
        return JPPhotonFeeders::findSlotAddress(config, feederId, machine, onMain, why);
    }
    if (kind == "PhotonFeeder" && (action == "photonFeed" || action == "photonFeed1mm")) {
        int pitch = 1;
        if (action == "photonFeed")
            main([&] {
                if (const JPFeeder* f = config.feeder(feederId)) pitch = f->number("part-pitch", 4);
            });
        outcome.changed = true;
        return JPPhotonFeeders::feed(config, feederId, "", pitch, machine, onMain, why);
    }
    if (kind == "Neoden4Feeder" && action == "actuate") {
        // Its actuator actuated with its pitch.
        std::string actuator;
        double pitch = 0;
        main([&] {
            if (const JPFeeder* f = config.feeder(feederId)) {
                actuator = f->text("actuator-name");
                pitch = f->lengthOf("part-pitch-in-tape", JPLength(4, JPLengthUnit::Millimeters)).value();
            }
        });
        if (machine.actuate(actuator, pitch, why)) return true;
        if (why.rfind("Unable to find", 0) == 0) why = "Can't find actuator '" + actuator + "'";
        return false;
    }
    if (action == "updateLocation") {
        std::string fiducial;
        JPLocation at(JPLengthUnit::Millimeters);
        double diameter = 0;
        JPJobMachine::FiducialLook look;
        std::string settings;
        JPFiducialLocator::PartProblem problem = JPFiducialLocator::PartProblem::None;
        bool known = false;
        main([&] {
            const JPFeeder* f = config.feeder(feederId);
            if (!f) return;
            fiducial = f->text("fiducial-part");
            at = f->location();
            if (const JPPart* part = config.part(fiducial)) {
                known = true;
                problem = JPFiducialLocator::partLook(config, *part, vision, diameter, look, settings);
            }
        });
        if (fiducial.empty()) {
            JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << "No fiducial defined for feeder " << name << ".";
            return true;
        }
        if (!known || problem != JPFiducialLocator::PartProblem::None) {
            why = !known ? "No part " + fiducial
                         : problem == JPFiducialLocator::PartProblem::NoSize
                               ? "Fiducial part " + fiducial + " has no footprint pad to give its size."
                               : "Part " + fiducial + " fiducial vision settings " + settings + " are disabled.";
            return false;
        }
        JPLocation found(JPLengthUnit::Millimeters);
        std::string seen;
        if (!machine.locateFiducial(at, look, found, seen)) {
            why = "Unable to locate fiducial";
            return false;
        }
        main([&] {
            if (JPFeeder* f = config.feeder(feederId)) {
                const JPLocation l = f->location().convertToUnits(JPLengthUnit::Millimeters);
                const JPLocation m = found.convertToUnits(JPLengthUnit::Millimeters);
                f->setLocation(l.derive(m.x(), m.y(), std::nullopt, std::nullopt));
            }
        });
        outcome.changed = true;
        return true;
    }
    if (kind != "SchultzFeeder") {
        why = action + " is not a " + kind + "'s";
        return false;
    }
    for (const SchultzButton& b : kSchultz) {
        if (action != b.action) continue;
        std::string actuator;
        main([&] {
            if (const JPFeeder* f = config.feeder(feederId)) actuator = f->text(b.actuator);
        });
        if (actuator.empty()) {
            JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << "No " << b.logName << " specified for feeder " << name << ".";
            return true;
        }
        if (b.read) {
            std::string value;
            char number[32];
            std::snprintf(number, sizeof number, "%g", feederNumber);
            if (!machine.readActuator(actuator, number, value, why)) {
                why = failed("Failed, ", why);
                return false;
            }
            outcome.readings.emplace_back(b.action, value);
            return true;
        }
        if (!machine.actuate(actuator, feederNumber, why)) {
            why = action == "testFeed" || action == "testPostPick" ? "Feed failed. " + why : failed("Failed, ", why);
            return false;
        }
        if (action == "clearFeedCount") outcome.readings.emplace_back("getFeedCount", "");
        return !b.then || run(config, feederId, b.then, machine, onMain, vision, outcome, why);
    }
    why = "no such action: " + action;
    return false;
}

} // inline namespace jf
