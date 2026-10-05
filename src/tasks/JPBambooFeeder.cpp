// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBambooFeeder.h"

#include "JPFeederPipelines.h"
#include "JPFeederVision.h"

#include "common/JPlacerLog.h"
#include "model/JPFeederTape.h"

#include <j/core/Log.h>

#include <opencv2/imgproc.hpp>

#include <cmath>

inline namespace jf {

namespace {

constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
// How long what vision found is shown on the camera (OpenPnP's showResultMilliseconds).
constexpr int kShowResultMs = 2000;
// Holes closer than this are taken as not set.
constexpr double kHolesLeastApartMm = 3;
// OpenPnP's defaults, kept in its machine.xml only: how much the feeder may have been moved, the holes' tolerance
// (size, position), the passes calibrating the holes, how close the camera must come to stop.
constexpr double kCalibrationToleranceMm = 1.95, kSprocketHoleToleranceMm = 0.6, kCalibrateToleranceMm = 0.3;
constexpr int    kCalibrateMaxPasses = 3;

using Main = std::function<void(const std::function<void()>&)>;

Main mainOf(const JPBambooFeeder::OnMain& onMain) {
    return [&onMain](const std::function<void()>& fn) {
        if (onMain) onMain(fn);
        else fn();
    };
}

std::string trigger(const JPFeeder& f) { return f.text("calibration-trigger", "UntilConfident"); }

JPFeederVision::Settings settingsOf(const JPFeeder& f) {
    JPFeederVision::Settings s;
    s.tape = JPFeederTape::of(f);
    s.tape.feedMultiplier = 1;   // fixed for these feeders
    s.normalizePickLocation = f.flag("normalize-pick-location", true);
    s.snapToAxis = f.flag("snap-to-axis", false);
    s.calibrationToleranceMm = f.real("calibration-tolerance-mm", kCalibrationToleranceMm);
    s.sprocketHoleToleranceMm = f.real("sprocket-hole-tolerance-mm", kSprocketHoleToleranceMm);
    return s;
}

// The feeder's pipeline, given the sprocket holes' size and search range (Auto Setup: the whole picture).
bool pipelineFor(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const Main& main, bool autoSetup,
                 std::optional<JPPipeline>& pipeline, std::string& why) {
    JPJobMachine::Sight sight;
    if (!machine.cameraSight(sight, why)) return false;
    main([&] {
        const JPFeeder* f = config.feeder(feederId);
        if (!f) return;
        pipeline = JPFeederPipelines::of(*f);
        if (!pipeline) return;
        pipeline->context().configurationDirectory = config.directory();
        JPFeederPipelines::configureTape(*f, *pipeline, autoSetup, sight.width, sight.height, sight.mmPerPixelX, sight.mmPerPixelY);
    });
    if (!pipeline) {
        why = "no feeder " + feederId;
        return false;
    }
    return true;
}

// OpenPnP's ensureCameraZ: a camera whose scale depends on Z needs the pick location's Z.
bool cameraZ(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const Main& main, std::string& why) {
    JPJobMachine::Sight sight;
    if (!machine.cameraSight(sight, why)) return false;
    bool unset = false;
    std::string name;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId)) {
            unset = f->location().z() == 0;
            name = f->name();
        }
    });
    if (sight.twoHeights && unset) {
        why = "Feeder " + name + ": Please set the Pick Location Z coordinate first, it is required to determine the true scale "
                                 "of the camera view for accurate computer vision.";
        return false;
    }
    return true;
}

