// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// AffineWarp cuts a region (millimetres about the camera's centre, Y up)
// out turned a quarter: what is found in it, unwarped, is back where the
// camera sees it; a caller's region of interest stands in for the stage's,
// and says so. DetectRectangleHough finds a rectangle's outline about the
// picture's centre, its sides as drawn.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "openpnp/JPXmlReader.h"
#include "pipeline/JPPipeline.h"

#include <opencv2/imgproc.hpp>

#include <cmath>

using namespace jf;

namespace {

const std::string S = "org.openpnp.vision.pipeline.stages.";

JPPipeline make(const std::string& stages) {
    JPXmlElement root;
    std::string error;
    assert(JPXmlReader::parse("<cv-pipeline><stages>" + stages + "</stages></cv-pipeline>", root, error));
    return JPPipeline::fromXml(root);
}

std::string stage(const std::string& cls, const std::string& name, const std::string& attributes) {
    return "<cv-stage class=\"" + S + cls + "\" name=\"" + name + "\" enabled=\"true\" " + attributes + "/>";
}

} // namespace

int main() {
    std::string why;
    // A bright 40 x 20 px bar at (400, 200) in a 640 x 480 picture, 10 px/mm:
    // 8 mm right of the centre, 4 mm up.
    const std::string capture = stage("ImageCapture", "0", "default-light=\"true\" settle-option=\"Settle\" count=\"1\"");
    const std::string find = stage("ConvertColor", "g", "conversion=\"Bgr2Gray\"")
                             + stage("Threshold", "th", "threshold=\"100\" auto=\"false\" invert=\"false\"")
                             + stage("FindContours", "c", "retrieval-mode=\"External\" approximation-method=\"None\"")
                             + stage("MinAreaRectContours", "rects", "contours-stage-name=\"c\"")
                             + stage("AffineUnwarp", "un", "warp-stage-name=\"warp\" results-stage-name=\"rects\"");
    // The region 4..12 mm right, 0..8 mm up, turned a quarter: its upper side runs down the picture.
    JPPipeline p = make(capture
                        + stage("AffineWarp", "warp",
                                "length-unit=\"Millimeters\" x0=\"4\" y0=\"8\" x1=\"4\" y1=\"0\" x2=\"12\" y2=\"8\" scale=\"1.0\" rectify=\"true\" region-of-interest-property=\"regionOfInterest\"")
                        + find);
    p.context().pixelsPerMmX = p.context().pixelsPerMmY = 10;
    p.context().capture = [](const std::string&, const std::string&, cv::Mat& bgr, std::string&) {
        bgr = cv::Mat(480, 640, CV_8UC3, cv::Scalar::all(10));
        cv::rectangle(bgr, cv::Rect(380, 190, 40, 20), cv::Scalar::all(240), cv::FILLED);
        return true;
    };
    assert(p.process(why));
    // The region, 80 x 80 px; in it the bar stands upright.
    const JPPipeline::Result& warp = p.expectedResult("warp");
    assert(warp.image.cols == 80 && warp.image.rows == 80);
    const auto* inRegion = std::get_if<std::vector<cv::RotatedRect>>(&p.expectedResult("rects").model.value);
    assert(inRegion && inRegion->size() == 1);
    const cv::Rect box = inRegion->front().boundingRect();
    assert(std::abs(box.width - 20) <= 2 && std::abs(box.height - 40) <= 2);
    // Back in the camera's pixels: where it was, as long as it was.
    const auto* back = std::get_if<std::vector<cv::RotatedRect>>(&p.expectedResult("un").model.value);
    assert(back && back->size() == 1);
    const cv::RotatedRect& r = back->front();
    assert(std::abs(r.center.x - 399.5) < 1.5 && std::abs(r.center.y - 199.5) < 1.5);
    assert(std::abs(std::max(r.size.width, r.size.height) - 40) < 2 && std::abs(std::min(r.size.width, r.size.height) - 20) < 2);

    // The caller's region of interest instead: the same region given untwisted.
    JPPipelineValue::RegionOfInterest roi { { 4, 8 }, { 12, 8 }, { 4, 0 }, true };
    p.setProperty("regionOfInterest", JPPipelineValue { roi });
    assert(p.process(why));
    assert(p.overrides("warp").at("x1") == "12" && p.overrides("warp").at("rectify") == "true");
    const cv::Rect box2 = std::get_if<std::vector<cv::RotatedRect>>(&p.expectedResult("rects").model.value)->front().boundingRect();
    assert(std::abs(box2.width - 40) <= 2 && std::abs(box2.height - 20) <= 2);
    const cv::RotatedRect& r2 = std::get_if<std::vector<cv::RotatedRect>>(&p.expectedResult("un").model.value)->front();
    assert(std::abs(r2.center.x - 399.5) < 1.5 && std::abs(r2.center.y - 199.5) < 1.5);

    // A rectangle's outline about the centre, 60 x 30 px.
    JPPipeline h = make(capture + stage("ConvertColor", "g", "conversion=\"Bgr2Gray\"")
                        + stage("DetectRectangleHough", "rect",
                                "index=\"0\" threshold=\"20\" acc-weight=\"0.1\" delta-theta=\"0.03\" delta-alpha=\"0.03\" delta-rho=\"20\" min-rho=\"10\" rho-a=\"60\" rho-b=\"30\" draw-lines=\"false\""));
    h.context().capture = [](const std::string&, const std::string&, cv::Mat& bgr, std::string&) {
        bgr = cv::Mat(200, 200, CV_8UC3, cv::Scalar::all(0));
        cv::rectangle(bgr, cv::Rect(70, 85, 60, 30), cv::Scalar::all(255), 1);
        return true;
    };
    assert(h.process(why));
    const auto* rect = std::get_if<cv::RotatedRect>(&h.expectedResult("rect").model.value);
    assert(rect);
    assert(std::abs(rect->center.x - 99.5) < 2 && std::abs(rect->center.y - 99.5) < 2);
    assert(std::abs(std::max(rect->size.width, rect->size.height) - 59) < 3 && std::abs(std::min(rect->size.width, rect->size.height) - 29) < 3);
    return 0;
}
