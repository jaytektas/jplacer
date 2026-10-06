// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's ReferenceBottomVisionOffsetTest and ReferenceBottomVisionTest: OpenPnP's default machine's simulated camera looking up, a part on
// the nozzle drawn as its SimulatedUpCamera draws it (off by its Pick Error Offsets), found by the stock bottom
// vision pipeline, its offsets worked out as ReferenceBottomVision.findOffsets does: a symmetric R0805 and an
// asymmetric part (its pads' middle 0.5 mm off its origin), with and without a Vision Offset, pre-rotated or not,
// placed at 0, 45, 90 and 180 degrees, picked true and picked off; within 0.1 mm and 0.07 degrees of what OpenPnP
// expects; and OpenPnP's default configuration's R0805 picked 0.25, 0.75 mm off and turned 13 degrees either way,
// found so within 0.1 mm and 0.03 degrees. And the checks: the nozzle tip's Max. Pick Tolerance and the Part size
// check.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "camera/JPSimulatedSource.h"
#include "camera/JPSimulatedUpCamera.h"
#include "machine/JPCameraCalibration.h"
#include "machine/JPNozzleTipConfig.h"
#include "machine/JPVisionConfig.h"
#include "model/JPConfiguration.h"
#include "setup/JPVisionPipelines.h"
#include "tasks/JPAlignRequests.h"
#include "tasks/JPBottomVision.h"
#include "tasks/JPVisionPipelinePrep.h"

using namespace jf;
namespace fs = std::filesystem;

namespace {

// OpenPnP's default machine: the camera looking up ("Bottom", 640 x 480 at 0.0134375 mm a pixel) and its place.
constexpr double kUpp = 0.0134375, kCamX = 118.799, kCamY = 7.117, kCamZ = 0;
constexpr int    kWidth = 640, kHeight = 480;
// What OpenPnP's test allows, and sets the nozzle tip's Max. Pick Tolerance to (for the large offsets).
constexpr double kMaxErrorMm = 0.1, kMaxErrorDeg = 0.07, kPickToleranceMm = 2;

struct Bench {
    JPConfiguration    config;
    JPVisionConfig     vision;
    JPNozzleTipConfig  tip;
    JPSimulatedUpCamera::Settings up;
    JPCameraCalibration cal;

    explicit Bench(const std::string& dir) : config(dir) {
        std::vector<std::string> problems;
        std::string error;
        assert(config.load(problems, error));
        JPVisionPipelines::ensureStock(config);
        // OpenPnP's default ReferenceBottomVision: enabled, not pre-rotating, by its pipeline.
        vision.preRotate = false;
        vision.bottomPipeline = true;
        tip.name = "NT1";
        tip.maxPickToleranceMm = kPickToleranceMm;
        up.width = kWidth;
        up.height = kHeight;
        up.uppX = up.uppY = kUpp;
        up.location = JPMachineLocation { kCamX, kCamY, kCamZ, 0 };
        // The camera as calibrated: its picture's scale, turn and mirroring (the simulation's own).
        const JJson scene = up.scene();
        cal.valid = true;
        for (size_t i = 0; i < 4; ++i) cal.pxPerMm[i] = scene["pxPerMm"][i].number();
        cal.width = kWidth;
        cal.height = kHeight;
        cal.lensCentreX = kWidth / 2.0;
        cal.lensCentreY = kHeight / 2.0;
    }

    JPVisionSettings& settingsOf(const std::string& partId) {
        const JPVisionSettings* v = config.inheritedVision(*config.part(partId), JPVisionSettings::Kind::Bottom, vision.bottomVisionId);
        assert(v);
        return *config.visionSettings(v->id);
    }