// The camera over `at`, the pipeline run, the features found as `mode` asks, and shown.
bool look(JPJobMachine& machine, JPPipeline& pipeline, const JPLocation& at, JPFeederVision::Mode mode,
          const JPFeederVision::Settings& settings, JPFeederVision::Found& found, std::string& why) {
    JPJobMachine::Sight sight;
    if (!machine.lookThrough(at, pipeline, sight, why)) return false;
    try {
        if (!JPFeederVision::find(pipeline.expectedResult("results").model, mode, settings, sight, found, why)) return false;
    } catch (const std::runtime_error& e) {
        why = e.what();
        return false;
    }
    // Text read by an enabled OCR stage.
    if (const JPPipeline::Result* r = pipeline.result("OCR"))
        if (const JPPipelineStage* s = pipeline.stage("OCR"); s && s->enabled())
            if (const auto* ocr = std::get_if<JPPipelineModel::Ocr>(&r->model.value)) found.ocrText = ocr->text;
    cv::Mat shown = pipeline.workingImage().clone();
    if (shown.channels() == 1) cv::cvtColor(shown, shown, cv::COLOR_GRAY2BGR);
    JPFeederVision::draw(shown, found, settings, sight);
    machine.showOnCamera(shown, kShowResultMs);
    return true;
}

// OpenPnP's performVisionOperations: the holes looked at from their middle,
// up to the passes allowed, until the farthest pick moves less than the
// tolerance; what each pass found kept as asked.
bool visionOperations(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const Main& main,
                      JPPipeline& pipeline, bool storeHoles, bool storePickLocation, bool storeVisionOffset, std::string& why) {
    JPLocation hole1(kMm), hole2(kMm), pick(kMm), runningOffset(kMm);
    double rotationInFeeder = 0, toleranceMm = kCalibrateToleranceMm;
    int passes = kCalibrateMaxPasses;
    bool found = false;
    main([&] {
        const JPFeeder* f = config.feeder(feederId);
        if (!f) return;
        found = true;
        hole1 = f->locationOf("hole-1-location");
        hole2 = f->locationOf("hole-2-location");
        pick = f->location();
        runningOffset = trigger(*f) != "None" && f->visionOffset ? *f->visionOffset : JPLocation::origin();
        rotationInFeeder = f->real("rotation-in-feeder", 0);
        passes = f->number("calibrate-max-passes", kCalibrateMaxPasses);
        toleranceMm = f->real("calibrate-tolerance-mm", kCalibrateToleranceMm);
    });
    if (!found) {
        why = "no feeder " + feederId;
        return false;
    }
    if (!cameraZ(config, feederId, machine, main, why)) return false;
    for (int i = 0; i < passes; ++i) {
        const JPLocation mid = hole1.add(hole2).multiply(0.5, 0.5, 0, 0).derive(std::nullopt, std::nullopt, std::nullopt,
                                                                                pick.rotation() + rotationInFeeder);
        JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "calibrating sprocket holes pass " << i << " midPoint is " << mid.text();
        JPFeederVision::Settings settings;
        main([&] {
            if (const JPFeeder* f = config.feeder(feederId)) settings = settingsOf(*f);
        });
        JPFeederVision::Found feature;
        if (!look(machine, pipeline, mid, JPFeederVision::Mode::CalibrateHoles, settings, feature, why)) return false;
        hole1 = *feature.hole1;
        hole2 = *feature.hole2;
        pick = *feature.pick;
        // The worst pick location error this gives: part 1 of the cycle, the farthest.
        const JPLocation uncalibrated = JPFeederTape::partLocation(1, runningOffset, settings.tape, rotationInFeeder);
        const JPLocation calibrated = JPFeederTape::partLocation(1, feature.visionOffset, settings.tape, rotationInFeeder);
        const double errorMm = calibrated.convertToUnits(kMm).linearDistanceTo(uncalibrated);
        JLOGC(JPlacerLog::kJob, JLogLevel::Trace) << "new vision offset " << feature.visionOffset->text() << " vs. previous vision offset "
                                                  << runningOffset.text() << " results in error " << errorMm
                                                  << "mm at the (farthest) pick location";
        main([&] {
            JPFeeder* f = config.feeder(feederId);
            if (!f) return;
            if (storeHoles) {
                f->setLocationOf("hole-1-location", hole1);
                f->setLocationOf("hole-2-location", hole2);
            }
            if (storePickLocation) f->setLocation(pick);
            if (storeVisionOffset) {
                // Only after an earlier calibration: the person's moving the feeder is no error of it.
                if (f->visionOffset) JPFeederTape::addCalibrationError(*f, JPLength(errorMm, kMm));
                f->visionOffset = feature.visionOffset;
            }
        });
        if (errorMm < toleranceMm) break;
        runningOffset = *feature.visionOffset;
    }
    return true;
}

