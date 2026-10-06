// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

// OpenPnP's bottom vision tests' machine: its default machine's simulated camera looking up ("Bottom", 640 x 480 at
// 0.0134375 mm a pixel), a part on the nozzle drawn as its SimulatedUpCamera draws it (off by its Pick Error
// Offsets), found by the part's bottom vision pipeline, in one look or in the shots of its vision compositing, and its
// offsets worked out as ReferenceBottomVision.findOffsets does. JP_BV_TRACE set: each look and shot said;
// JP_BV_SAVE a folder: each shot's picture and working image kept there.

#include "camera/JPSimulatedSource.h"
#include "camera/JPSimulatedUpCamera.h"
#include "machine/JPCameraCalibration.h"
#include "machine/JPCameraConfig.h"
#include "machine/JPNozzleTipConfig.h"
#include "machine/JPVisionConfig.h"
#include "model/JPConfiguration.h"
#include "setup/JPVisionPipelines.h"
#include "pipeline/JPStageUtil.h"
#include "tasks/JPAlignRequests.h"
#include "tasks/JPBottomVision.h"
#include "tasks/JPVisionComposite.h"
#include "tasks/JPVisionPipelinePrep.h"

#include <opencv2/imgproc.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

struct BottomVisionBench {
    static constexpr double kUpp = 0.0134375, kCamX = 118.799, kCamY = 7.117, kCamZ = 0;
    static constexpr int    kWidth = 640, kHeight = 480;
    static constexpr double kFps = 1000;
    static constexpr int    kGrabMs = 1000;

    jf::JPConfiguration           config;
    jf::JPVisionConfig            vision;
    jf::JPNozzleTipConfig         tip;
    jf::JPCameraConfig            camera;   // its roaming radius, for vision compositing
    jf::JPSimulatedUpCamera::Settings up;
    jf::JPCameraCalibration       cal;

    explicit BottomVisionBench(const std::string& dir) : config(dir) {
        std::vector<std::string> problems;
        std::string error;
        const bool loaded = config.load(problems, error);
        assert(loaded);
        jf::JPVisionPipelines::ensureStock(config);
        // OpenPnP's default ReferenceBottomVision: enabled, not pre-rotating, by its pipeline.
        vision.preRotate = false;
        vision.bottomPipeline = true;
        tip.name = "NT1";
        camera.name = "Bottom";
        up.width = kWidth;
        up.height = kHeight;
        up.uppX = up.uppY = kUpp;
        up.location = jf::JPMachineLocation { kCamX, kCamY, kCamZ, 0 };
        // The camera as calibrated: its picture's scale, turn and mirroring (the simulation's own).
        const jf::JJson scene = up.scene();
        cal.valid = true;
        for (size_t i = 0; i < 4; ++i) cal.pxPerMm[i] = scene["pxPerMm"][i].number();
        cal.width = kWidth;
        cal.height = kHeight;
        cal.lensCentreX = kWidth / 2.0;
        cal.lensCentreY = kHeight / 2.0;
    }

    jf::JPVisionSettings& settingsOf(const std::string& partId) {
        const jf::JPVisionSettings* v =
            config.inheritedVision(*config.part(partId), jf::JPVisionSettings::Kind::Bottom, vision.bottomVisionId);
        assert(v);
        return *config.visionSettings(v->id);
    }