    // ReferenceBottomVision.findOffsets for the part on the nozzle, placed at `placementAngle`.
    bool findOffsets(const std::string& partId, double placementAngle, JPBottomVision::Offset& offset, std::string& why) {
        const JPPart& part = *config.part(partId);
        const double heightMm = part.height.convertToUnits(JPLengthUnit::Millimeters).value();
        JPJobMachine::AlignRequest rq;
        assert(JPAlignRequests::forPart(config, vision, part, heightMm, placementAngle, rq));
        rq.offsets.maxPickToleranceMm = tip.maxPickToleranceMm;
        rq.offsets.tipName = tip.name;
        JPPipeline& pipeline = *rq.pipeline;
        JPPipeline::Context& ctx = pipeline.context();
        ctx.pixelsPerMmX = ctx.pixelsPerMmY = 1 / kUpp;
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
        // The camera's picture with the nozzle where it is.
        JPSimulatedUpCamera::Nozzle nozzle;
        nozzle.tipDiameter = 1;
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
        assert(source.open(error) && source.start(source.modes().front(), error));
        ctx.capture = [&](const std::string&, const std::string&, cv::Mat& bgr, std::string& w) {
            JPFrame frame;
            if (!source.grab(frame, kGrabMs, w)) return false;
            cv::Mat rgba(frame.height, frame.width, CV_8UC4, frame.rgba.data());
            cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
            return true;
        };
        if (!JPVisionPipelinePrep::bottom(pipeline, config, *config.visionSettings(rq.settingsId), partId, "", rq.imageAngle, &tip,
                                          nullptr, why))
            return false;
        const JPBottomVision::Look look = [&](const JPLocation& at, double expected, int, JPBottomVision::Seen& seen, std::string& w) {
            // The nozzle there, at the camera's height for the part (its underside in focus).
            nozzle.x = at.x();
            nozzle.y = at.y();
            nozzle.angle = at.rotation();
            nozzle.tipZ = kCamZ + heightMm;
            return JPBottomVision::findByPipeline(pipeline, partId, cal, kCamX, kCamY, at.x(), at.y(), expected,
                                                  rq.offsets.fullRotation ? 180 : JPBottomVision::kAdjustRange, seen, w);
        };
        return JPBottomVision::findOffsets(rq.offsets, kCamX, kCamY, look, offset, why);
    }

    static constexpr double kFps = 1000;
    static constexpr int    kGrabMs = 1000;
};

// One of OpenPnP's tests: the part, its settings, and the offsets expected for each placement angle.
struct Case {
    const char* name;
    const char* partId;
    double      visionOffsetX, visionOffsetY;
    const char* preRotateUsage;
    bool        noCompositing;
    JPMachineLocation error;   // the camera's Pick Error Offsets
    // The offsets expected at 0, 45, 90 and 180 degrees.
    std::vector<JPLocation> expected;
};

JPLocation mm(double x, double y, double r = 0) { return JPLocation(JPLengthUnit::Millimeters, x, y, 0, r); }

void run(const std::string& dir, const Case& c) {
    Bench bench(dir);
    JPVisionSettings& settings = bench.settingsOf(c.partId);
    settings.setLocationOf("vision-offset", mm(c.visionOffsetX, c.visionOffsetY));
    settings.setText("pre-rotate-usage", c.preRotateUsage);
    if (c.noCompositing) {
        JPPackage& pkg = *bench.config.package(bench.config.part(c.partId)->packageId);
        JPVisionCompositing vc = pkg.visionCompositing.value_or(JPVisionCompositing {});
        vc.compositingMethod = JPVisionCompositing::Method::None;
        pkg.visionCompositing = vc;
    }
    bench.up.errorOffsets = c.error;
    const double angles[] = { 0, 45, 90, 180 };
    for (size_t i = 0; i < 4; ++i) {
        JPBottomVision::Offset offset;
        std::string why;
        const bool found = bench.findOffsets(c.partId, angles[i], offset, why);
        const JPLocation& o = offset.location;
        const JPLocation& e = c.expected[i];
        std::fprintf(stderr, "%s at %g: %s%s offsets %.3f, %.3f, %.3f (expected %.3f, %.3f, %.3f)\n", c.name, angles[i],
                     found ? "" : "FAILED ", why.c_str(), o.x(), o.y(), o.rotation(), e.x(), e.y(), e.rotation());
        assert(found);
        assert(std::abs(o.x() - e.x()) <= kMaxErrorMm && std::abs(o.y() - e.y()) <= kMaxErrorMm);
        assert(std::abs(o.rotation() - e.rotation()) <= kMaxErrorDeg);
    }
}

} // namespace

