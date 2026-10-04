// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederActions.h"

#include "JPFiducialLocator.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <cctype>

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
                          JPJobMachine& machine, const OnMain& onMain, const std::string& fiducialVisionId, Outcome& outcome,
                          std::string& why) {
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
                problem = JPFiducialLocator::partLook(config, *part, fiducialVisionId, diameter, look, settings);
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
        if (!machine.locateFiducial(at, diameter, look, found, seen)) {
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
            if (!machine.readActuator(actuator, feederNumber, value, why)) {
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
        return !b.then || run(config, feederId, b.then, machine, onMain, fiducialVisionId, outcome, why);
    }
    why = "no such action: " + action;
    return false;
}

} // inline namespace jf
