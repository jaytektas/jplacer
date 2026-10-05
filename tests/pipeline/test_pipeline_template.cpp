// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's template stages: a footprint drawn to scale (pads light, the
// gap between dark, the margin around); a part's template written under
// the part's name and read back by it, the package's name next, the body's
// size last; and a part with a mark at one end told from the same part
// turned half round by MatchPartTemplate.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "openpnp/JPXmlReader.h"
#include "pipeline/JPPipeline.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <filesystem>

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

JPPipelineValue::Outline square(double x, double y, double side) {
    return { { x - side / 2, y - side / 2 }, { x + side / 2, y - side / 2 }, { x + side / 2, y + side / 2 }, { x - side / 2, y + side / 2 } };
}

// A grey part body 40 x 80 px with a white mark near its top end, at
// (320, 240), turned `degrees`.
void part(JPPipeline& p, double degrees) {
    p.context().pixelsPerMmX = p.context().pixelsPerMmY = 20;
    p.context().capture = [degrees](const std::string&, const std::string&, cv::Mat& bgr, std::string&) {
        cv::Mat upright(120, 120, CV_8UC3, cv::Scalar::all(10));
        cv::rectangle(upright, cv::Rect(40, 20, 40, 80), cv::Scalar::all(150), cv::FILLED);
        cv::circle(upright, { 60, 32 }, 8, cv::Scalar::all(250), cv::FILLED);
        cv::Mat turned;
        cv::warpAffine(upright, turned, cv::getRotationMatrix2D({ 60, 60 }, degrees, 1), upright.size());
        bgr = cv::Mat(480, 640, CV_8UC3, cv::Scalar::all(10));
        turned.copyTo(bgr(cv::Rect(260, 180, 120, 120)));
        return true;
    };
}

} // namespace

int main() {
    std::string why;
    // Two 1 mm pads 2 mm apart, at 20 px/mm: 60 x 20 px drawn, half as much again around it.
    JPPipeline fp = make(stage("CreateFootprintTemplateImage", "fp", "footprint-view=\"Fiducial\" property-name=\"footprint\""));
    fp.context().pixelsPerMmX = fp.context().pixelsPerMmY = 20;
    JPPipelineValue::Footprint footprint;
    footprint.pads = { square(-1, 0, 1), square(1, 0, 1) };
    footprint.body = square(0, 0, 0.5);
    fp.setProperty("footprint", JPPipelineValue { footprint });
    assert(fp.process(why));
    const cv::Mat& t = fp.expectedResult("fp").image;
    assert(t.cols == 90 && t.rows == 30);
    assert(t.at<cv::Vec3b>(15, 25)[0] == 255 && t.at<cv::Vec3b>(15, 65)[0] == 255);
    assert(t.at<cv::Vec3b>(15, 45)[0] == 0 && t.at<cv::Vec3b>(2, 25)[0] == 0);
    // Without a footprint, it says what it needs.
    JPPipeline none = make(stage("CreateFootprintTemplateImage", "fp", ""));
    none.context().pixelsPerMmX = none.context().pixelsPerMmY = 20;
    assert(none.process(why) && none.result("fp")->model.failure()->message == "Property \"footprint\" is required.");

    // The upright part's picture written as its template, under its id.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "jplacer_test_templates";
    std::filesystem::remove_all(dir);
    JPPipelineValue::Part r1 { "R1-0805", "R0805", true, 2.0, 1.25 };
    const std::string capture = stage("ImageCapture", "0", "default-light=\"true\" settle-option=\"Settle\" count=\"1\"");
    const std::string find = stage("ConvertColor", "g", "conversion=\"Bgr2Gray\"")
                             + stage("Threshold", "th", "threshold=\"100\" auto=\"false\" invert=\"false\"")
                             + stage("FindContours", "c", "retrieval-mode=\"External\" approximation-method=\"None\"")
                             + stage("MinAreaRectContours", "rects", "contours-stage-name=\"c\"")
                             + stage("ImageRecall", "back", "image-stage-name=\"0\"");
    // Cut to the part, upright, as OpenPnP's template pipelines do.
    JPPipeline writer = make(capture + find + stage("CreateModelTemplateImage", "cut", "model-stage-name=\"rects\" degrees=\"0.0\"")
                             + stage("WritePartTemplateImage", "w", "template-file=\"\" extension=\".png\" prefix=\"up_\" as-package=\"false\""));
    part(writer, 0);
    writer.context().configurationDirectory = dir.string();
    writer.setProperty("part", JPPipelineValue { r1 });
    assert(writer.process(why) && !writer.result("w")->model.failure());
    assert(std::filesystem::exists(dir / "templates/new/up_R1-0805.png"));
    std::filesystem::rename(dir / "templates/new/up_R1-0805.png", dir / "templates/up_R1-0805.png");

    // Read back by the part's id; turned half round, the match says so.
    const std::string match = capture + find
                              + stage("ReadPartTemplateImage", "templ", "template-file=\"\" extension=\".png\" prefix=\"up_\" log=\"false\" color-space=\"Bgr\"")
                              + stage("ImageRecall", "back2", "image-stage-name=\"0\"")
                              + stage("MatchPartTemplate", "m", "log=\"false\" template-stage-name=\"templ\" model-stage-name=\"rects\" threshold=\"0.4\"");
    auto angle = [&](double degrees) {
        JPPipeline p = make(match);
        part(p, degrees);
        p.context().configurationDirectory = dir.string();
        p.setProperty("part", JPPipelineValue { r1 });
        assert(p.process(why));
        const cv::Mat& templ = p.expectedResult("templ").image;
        assert(std::abs(templ.cols - 41) <= 2 && std::abs(templ.rows - 81) <= 2);
        const auto* rects = std::get_if<std::vector<cv::RotatedRect>>(&p.expectedResult("m").model.value);
        assert(rects && rects->size() == 1);
        assert(std::abs(rects->front().center.x - 320) < 2 && std::abs(rects->front().center.y - 240) < 2);
        return double(rects->front().angle);
    };
    const double upright = angle(0), turned = angle(180);
    const double apart = std::fmod(std::fabs(turned - upright) + 360, 360);
    assert(std::abs(apart - 180) < 3);

    // No part file: the package's; neither: a white body-sized template, upright.
    JPPipeline body = make(stage("ReadPartTemplateImage", "templ", "prefix=\"up_\""));
    body.context().pixelsPerMmX = body.context().pixelsPerMmY = 20;
    body.context().configurationDirectory = dir.string();
    body.setProperty("part", JPPipelineValue { JPPipelineValue::Part { "C1", "C0603", true, 1.6, 0.8 } });
    assert(body.process(why));
    const cv::Mat& b = body.expectedResult("templ").image;
    assert(b.cols == 16 && b.rows == 32 && b.at<cv::Vec3b>(5, 5)[0] == 255);

    std::filesystem::remove_all(dir);
    return 0;
}