int main() {
    // OpenPnP's test configuration (its parts and packages) in a configuration of its own.
    const fs::path dir = fs::temp_directory_path() / ("jplacer-bottom-vision-offset-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    for (const char* f : { "packages.xml", "parts.xml" })
        fs::copy_file(fs::path(JPLACER_TESTDATA_DIR) / "openpnp/bottom-vision-offset" / f, dir / f, fs::copy_options::overwrite_existing);

    const JPMachineLocation none {};
    const std::vector<JPLocation> zero(4, mm(0, 0));
    const double r2 = std::sqrt(2.0);
    std::vector<Case> cases {
        { "testSymetricPartNoOffsetNoPreRotation", "R0805-1K", 0, 0, "AlwaysOff", false, none, zero },
        { "testSymetricPartNoOffsetWithPreRotation", "R0805-1K", 0, 0, "AlwaysOn", false, none, zero },
        { "testSymetricPartWithOffsetWithPreRotation", "R0805-1K", 1, 1, "AlwaysOn", false, none,
          { mm(-1, -1), mm(0, -r2), mm(1, -1), mm(1, 1) } },
        { "testAsymetricPartNoOffsetNoPreRotation", "DoubleAsymPart", 0, 0, "AlwaysOff", true, none, std::vector<JPLocation>(4, mm(0.5, 0.5)) },
        { "testAsymetricPartNoOffsetWithPreRotation", "DoubleAsymPart", 0, 0, "AlwaysOn", true, none,
          { mm(0.5, 0.5), mm(0.5, 0.5).rotateXy(45), mm(0.5, 0.5).rotateXy(90), mm(0.5, 0.5).rotateXy(180) } },
        { "testAsymetricPartWithOffsetNoPreRotation", "DoubleAsymPart", 0.5, 0.5, "AlwaysOff", false, none, zero },
        { "testAsymetricPartWithOffsetWithPreRotation", "DoubleAsymPart", 0.5, 0.5, "AlwaysOn", false, none, zero },
    };
    // Picked off: the camera's Pick Error Offsets, found again as the offsets.
    const JPLocation error1 = mm(1.0, -0.5, 18.0);
    cases.push_back({ "testAsymetricPartWithOffsetNoPreRotationWithError", "DoubleAsymPart", 0.5, 0.5, "AlwaysOff", false,
                      JPMachineLocation { 1.0, -0.5, 0, 18.0 }, std::vector<JPLocation>(4, error1) });
    const JPLocation error2 = mm(-0.6, 1.2, -12.0);
    std::vector<JPLocation> turned;
    for (double a : { 0.0, 45.0, 90.0, 180.0 }) turned.push_back(error2.rotateXy(-error2.rotation() + a));
    cases.push_back({ "testAsymetricPartWithOffsetWithPreRotationWithError", "DoubleAsymPart", 0.5, 0.5, "AlwaysOn", false,
                      JPMachineLocation { -0.6, 1.2, 0, -12.0 }, turned });
    for (const Case& c : cases) run(dir.string(), c);

    // ReferenceBottomVisionTest: OpenPnP's default configuration, a 1 mm pick tolerance, not pre-rotated.
    for (const double angle : { 13.0, -13.0 }) {
        Bench bench(std::string(JPLACER_TESTDATA_DIR) + "/../../openpnp-defaults/config");
        bench.tip.maxPickToleranceMm = 1;
        bench.up.errorOffsets = JPMachineLocation { 0.25, 0.75, 0, angle };
        JPBottomVision::Offset offset;
        std::string why;
        assert(bench.findOffsets("R0805-1K", 0, offset, why));
        const JPLocation& o = offset.location;
        std::fprintf(stderr, "testError %g: offsets %.3f, %.3f, %.3f\n", angle, o.x(), o.y(), o.rotation());
        assert(!offset.preRotated);
        assert(std::abs(o.x() - 0.25) <= 0.1 && std::abs(o.y() - 0.75) <= 0.1 && std::abs(o.rotation() - angle) <= 0.03);
    }

    // Offsets beyond the nozzle tip's Max. Pick Tolerance: refused.
    {
        Bench bench(dir.string());
        bench.settingsOf("DoubleAsymPart").setText("pre-rotate-usage", "AlwaysOff");
        bench.tip.maxPickToleranceMm = 0.5;
        JPBottomVision::Offset offset;
        std::string why;
        assert(!bench.findOffsets("DoubleAsymPart", 0, offset, why));
        std::fprintf(stderr, "%s\n", why.c_str());
        assert(why.find("larger than the allowed Max. Pick Tolerance 0.500mm set on nozzle tip NT1") != std::string::npos);
    }
    // The Part size check: the R0805's pads seen as wide and long as its body; the asymmetric part's pads (1.25 mm
    // across) far smaller than its 3 mm body.
    {
        Bench bench(dir.string());
        bench.settingsOf("R0805-1K").setText("check-part-size-method", "BodySize");
        JPBottomVision::Offset offset;
        std::string why;
        assert(bench.findOffsets("R0805-1K", 0, offset, why));
        bench.settingsOf("DoubleAsymPart").setText("check-part-size-method", "BodySize");
        bench.settingsOf("DoubleAsymPart").setLocationOf("vision-offset", mm(0.5, 0.5));
        assert(!bench.findOffsets("DoubleAsymPart", 0, offset, why));
        std::fprintf(stderr, "%s\n", why.c_str());
        assert(why.find("Part DoubleAsymPart width too small: nominal 3.000mm, limit 2.400mm") == 0);
    }
    fs::remove_all(dir);
    return 0;
}
