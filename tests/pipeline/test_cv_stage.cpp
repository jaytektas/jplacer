// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's CvStageTest and OpenCvTest. CvStageTest: a stage's setting, and the pipeline property set in its place
// (OpenPnP's getPossiblePipelinePropertyOverride), converted as OpenPnP converts it for VisionUtilsTest's camera
// (640 x 480 at 1 mm a pixel): a flag from a flag, a text from a text; a number from a number, a whole number, an
// area or a length (in pixels); a whole number from those, rounded as Java's Math.round; a point from a point or a
// location (in pixels from the picture's middle, Y up). jplacer's stages take no area, length or location settings
// of their own (OpenPnP's Area, Length and Location to themselves), so those have nothing to convert. OpenCvTest:
// OpenCV works (a picture made grey).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "pipeline/JPPipeline.h"

#include <opencv2/imgproc.hpp>

#include <string>

using namespace jf;

namespace {

using V = JPPipelineValue;
const std::string kProp = "propName";

// A pipeline with VisionUtilsTest's camera, and a stage of no kind of its own (OpenPnP's TestStage).
struct Bench {
    JPPipeline      pipeline;
    JPPipelineStage stage = JPPipelineStage::create("org.openpnp.vision.pipeline.stages.TestStage", "TestStage");
    Bench() {
        JPPipeline::Context& c = pipeline.context();
        c.pixelsPerMmX = c.pixelsPerMmY = 1;
        c.cameraWidth = 640;
        c.cameraHeight = 480;
        c.locationToPixel = [](double x, double y, double& px, double& py) {
            px = 640 / 2.0 + x;
            py = 480 / 2.0 - y;
            return true;
        };
    }
};

void toBoolean() {
    Bench b;
    assert(b.pipeline.overriddenFlag(b.stage, "p", true, kProp) == true);
    b.pipeline.setProperty(kProp, V { false });
    assert(b.pipeline.overriddenFlag(b.stage, "p", true, kProp) == false);
}

void toString() {
    Bench b;
    assert(b.pipeline.overriddenText(b.stage, "p", "true", kProp) == "true");
    b.pipeline.setProperty(kProp, V { std::string("false") });
    assert(b.pipeline.overriddenText(b.stage, "p", "true", kProp) == "false");
}

void toDouble() {
    const double original = 39.5;
    for (const auto& [value, expected] : { std::pair { V { -87.4 }, -87.4 }, std::pair { V { -87L }, -87.0 },
                                           std::pair { V { V::AreaMm2 { -87.4 } }, -87.4 }, std::pair { V { V::LengthMm { -87.4 } }, -87.4 } }) {
        Bench b;
        assert(b.pipeline.overridden(b.stage, "p", original, kProp) == original);
        b.pipeline.setProperty(kProp, value);
        assert(b.pipeline.overridden(b.stage, "p", original, kProp) == expected);
    }
}

void toInteger() {
    // Integer and Long alike (jplacer's whole numbers): from a number rounded as Math.round, a whole number as it is.
    const long original = 39;
    for (const auto& [value, expected] : { std::pair { V { -87.4 }, -87L }, std::pair { V { -87.5 }, -87L }, std::pair { V { 87.5 }, 88L },
                                           std::pair { V { -87L }, -87L }, std::pair { V { V::AreaMm2 { -87.4 } }, -87L },
                                           std::pair { V { V::LengthMm { -87.6 } }, -88L } }) {
        Bench b;
        assert(b.pipeline.overriddenInteger(b.stage, "p", original, kProp) == original);
        b.pipeline.setProperty(kProp, value);
        assert(b.pipeline.overriddenInteger(b.stage, "p", original, kProp) == expected);
    }
}

void toPoint() {
    // org.opencv.core.Point and org.openpnp.model.Point alike (jplacer's pixel point).
    const cv::Point2d original(39.5, -87.4);
    {
        Bench b;
        assert(b.pipeline.overriddenPoint(b.stage, "p", original, kProp) == original);
        b.pipeline.setProperty(kProp, V { V::Pixel { -47.8, 73.1 } });
        assert(b.pipeline.overriddenPoint(b.stage, "p", original, kProp) == cv::Point2d(-47.8, 73.1));
    }
    // From Location: in pixels, from the picture's middle, Y up.
    {
        Bench b;
        b.pipeline.setProperty(kProp, V { V::LocationMm { -47.8, 73.1 } });
        assert(b.pipeline.overriddenPoint(b.stage, "p", original, kProp) == cv::Point2d(640 / 2.0 - 47.8, 480 / 2.0 - 73.1));
    }
}

void openCvWorks() {
    const cv::Mat img(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    assert(gray.type() == CV_8UC1 && gray.cols == 640 && gray.rows == 480);
}

} // namespace

int main() {
    toBoolean();
    toString();
    toDouble();
    toInteger();
    toPoint();
    openCvWorks();
    return 0;
}