    // ReferenceBottomVision.findOffsets for the part on the nozzle, placed at `placementAngle`.
    bool findOffsets(const std::string& partId, double placementAngle, jf::JPBottomVision::Offset& offset, std::string& why) {
        using namespace jf;
        const JPPart& part = *config.part(partId);
        const double heightMm = part.height.convertToUnits(JPLengthUnit::Millimeters).value();
        JPJobMachine::AlignRequest rq;
        const bool aligned = JPAlignRequests::forPart(config, vision, part, heightMm, placementAngle, rq);
        assert(aligned);
        rq.offsets.maxPickToleranceMm = tip.maxPickToleranceMm;
        rq.offsets.tipName = tip.name;
        JPPipeline& pipeline = *rq.pipeline;
        JPPipeline::Context& ctx = pipeline.context();
        ctx.pixelsPerMmX = ctx.pixelsPerMmY = 1 / kUpp;
        ctx.pictureMirrored = cal.pictureMirrored();
        ctx.pictureTurnDeg = cal.pictureTurnDeg();
        ctx.cameraWidth = kWidth;
        ctx.cameraHeight = kHeight;
        // The part on the nozzle, as its footprint has it.
        const JPFootprint& f = config.package(part.packageId)->footprint;
        const double mm = JPLength(1, f.units).convertToUnits(JPLengthUnit::Millimeters).value();
        auto inMm = [mm](const JPFootprint::Outline& o) {
            JPSimulatedUpCamera::Polygon out;
            for (const JPFootprint::Point& p : o) out.push_back({ p.x * mm, p.y * mm });
            return out;
        };
        JPSimulatedUpCamera::Part held { inMm(f.bodyOutline()), {}, heightMm };
        for (const JPFootprint::Outline& o : f.padsOutlines()) held.pads.push_back(inMm(o));
        // The camera's picture with the nozzle where it is (at the camera's height for the part: its underside in focus).
        JPSimulatedUpCamera::Nozzle nozzle;
        nozzle.tipDiameter = 1;
        nozzle.x = kCamX;
        nozzle.y = kCamY;
        nozzle.tipZ = kCamZ + heightMm;
        JPSimulatedSource source("Bottom", kWidth, kHeight, kFps, up.scene(), [](double& x, double& y) {
            x = kCamX;
            y = kCamY;
            return true;
        }, 0, 0, [&] {
            JPSimulatedSource::Extras e;
            JPSimulatedUpCamera::drawNozzle(&up, kCamX, kCamY, kCamZ, nozzle, &held, e);
            return e;
        });
        std::string error;
        const bool started = source.open(error) && source.start(source.modes().front(), error);
        assert(started);
        ctx.capture = [&](const std::string&, const std::string&, cv::Mat& bgr, std::string& w) {
            JPFrame frame;
            if (!source.grab(frame, kGrabMs, w)) return false;
            cv::Mat rgba(frame.height, frame.width, CV_8UC4, frame.rgba.data());
            cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
            return true;
        };
        ctx.locationToPixel = [this](double x, double y, double& px, double& py) { return cal.pixelFor(x, y, kCamX, kCamY, px, py); };
        std::shared_ptr<JPVisionComposite> composite;
        if (!JPVisionPipelinePrep::bottom(pipeline, config, *config.visionSettings(rq.settingsId), partId, "", rq.imageAngle, &tip,
                                          &camera, why, &composite))
            return false;
        const bool composited = composite && JPVisionComposite::isAdvanced(composite->solution());
        auto put = [&](double x, double y, double r) {
            nozzle.x = x;
            nozzle.y = y;
            nozzle.angle = r;
        };
        const JPBottomVision::Look look = [&](const JPLocation& at, double expected, int, JPBottomVision::Seen& seen, std::string& w) {
            put(at.x(), at.y(), at.rotation());
            if (composited) {
                const JPBottomVision::Composite k {
                    pipeline, *composite, &tip, cal, kCamX, kCamY, partId,
                    [&](double& x, double& y) {
                        x = nozzle.x;
                        y = nozzle.y;
                        return true;
                    },
                    [&](double x, double y, std::string&) {
                        put(x, y, at.rotation());
                        return true;
                    },
                    [&](JPPipeline& p) {
                        if (!std::getenv("JP_BV_TRACE")) return;
                        const JPPipeline::Result* r = p.result("results");
                        const auto* rect = r ? std::get_if<cv::RotatedRect>(&r->model.value) : nullptr;
                        if (!rect) return;
                        cv::Point2f pts[4];
                        rect->points(pts);
                        std::fprintf(stderr, "    shot (nozzle %.3f,%.3f): rect %.1f,%.1f %.1fx%.1f %.2f deg; corners mm:", nozzle.x - kCamX,
                                     nozzle.y - kCamY, rect->center.x, rect->center.y, rect->size.width, rect->size.height, rect->angle);
                        for (const auto& q : pts) {
                            double mx = 0, my = 0;
                            cal.machinePoint(q.x, q.y, kCamX, kCamY, mx, my);
                            std::fprintf(stderr, " (%.3f,%.3f)", mx - nozzle.x, my - nozzle.y);
                        }
                        std::fprintf(stderr, "\n");
                        if (const char* d = std::getenv("JP_BV_SAVE")) {
                            static int n = 0;
                            JPStageUtil::writePicture(std::string(d) + "/shot" + std::to_string(n) + "-working.png", p.workingImage());
                            cv::Mat bgr;
                            std::string w;
                            if (p.context().capture("Skip", "", bgr, w)) JPStageUtil::writePicture(std::string(d) + "/shot" + std::to_string(n) + "-camera.png", bgr);
                            ++n;
                        }
                    } };
                const bool ok = JPBottomVision::seeComposite(k, at.x(), at.y(), expected, seen, w);
                if (std::getenv("JP_BV_TRACE"))
                    std::fprintf(stderr, "  look at %.3f,%.3f,%.3f: seen %.4f,%.4f angle %.4f size %.3fx%.3f\n", at.x() - kCamX, at.y() - kCamY,
                                 at.rotation(), seen.x - kCamX, seen.y - kCamY, seen.angle, seen.widthMm, seen.heightMm);
                return ok;
            }
            return JPBottomVision::findByPipeline(pipeline, partId, cal, kCamX, kCamY, at.x(), at.y(), expected,
                                                  rq.offsets.fullRotation ? 180 : JPBottomVision::kAdjustRange, seen, w);
        };
        return JPBottomVision::findOffsets(rq.offsets, kCamX, kCamY, look, offset, why);
    }
};
