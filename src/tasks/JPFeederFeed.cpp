// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederFeed.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <cmath>
#include <cstdlib>
#include <optional>

inline namespace jf {

namespace {

// A hole found farther than this from where it should be is not taken (mm).
constexpr double kMostOffMm = 2.0;
constexpr const char* kEndOfStrip = "Unable to locate reference hole. End of strip?";
constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
// The step back along the tape to each of the parts one feed brings (OpenPnP's partsPitchX, for 0402).
constexpr double kPartsPitchXMm = -2;
// What a Rapid feeder's feed is sent to (OpenPnP's RapidFeeder.actuatorName).
constexpr const char* kRapidActuator = "RAPIDFEEDER";
// A lever's push moves the tape this far: a longer pitch takes more pushes.
constexpr double kLeverStrokeMm = 4;

} // namespace

bool JPFeederFeed::pinFeed(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                           std::string& why) {
    auto main = [&onMain](const std::function<void()>& fn) {
        if (onMain) onMain(fn);
        else fn();
    };
    std::string actuator, peelOff, templatePath;
    JPLocation start(kMm), end(kMm), location(kMm);
    std::optional<JPLocation> visionOffset;
    double pitch = 0, speed = 1, backoff = 0;
    bool vision = false, part0402 = false, lever = false;
    JPTemplateFinder::Area aoi;
    int fed = 0;
    main([&] {
        const JPFeeder* f = config.feeder(feederId);
        if (!f) return;
        lever = f->typeName() == "ReferenceLeverFeeder";
        actuator = f->text("actuator-name");
        peelOff = f->text("peel-off-actuator-name");
        start = f->locationOf("feed-start-location").convertToUnits(kMm);
        end = f->locationOf("feed-end-location").convertToUnits(kMm);
        location = f->location().convertToUnits(kMm);
        pitch = f->lengthOf("part-pitch", JPLength(4, kMm)).convertToUnits(kMm).value();
        speed = std::strtod(f->childText("feed-speed", "1.0").c_str(), nullptr);
        backoff = f->lengthOf("backoff-distance", JPLength(0, kMm)).convertToUnits(kMm).value();
        vision = f->attributeAt("vision", "enabled") == "true";
        templatePath = f->templatePath(config.directory());
        aoi = { std::atoi(f->attributeAt("vision/area-of-interest", "x", "0").c_str()),
                std::atoi(f->attributeAt("vision/area-of-interest", "y", "0").c_str()),
                std::atoi(f->attributeAt("vision/area-of-interest", "width", "0").c_str()),
                std::atoi(f->attributeAt("vision/area-of-interest", "height", "0").c_str()) };
        visionOffset = f->templateOffset;
        fed = f->partsFed;
        // OpenPnP's isPart0402: its package's id says C0402 or R0402.
        if (const JPPart* p = config.part(f->partId()))
            part0402 = p->packageId.find("C0402") != std::string::npos || p->packageId.find("R0402") != std::string::npos;
    });
    if (actuator.empty()) {
        why = "No actuator name set.";
        return false;
    }
    auto look = [&](JPLocation& offset) {
        if (templatePath.empty()) {
            why = "Template image is required when vision is enabled.";
            return false;
        }
        if (aoi.width == 0 || aoi.height == 0) {
            why = "Area of Interest is required when vision is enabled.";
            return false;
        }
        return machine.matchTemplate(location, templatePath, aoi, offset, why);
    };
    if (!machine.safeZ(why)) return false;
    if (!lever && vision && !visionOffset) {
        // The first feed with vision (or its offset reset): looked at before the drag.
        JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "First feed, running vision pre-flight.";
        JPLocation offset(kMm);
        if (!look(offset)) return false;
        visionOffset = offset;
        fed = 0;
    }
    if (fed == 0 && lever) {
        // The lever pushed from the start to the end and let back, once per
        // 4 mm of the pitch, the take up running while it comes back.
        for (double left = pitch; left > 0; left -= kLeverStrokeMm) {
            if (!machine.moveActuator(actuator, start, false, 1.0, why) || !machine.actuate(actuator, 1, why) ||
                !machine.moveActuator(actuator, end, true, speed, why))
                return false;
            if (!peelOff.empty() && !machine.actuate(peelOff, 1, why)) return false;
            if (!machine.moveActuator(actuator, start, false, 1.0, why)) return false;
            if (!peelOff.empty() && !machine.actuate(peelOff, 0, why)) return false;
            if (!machine.actuate(actuator, 0, why)) return false;
        }
        if (pitch == 2) fed = 2;
    } else if (fed == 0) {
        JPLocation from = start;
        if (vision && visionOffset) from = from.subtract(*visionOffset);
        // Over the start, the pin out and down into the tape, dragged to the end.
        if (!machine.moveActuator(actuator, from, false, 1.0, why) || !machine.actuate(actuator, 1, why) ||
            !machine.moveActuator(actuator, from, true, 1.0, why) || !machine.moveActuator(actuator, end, true, speed, why))
            return false;
        if (!peelOff.empty() && (!machine.actuate(peelOff, 1, why) || !machine.actuate(peelOff, 0, why))) return false;
        // Backed off along the drag to take the tension off the pin.
        const double length = std::hypot(from.x() - end.x(), from.y() - end.y());
        if (backoff != 0 && length > 0) {
            const JPLocation back = end.derive(end.x() + (from.x() - end.x()) / length * backoff,
                                               end.y() + (from.y() - end.y()) / length * backoff, std::nullopt, std::nullopt);
            if (!machine.moveActuator(actuator, back, true, speed, why)) return false;
        }
        if (!machine.actuate(actuator, 0, why)) return false;
        if (part0402) {
            pitch = 2;
            main([&] {
                if (JPFeeder* f = config.feeder(feederId)) f->setLengthOf("part-pitch", JPLength(2, kMm));
            });
        }
        if (pitch == 2) fed = 2;
    } else {
        JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << (lever ? "Multi parts Lever feeder: skipping feed " : "Multi parts drag feeder: skipping drag ")
                                                  << fed;
    }
    if (!machine.safeZ(why)) return false;
    std::optional<JPLocation> partPick;
    if (fed > 0) {
        --fed;
        if (fed > 0) partPick = JPLocation(kMm, kPartsPitchXMm * fed, 0, 0, 0);
    }
    if (vision) {
        JPLocation offset(kMm);
        if (!look(offset)) return false;
        visionOffset = offset;
    }
    main([&] {
        if (JPFeeder* f = config.feeder(feederId)) {
            f->partsFed = fed;
            f->nextPartPick = partPick;
            f->templateOffset = visionOffset;
        }
    });
    return true;
}

bool JPFeederFeed::feed(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId,
                        JPJobMachine& machine, const OnMain& onMain, std::string& why, bool& empty) {
    auto main = [&onMain](const std::function<void()>& fn) {
        if (onMain) onMain(fn);
        else fn();
    };
    bool fed = false, actuate = false;
    empty = false;
    std::vector<int> checks;
    std::string actuatorName;
    double actuatorValue = 0;
    bool moveFirst = false;
    std::optional<JPLocation> pickAt;
    main([&] {
        JPFeeder* f = config.feeder(feederId);
        if (!f) {
            why = "no feeder " + feederId;
            return;
        }
        fed = f->feed(why, &empty);
        checks = f->takeVisionChecks();
        actuate = f->takeFeedActuation();
        actuatorName = f->text("actuator-name");
        actuatorValue = f->real("actuator-value", 0);
        moveFirst = f->flag("move-before-feed", false);
        pickAt = f->pickLocation();
    });
    if (!fed) return false;
    bool pinned = false;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId))
            pinned = f->typeName() == "ReferenceDragFeeder" || f->typeName() == "ReferenceLeverFeeder";
    });
    if (pinned) return pinFeed(config, feederId, machine, onMain, why);
    // A Schultz feeder: the nozzle over its pick place (at safe Z), its pre
    // pick actuator actuated with its feeder number.
    std::string schultz, schultzName;
    double feederNumber = 0;
    std::optional<JPLocation> schultzAt;
    bool isSchultz = false;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId); f && f->feedsAs() == "SchultzFeeder") {
            isSchultz = true;
            schultz = f->text("actuator-name");
            schultzName = f->name();
            feederNumber = f->real("actuator-value", 0);
            schultzAt = f->pickLocation();
        }
    });
    if (isSchultz) {
        if (schultz.empty()) {
            JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << "No actuatorName specified for feeder " << schultzName << ".";
            return true;
        }
        if (schultzAt && !nozzleId.empty() && !machine.positionNozzle(nozzleId, *schultzAt, why)) return false;
        if (machine.actuate(schultz, feederNumber, why)) return true;
        why = "Feed failed. " + why;
        return false;
    }
    // A Neoden 4 feeder: its actuator actuated with its pitch; then, with
    // vision, the template looked for (a failure only logged), and the count on.
    bool neoden = false;
    std::string neodenActuator, neodenTemplate;
    double neodenPitch = 0;
    bool neodenVision = false;
    JPLocation neodenAt(kMm);
    JPTemplateFinder::Area neodenArea;
    main([&] {
        const JPFeeder* f = config.feeder(feederId);
        if (!f || f->typeName() != "Neoden4Feeder") return;
        neoden = true;
        neodenActuator = f->text("actuator-name");
        neodenPitch = f->lengthOf("part-pitch-in-tape", JPLength(4, kMm)).value();
        neodenVision = f->attributeAt("vision", "enabled") == "true";
        neodenTemplate = f->templatePath(config.directory());
        neodenAt = f->location();
        neodenArea = { std::atoi(f->attributeAt("vision/area-of-interest", "x", "0").c_str()),
                       std::atoi(f->attributeAt("vision/area-of-interest", "y", "0").c_str()),
                       std::atoi(f->attributeAt("vision/area-of-interest", "width", "0").c_str()),
                       std::atoi(f->attributeAt("vision/area-of-interest", "height", "0").c_str()), true };
    });
    if (neoden) {
        if (neodenActuator.empty()) {
            why = "No actuator name set.";
            return false;
        }
        if (!machine.actuate(neodenActuator, neodenPitch, why)) return false;
        if (neodenVision) {
            std::string seen;
            JPLocation offset(kMm);
            if (neodenTemplate.empty()) seen = "Template image is required when vision is enabled.";
            else if (neodenArea.width == 0 || neodenArea.height == 0) seen = "Area of Interest is required when vision is enabled.";
            if (seen.empty() && machine.matchTemplate(neodenAt, neodenTemplate, neodenArea, offset, seen)) {
                main([&] {
                    if (JPFeeder* f = config.feeder(feederId)) f->templateOffset = offset;
                });
            } else {
                JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "Neoden 4 feeder vision: " << seen;
            }
        }
        main([&] {
            if (JPFeeder* f = config.feeder(feederId)) f->setNumber("feed-count", f->number("feed-count") + 1);
        });
        return true;
    }
    // A Rapid feeder: its address and pitch to the RAPIDFEEDER actuator.
    std::string rapid;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId); f && f->typeName() == "RapidFeeder")
            rapid = f->text("address") + " " + std::to_string(f->number("pitch", 4));
    });
    if (!rapid.empty()) {
        if (machine.actuateText(kRapidActuator, rapid, why)) return true;
        why = "Feed failed. " + why;
        return false;
    }
    if (actuate) {
        if (actuatorName.empty()) {
            JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << "No actuatorName specified for feeder " << feederId << ".";
        } else {
            if (moveFirst && pickAt && !nozzleId.empty() && !machine.positionNozzle(nozzleId, *pickAt, why)) return false;
            if (!machine.actuate(actuatorName, actuatorValue, why)) {
                why = "Feed failed. " + why;
                return false;
            }
        }
    }
    for (const int n : checks) {
        std::optional<JPLocation> expected;
        double diameter = 0, search = 0, parallaxDiameter = 0, parallaxAngle = 0;
        std::string name;
        main([&] {
            if (const JPFeeder* f = config.feeder(feederId)) {
                expected = f->visionExpected(n);
                diameter = f->holeDiameter().convertToUnits(JPLengthUnit::Millimeters).value();
                search = f->holePitch().convertToUnits(JPLengthUnit::Millimeters).value() / 2;
                parallaxDiameter = f->lengthOf("parallax-diameter", JPLength(0, JPLengthUnit::Millimeters))
                                       .convertToUnits(JPLengthUnit::Millimeters)
                                       .value();
                parallaxAngle = std::strtod(f->childText("parallax-angle", "0").c_str(), nullptr);
                name = f->name();
            }
        });
        if (!expected) continue;
        JPLocation found(JPLengthUnit::Millimeters);
        std::string seen;
        const bool ok = machine.locateHole(*expected, diameter, search, parallaxDiameter, parallaxAngle, found, seen);
        const JPLocation e = expected->convertToUnits(JPLengthUnit::Millimeters);
        if (!ok || std::hypot(found.x() - e.x(), found.y() - e.y()) > kMostOffMm) {
            JLOGC(JPlacerLog::kJob, JLogLevel::Info) << name << ": hole " << n << " not found" << (ok ? "" : ": " + seen);
            why = kEndOfStrip;
            empty = true;
            return false;
        }
        main([&] {
            if (JPFeeder* f = config.feeder(feederId)) f->setVisionFound(n, found);
        });
    }
    return true;
}

bool JPFeederFeed::postPick(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                            std::string& why) {
    std::string name;
    double value = 0;
    auto main = [&onMain](const std::function<void()>& fn) {
        if (onMain) onMain(fn);
        else fn();
    };
    bool emptySlot = false;
    main([&] {
        const JPFeeder* f = config.feeder(feederId);
        if (!f) return;
        emptySlot = f->isSlot() && !f->slotLoad;
        if (f->feedsAs() == "ReferenceAutoFeeder") {
            name = f->text("post-pick-actuator-name");
            value = f->real("post-pick-actuator-value", 0);
        } else if (f->feedsAs() == "SchultzFeeder") {
            // With its feeder number.
            name = f->text("post-pick-actuator-name");
            value = f->real("actuator-value", 0);
        }
    });
    if (emptySlot) {
        why = "No feeder loaded in slot.";
        return false;
    }
    if (name.empty()) return true;
    if (!machine.actuate(name, value, why)) {
        why = "Post pick failed. " + why;
        return false;
    }
    return true;
}

} // inline namespace jf