bool feedTape(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const Main& main, std::string& why) {
    std::string actuator, name;
    double value = 0;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId)) {
            actuator = f->text("feed-actuator-name");
            value = f->real("feed-actuator-value", 0);
            name = f->name();
        }
    });
    if (actuator.empty()) {
        why = "No actuatorName specified for feeder " + name;
        return false;
    }
    if (machine.actuate(actuator, value, why)) return true;
    why = "Feed failed. " + why;
    return false;
}

} // namespace

bool JPBambooFeeder::calibrate(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                               std::string& why) {
    const Main main = mainOf(onMain);
    std::optional<JPPipeline> pipeline;
    if (!pipelineFor(config, feederId, machine, main, false, pipeline, why)) return false;
    return visionOperations(config, feederId, machine, main, *pipeline, false, false, true, why);
}

bool JPBambooFeeder::assertCalibrated(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine,
                                      const OnMain& onMain, bool tapeFeed, std::string& why) {
    const Main main = mainOf(onMain);
    bool tooClose = false, needed = false;
    std::string name;
    main([&] {
        const JPFeeder* f = config.feeder(feederId);
        if (!f) return;
        name = f->name();
        tooClose = f->locationOf("hole-1-location").convertToUnits(kMm).linearDistanceTo(f->locationOf("hole-2-location"))
                   < kHolesLeastApartMm;
        const std::string t = trigger(*f);
        needed = (!f->visionOffset && t != "None") || (tapeFeed && t == "UntilConfident" && !JPFeederTape::precisionSufficient(*f))
                 || (tapeFeed && t == "OnEachTapeFeed");
    });
    if (tooClose) {
        why = "Feeder " + name + " sprocket hole locations undefined/too close together.";
        return false;
    }
    if (!needed) return true;
    if (!calibrate(config, feederId, machine, onMain, why)) return false;
    bool locked = false;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId)) locked = f->visionOffset.has_value();
    });
    if (!locked) {
        why = "Vision failed on feeder " + name + ".";
        return false;
    }
    return true;
}

bool JPBambooFeeder::feed(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId, JPJobMachine& machine,
                          const OnMain& onMain, std::string& why) {
    const Main main = mainOf(onMain);
    bool moveFirst = false;
    std::optional<JPLocation> pickAt;
    JPFeeder::FeedOptions options = JPFeeder::FeedOptions::Normal;
    long count = 0, cycle = 1, feedsPerPart = 1;
    main([&] {
        const JPFeeder* f = config.feeder(feederId);
        if (!f) return;
        moveFirst = f->flag("move-before-feed", false);
        pickAt = f->pickLocation();
        options = f->feedOptions();
        count = f->number("feed-count", 0);
        JPFeederTape::Params tape = JPFeederTape::of(*f);
        tape.feedMultiplier = 1;
        cycle = JPFeederTape::partsPerFeedCycle(tape);
        const double part = tape.partPitch.convertToUnits(kMm).value(), feedPitch = tape.feedPitch.convertToUnits(kMm).value();
        feedsPerPart = feedPitch > 0 ? long(std::ceil(part / feedPitch)) : 1;
    });
    if (moveFirst && pickAt && !nozzleId.empty() && !machine.positionNozzle(nozzleId, *pickAt, why)) return false;
    if (options == JPFeeder::FeedOptions::Normal) {
        if (count % cycle == 0) {
            // No part left of the last feed: a feed.
            if (!assertCalibrated(config, feederId, machine, onMain, false, why)) return false;
            for (long i = 0; i < feedsPerPart; ++i)
                if (!feedTape(config, feederId, machine, main, why)) return false;
            if (!machine.safeZ(why)) return false;
            if (!assertCalibrated(config, feederId, machine, onMain, true, why)) return false;
        } else {
            JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "Multi parts feed: skipping tape feed at feed count " << count;
        }
    } else {
        if (!assertCalibrated(config, feederId, machine, onMain, false, why)) return false;
        if (!machine.safeZ(why)) return false;
    }
    main([&] {
        JPFeeder* f = config.feeder(feederId);
        if (!f) return;
        if (options == JPFeeder::FeedOptions::Normal) f->setNumber("feed-count", f->number("feed-count", 0) + 1);
        if (options == JPFeeder::FeedOptions::SkipNext) f->setFeedOptions(JPFeeder::FeedOptions::Normal);
    });
    return true;
}

