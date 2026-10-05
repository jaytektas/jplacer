// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// DetectCircularSymmetry finds a nozzle tip's ring off the picture's centre,
// to the pixel, its diameter about the ring's; given a diameter by the
// caller, it searches about it, and says so; asked for three targets it
// finds three discs in a noisy picture, best first. DetectRectlinearSymmetry finds a part turned
// 10°: its centre, its angle, its size.
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

JPPipeline make(const std::string& stages, cv::Mat picture) {
    JPXmlElement root;
    std::string error;
    assert(JPXmlReader::parse("<cv-pipeline><stages><cv-stage class=\"" + S
                                  + "ImageCapture\" name=\"0\" enabled=\"true\" default-light=\"true\" settle-option=\"Settle\" count=\"1\"/>"
                                  + stages + "</stages></cv-pipeline>",
                              root, error));
    JPPipeline p = JPPipeline::fromXml(root);
    p.context().pixelsPerMmX = p.context().pixelsPerMmY = 20;
    p.context().capture = [picture](const std::string&, const std::string&, cv::Mat& bgr, std::string&) {
        bgr = picture.clone();
        return true;
    };
    return p;
}

} // namespace

int main() {
    std::string why;
    // A tip: a bright ring 60 px across with a dark bore, at (335, 228).
    cv::Mat tip(480, 640, CV_8UC3, cv::Scalar::all(30));
    cv::circle(tip, { 335, 228 }, 30, cv::Scalar::all(220), cv::FILLED, cv::LINE_AA);
    cv::circle(tip, { 335, 228 }, 10, cv::Scalar::all(40), cv::FILLED, cv::LINE_AA);
    const std::string circular = "<cv-stage class=\"" + S
                                 + "DetectCircularSymmetry\" name=\"c\" enabled=\"true\" min-diameter=\"30\" max-diameter=\"90\" max-distance=\"100\" max-target-count=\"1\" min-symmetry=\"1.2\" sub-sampling=\"8\" super-sampling=\"1\" property-name=\"nozzleTip\" diagnostics=\"true\"/>";
    JPPipeline one = make(circular, tip);
    assert(one.process(why));
    const auto* circles = std::get_if<std::vector<JPPipelineModel::Circle>>(&one.expectedResult("c").model.value);
    assert(circles && circles->size() == 1);
    const JPPipelineModel::Circle& c = circles->front();
    assert(std::abs(c.x - 335.5) <= 1 && std::abs(c.y - 228.5) <= 1);
    assert(std::abs(c.diameter - 60) <= 6 && c.score > 1.2);
    // The diagnostics drawn on the picture: the cross hairs green.
    const cv::Vec3b hair = one.workingImage().at<cv::Vec3b>(228, 300);
    assert(hair[1] > hair[2]);

    // The caller's diameter: searched about it (−40 %, +20 %).
    JPPipeline told = make(circular, tip);
    told.setProperty("nozzleTip.diameter", JPPipelineValue { 60.0 });
    assert(told.process(why));
    assert(told.overrides("c").at("min-diameter") == "36" && told.overrides("c").at("max-diameter") == "72");
    assert(std::get_if<std::vector<JPPipelineModel::Circle>>(&told.expectedResult("c").model.value)->size() == 1);

    // Three discs, three targets wanted.
    cv::Mat discs(480, 640, CV_8UC1, cv::Scalar(20));
    const cv::Point at[] = { { 200, 160 }, { 420, 180 }, { 320, 330 } };
    for (const cv::Point& d : at) cv::circle(discs, d, 18, cv::Scalar(230), cv::FILLED, cv::LINE_AA);
    // A camera's noise: no two places score exactly alike (OpenPnP takes only true peaks).
    cv::Mat noise(discs.size(), CV_8SC1);
    cv::theRNG().state = 7;
    cv::randn(noise, 0, 4);
    cv::add(discs, noise, discs, cv::noArray(), CV_8U);
    JPPipeline three = make("<cv-stage class=\"" + S + "ConvertColor\" name=\"g\" enabled=\"true\" conversion=\"Bgr2Gray\"/><cv-stage class=\"" + S
                                + "DetectCircularSymmetry\" name=\"c\" enabled=\"true\" min-diameter=\"20\" max-diameter=\"60\" max-distance=\"220\" max-target-count=\"3\" min-symmetry=\"1.2\" corr-symmetry=\"0.2\" sub-sampling=\"8\" super-sampling=\"1\"/>",
                            [&] {
                                cv::Mat bgr;
                                cv::cvtColor(discs, bgr, cv::COLOR_GRAY2BGR);
                                return bgr;
                            }());
    assert(three.process(why));
    const auto* found = std::get_if<std::vector<JPPipelineModel::Circle>>(&three.expectedResult("c").model.value);
    assert(found && found->size() == 3);
    for (const JPPipelineModel::Circle& f : *found) {
        bool near = false;
        for (const cv::Point& d : at) near |= std::hypot(f.x - d.x - 0.5, f.y - d.y - 0.5) <= 1.5;
        assert(near);
    }
    assert((*found)[0].score >= (*found)[1].score && (*found)[1].score >= (*found)[2].score);

    // A part 120 x 60 px with two pads at its ends, turned 10°, at (330, 250).
    cv::Mat part(480, 640, CV_8UC3, cv::Scalar::all(10));
    cv::Point2f corners[4];
    cv::RotatedRect(cv::Point2f(330, 250), cv::Size2f(120, 60), 10).points(corners);
    std::vector<cv::Point> poly(corners, corners + 4);
    cv::fillConvexPoly(part, poly, cv::Scalar::all(90), cv::LINE_AA);
    for (const float x : { -45.0f, 45.0f }) {
        const float a = float(10 * M_PI / 180);
        cv::RotatedRect(cv::Point2f(330 + x * std::cos(a), 250 + x * std::sin(a)), cv::Size2f(30, 60), 10).points(corners);
        std::vector<cv::Point> pad(corners, corners + 4);
        cv::fillConvexPoly(part, pad, cv::Scalar::all(240), cv::LINE_AA);
    }
    JPPipeline rect = make("<cv-stage class=\"" + S
                               + "DetectRectlinearSymmetry\" name=\"r\" enabled=\"true\" expected-angle=\"0\" search-distance=\"60\" search-angle=\"45\" max-width=\"200\" max-height=\"200\" min-symmetry=\"10\" sub-sampling=\"8\" super-sampling=\"1\"/>",
                           part);
    assert(rect.process(why));
    const auto* r = std::get_if<cv::RotatedRect>(&rect.expectedResult("r").model.value);
    assert(r);
    assert(std::abs(r->center.x - 330) < 2 && std::abs(r->center.y - 250) < 2);
    // Turned 10° (with the picture's Y down, OpenCV's angle is clockwise).
    const double a = std::fmod(r->angle + 360 + 45, 90) - 45;
    assert(std::abs(std::abs(a) - 10) < 1.5);
    const float longSide = std::max(r->size.width, r->size.height), shortSide = std::min(r->size.width, r->size.height);
    assert(std::abs(longSide - 120) < 8 && std::abs(shortSide - 60) < 8);
    return 0;
}
