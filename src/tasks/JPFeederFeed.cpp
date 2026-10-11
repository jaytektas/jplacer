// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederFeed.h"

#include "tasks/JPFeederPipelines.h"

#include "JPVisionTapeFeeder.h"
#include "JPBlindsFeeder.h"
#include "JPHeapFeeder.h"

#include "JPFeederPipelines.h"

#include "JPPhotonFeeders.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <cmath>
#include <cstdlib>
#include <optional>

inline namespace jf {

namespace {

// A loose part feeder's looks (OpenPnP's three), and how long its picture is
// shown: the last look's, or (an advanced feeder) each look's.
constexpr int kLooseLooks = 3, kLooseShownMs = 1000, kLooseAdvancedShownMs = 250;

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
        const JPPinFeeder* p = f ? f->pinFeeding() : nullptr;
        if (!p) return;
        lever = f->as<JPLeverFeeder>() != nullptr;
        actuator = p->actuatorName;
        peelOff = p->peelOffActuatorName;
        start = p->feedStart.convertToUnits(kMm);
        end = p->feedEnd.convertToUnits(kMm);
        location = f->location().convertToUnits(kMm);
        pitch = p->partPitch.convertToUnits(kMm).value();
        speed = p->feedSpeed;
        if (const JPDragFeeder* d = f->as<JPDragFeeder>()) backoff = d->backoffDistance.convertToUnits(kMm).value();
        vision = p->vision.enabled;
        templatePath = f->templatePath(config.directory());
        const JPTemplateVision::Area& a = p->vision.areaOfInterest;
        aoi = { a.x, a.y, a.width, a.height };
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
                if (JPFeeder* f = config.feeder(feederId))
                    if (JPPinFeeder* p = f->pinFeeding()) p->partPitch = JPLength(2, kMm);
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

bool JPFeederFeed::looseFeed(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                             std::string& why) {
    auto main = [&onMain](const std::function<void()>& fn) {
        if (onMain) onMain(fn);
        else fn();
    };
    std::optional<JPPipeline> held;
    JPLocation location(kMm);
    std::string name;
    bool advanced = false;
    main([&] {
        const JPFeeder* f = config.feeder(feederId);
        if (!f) return;
        held = JPFeederPipelines::of(*f);
        if (held) {
            held->context().configurationDirectory = config.directory();
            held->context().label = "feeder " + f->id();
            JPFeederPipelines::configureForEditing(config, *f, *held);
        }
        location = f->location().convertToUnits(kMm);
        name = f->name();
        advanced = f->typeName() == "AdvancedLoosePartFeeder";
    });
    if (!held) {
        why = "no feeder " + feederId;
        return false;
    }
    // The part found nearest the camera, from where the camera is: where it
    // is, at the feeder's Z, turned as OpenPnP turns it.
    auto nearest = [&](const JPLocation& from, int showMs, std::optional<JPLocation>& pick) {
        JPJobMachine::SeenRects seen;
        if (!machine.seeRects(from, *held, showMs, seen, why)) return false;
        pick.reset();
        double best = INFINITY;
        for (const JPJobMachine::SeenRects::Rect& r : seen.rects)
            if (const double d = std::hypot(r.x - from.x(), r.y - from.y()); d < best) {
                best = d;
                const double angle = advanced ? -(r.pixelAngle + location.rotation()) : r.pixelAngle + location.rotation();
                pick = JPLocation(kMm, r.x, r.y, location.z(), angle);
            }
        // An advanced feeder's pick within the camera's first view, else none (a pipeline drifting off).
        if (advanced && pick
            && (std::abs(location.x() - pick->x()) > seen.halfWidthMm || std::abs(location.y() - pick->y()) > seen.halfHeightMm))
            pick.reset();
        return true;
    };
    std::optional<JPLocation> pick;
    if (advanced) {
        // From its location, then from each part found (each look shown a moment).
        pick = location;
        for (int i = 0; i < kLooseLooks && pick; ++i)
            if (!nearest(*pick, kLooseAdvancedShownMs, pick)) return false;
        if (!pick) {
            why = "Feeder " + name + ": No parts found.";
            return false;
        }
    } else {
        JPLocation from = location;
        for (int i = 0; i < kLooseLooks; ++i) {
            if (!nearest(from, i == kLooseLooks - 1 ? kLooseShownMs : 0, pick)) return false;
            if (!pick) {
                why = "Feeder " + name + ": No parts found.";
                return false;
            }
            from = *pick;
        }
    }
    main([&] {
        if (JPFeeder* f = config.feeder(feederId)) f->foundPick = pick;
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
    std::optional<JJson> controls;
    bool tuneFirst = false;
    main([&] {
        JPFeeder* f = config.feeder(feederId);
        if (!f) {
            why = "no feeder " + feederId;
            return;
        }
        fed = f->feed(why, &empty);
        checks = f->takeVisionChecks();
        actuate = f->takeFeedActuation();
        if (const JPAutoFeeder* a = f->autoFeeding(); actuate && a) {
            actuatorName = a->actuatorName;
            actuatorValue = a->actuatorValue;
            moveFirst = a->moveBeforeFeed;
        }
        pickAt = f->pickLocation();
        controls = f->cameraTune;
        tuneFirst = f->autoTune();
    });
    if (!fed) return false;
    // Its vision at its own tune this run (none yet, its Auto-Tune? on: made on its first look), else the camera's
    // own settings: none carried over from another look.
    machine.useHeadTune(feederId, controls, tuneFirst && !controls);
    bool pinned = false;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId))
            pinned = f->typeName() == "ReferenceDragFeeder" || f->typeName() == "ReferenceLeverFeeder";
    });
    if (pinned) return pinFeed(config, feederId, machine, onMain, why);
    bool loose = false;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId))
            loose = f->typeName() == "ReferenceLoosePartFeeder" || f->typeName() == "AdvancedLoosePartFeeder";
    });
    if (loose) return looseFeed(config, feederId, machine, onMain, why);
    bool tape = false;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId)) tape = f->isVisionTape();
    });
    if (tape) return JPVisionTapeFeeder::feed(config, feederId, nozzleId, machine, onMain, why);
    bool blinds = false;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId)) blinds = f->typeName() == "BlindsFeeder";
    });
    if (blinds) return JPBlindsFeeder::feed(config, feederId, nozzleId, machine, onMain, why);
    bool heap = false;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId)) heap = f->typeName() == "ReferenceHeapFeeder";
    });
    if (heap) {
        // With the nozzle that will pick; else the head's first.
        std::string with = nozzleId;
        if (with.empty())
            if (const auto n = machine.nozzles(); !n.empty()) with = n.front().id;
        return JPHeapFeeder::feed(config, feederId, with, machine, onMain, why);
    }
    // A Schultz feeder: the nozzle over its pick place (at safe Z), its pre
    // pick actuator actuated with its feeder number.
    std::string schultz, schultzName;
    double feederNumber = 0;
    std::optional<JPLocation> schultzAt;
    bool isSchultz = false;
    main([&] {
        const JPFeeder* f = config.feeder(feederId);
        if (const JPSchultzFeeder* z = f ? f->schultzFeeding() : nullptr) {
            isSchultz = true;
            schultz = z->actuatorName;
            schultzName = f->name();
            feederNumber = z->actuatorValue;
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
        const JPNeoden4Feeder* n = f ? f->as<JPNeoden4Feeder>() : nullptr;
        if (!n) return;
        neoden = true;
        neodenActuator = n->actuatorName;
        neodenPitch = n->partPitchInTape.value();
        neodenVision = n->vision.enabled;
        neodenTemplate = f->templatePath(config.directory());
        neodenAt = f->location();
        const JPTemplateVision::Area& a = n->vision.areaOfInterest;
        neodenArea = { a.x, a.y, a.width, a.height, true };
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
            if (JPFeeder* f = config.feeder(feederId)) f->setFeedCount(f->feedCount() + 1);
        });
        return true;
    }
    // A Photon feeder: fed forward by its part pitch as its feed option says.
    bool photon = false;
    int photonPitch = 0;
    main([&] {
        JPFeeder* f = config.feeder(feederId);
        if (!f || !f->isPhoton()) return;
        photon = true;
        photonPitch = f->as<JPPhotonFeeder>()->partPitch;
        if (f->feedOptions() == JPFeeder::FeedOptions::SkipNext) {
            f->setFeedOptions(JPFeeder::FeedOptions::Normal);
            photonPitch = 0;
        } else if (f->feedOptions() == JPFeeder::FeedOptions::Disable) {
            photonPitch = 0;
        }
    });
    if (photon) return photonPitch == 0 || JPPhotonFeeders::feed(config, feederId, nozzleId, photonPitch, machine, onMain, why);
    // A Rapid feeder: its address and pitch to the RAPIDFEEDER actuator.
    std::string rapid;
    main([&] {
        const JPFeeder* f = config.feeder(feederId);
        if (const JPRapidFeeder* r = f ? f->as<JPRapidFeeder>() : nullptr) rapid = r->address + " " + std::to_string(r->pitch);
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
        std::optional<JPPipeline> pipeline;
        std::optional<JPFeeder> feeder;   // as it is, to set its pipeline up for the camera with
        std::vector<JPLocation> views;
        double diameter = 0, search = 0, parallaxDiameter = 0, parallaxAngle = 0;
        std::string name;
        main([&] {
            if (const JPFeeder* f = config.feeder(feederId)) {
                expected = f->visionExpected(n);
                feeder = *f;
                // At the tape's height there (the scale for it), looked at from a part beside it.
                if (expected) {
                    expected = expected->derive(std::nullopt, std::nullopt,
                                                JPLength(f->tapeZ(*expected), JPLengthUnit::Millimeters).convertToUnits(expected->units()).value(),
                                                std::nullopt);
                    views = { f->visionView(*expected) };
                }
                pipeline = JPFeederPipelines::of(*f);
                diameter = f->holeDiameter().convertToUnits(JPLengthUnit::Millimeters).value();
                search = f->holePitch().convertToUnits(JPLengthUnit::Millimeters).value() / 2;
                if (const JPStripFeeder* s = f->as<JPStripFeeder>()) {
                    parallaxDiameter = s->parallaxDiameter.convertToUnits(JPLengthUnit::Millimeters).value();
                    parallaxAngle = s->parallaxAngle;
                }
                name = f->name();
            }
        });
        if (!expected) continue;
        if (!pipeline) {
            why = name + " has no pipeline to find its holes with";
            return false;
        }
        // A parallax diameter set (OpenPnP's): from either side of the hole, that far apart and turned that way, instead.
        if (parallaxDiameter > 0) {
            const JPLocation e = expected->convertToUnits(JPLengthUnit::Millimeters);
            const double r = parallaxDiameter / 2, a = parallaxAngle * M_PI / 180;
            views = { JPLocation(JPLengthUnit::Millimeters, e.x() + r * std::cos(a), e.y() + r * std::sin(a), e.z(), 0),
                      JPLocation(JPLengthUnit::Millimeters, e.x() - r * std::cos(a), e.y() - r * std::sin(a), e.z(), 0) };
        }
        // Beside the part out of the camera's reach (a strip's end by the machine's edge): from over the hole, as
        // OpenPnP looks, when the camera reaches that.
        else if (!views.empty() && !machine.cameraReaches(views.front()) && machine.cameraReaches(*expected)) {
            JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << name << ": hole " << n << " looked at from over it: beside its part is out of reach";
            views = { *expected };
        }
        // The search round the picture's middle: as far as the farthest view is from the hole, and half a pitch more.
        double farthest = 0;
        const JPLocation hole = expected->convertToUnits(JPLengthUnit::Millimeters);
        for (const JPLocation& v : views) {
            const JPLocation m = v.convertToUnits(JPLengthUnit::Millimeters);
            farthest = std::max(farthest, std::hypot(m.x() - hole.x(), m.y() - hole.y()));
        }
        // Set up as Auto Setup sets it, once it has the camera (its Hough stages' sizes in pixels); then its holes' size,
        // and the search round the picture's middle.
        const auto configure = [&config, &feeder, diameter, reach = farthest + search](JPPipeline& p) {
            JPFeederPipelines::configureForEditing(config, *feeder, p);
            p.setProperty("sprocketHole.diameter", JPPipelineValue { JPPipelineValue::LengthMm { diameter } });
            p.setProperty("sprocketHole.maxDistance", JPPipelineValue { JPPipelineValue::LengthMm { reach } });
        };
        JPLocation found(JPLengthUnit::Millimeters);
        std::string seen;
        const bool ok = machine.locateHole(*expected, *pipeline, search, views, configure, found, seen);
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
        const bool bamboo = f->typeName() == "BambooFeederAutoVision";
        if (const JPAutoFeeder* a = f->autoFeeding()) {
            name = a->postPickActuatorName;
            value = a->postPickActuatorValue;
        } else if (bamboo) {
            name = f->text("post-pick-actuator-name");
            value = f->real("post-pick-actuator-value", 0);
            if (name.empty())
                JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "Post pick cancelled. Actuator not set for feeder " << f->name();
        } else if (const JPSchultzFeeder* z = f->schultzFeeding()) {
            // With its feeder number.
            name = z->postPickActuatorName;
            value = z->actuatorValue;
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