bool JPBambooFeeder::autoSetup(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                               std::string& why) {
    const Main main = mainOf(onMain);
    main([&] {
        // Just assume it is wanted now.
        if (JPFeeder* f = config.feeder(feederId); f && trigger(*f) == "None") f->setText("calibration-trigger", "UntilConfident");
    });
    if (!cameraZ(config, feederId, machine, main, why)) return false;
    const std::optional<JPLocation> cameraAt = machine.cameraLocation();
    if (!cameraAt) {
        why = "no camera on the head";
        return false;
    }
    std::optional<JPPipeline> pipeline;
    if (!pipelineFor(config, feederId, machine, main, true, pipeline, why)) return false;
    JPFeederVision::Settings settings;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId)) settings = settingsOf(*f);
    });
    JPFeederVision::Found feature;
    if (!look(machine, *pipeline, *cameraAt, JPFeederVision::Mode::FromPickLocationGetHoles, settings, feature, why)) return false;
    // The first vision based results; the statistics reset, as all this changed.
    main([&] {
        JPFeeder* f = config.feeder(feederId);
        if (!f) return;
        f->setLocation(*feature.pick);
        f->setLocationOf("hole-1-location", *feature.hole1);
        f->setLocationOf("hole-2-location", *feature.hole2);
        JPFeederTape::resetCalibrationStatistics(*f);
    });
    // Then the holes calibrated, and the camera back over the pick location, failed or not.
    const bool ok = visionOperations(config, feederId, machine, main, *pipeline, true, true, false, why);
    std::optional<JPLocation> back;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId)) back = f->location();
    });
    std::string moved;
    if (back && !machine.positionCamera(*back, moved) && ok) {
        why = moved;
        return false;
    }
    return ok;
}

bool JPBambooFeeder::showFeatures(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine,
                                  const OnMain& onMain, std::string& why) {
    const Main main = mainOf(onMain);
    if (!cameraZ(config, feederId, machine, main, why)) return false;
    std::optional<JPPipeline> pipeline;
    if (!pipelineFor(config, feederId, machine, main, true, pipeline, why)) return false;
    JPFeederVision::Settings settings;
    JPLocation at(kMm);
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId)) {
            settings = settingsOf(*f);
            at = f->location();
        }
    });
    JPFeederVision::Found feature;
    return look(machine, *pipeline, at, JPFeederVision::Mode::Preview, settings, feature, why);
}

std::optional<JPLocation> JPBambooFeeder::jobPreparationLocation(const JPFeeder& feeder) {
    if (feeder.visionOffset || trigger(feeder) == "None") return std::nullopt;
    JPFeederTape::Params tape = JPFeederTape::of(feeder);
    tape.feedMultiplier = 1;
    return JPFeederTape::partLocation(0, std::nullopt, tape, feeder.real("rotation-in-feeder", 0));
}

bool JPBambooFeeder::prepareForJob(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine,
                                   const OnMain& onMain, std::string& why) {
    bool calibrated = true, withTrigger = false;
    mainOf(onMain)([&] {
        if (const JPFeeder* f = config.feeder(feederId)) {
            calibrated = f->visionOffset.has_value();
            withTrigger = trigger(*f) != "None";
        }
    });
    if (calibrated) return true;
    if (withTrigger) return calibrate(config, feederId, machine, onMain, why);
    return assertCalibrated(config, feederId, machine, onMain, false, why);
}

} // inline namespace jf
