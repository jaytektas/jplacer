// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's stock bottom vision pipeline (parameters, capture, blur, masks,
// colour conversions, threshold, contours, MinAreaRect, drawing) run on a
// picture of a bright, turned rectangle: the "results" stage finds it where
// it is, its size and angle; each stage's result is kept; a parameter's
// assigned value reaches its stage; written back, the pipeline is as read.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"
#include "pipeline/JPPipeline.h"

#include <opencv2/imgproc.hpp>

#include <cmath>

using namespace jf;

namespace {

constexpr const char* kPipeline = R"(<cv-pipeline>
<stages>
<cv-stage class="org.openpnp.vision.pipeline.stages.ParameterNumeric" name="pThreshold" enabled="true" parameter-label="Threshold" parameter-description="Set the brightness threshold that isolates the shiny contacts of a part." stage-name="threshold" property-name="threshold" effect-stage-name="threshold" preview-result="true" minimum-value="1.0" maximum-value="254.0" default-value="100.0" numeric-type="Integer"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.ParameterNumeric" name="pDetail" enabled="true" parameter-label="Min. Detail Size" parameter-description="Minimal size of a detail that should be included in the detected shape." stage-name="filterContours" property-name="minArea" effect-stage-name="contours" preview-result="true" minimum-value="0.0" maximum-value="0.25" default-value="0.01" numeric-type="SquareMillimetersToPixels"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" default-light="true" settle-option="Settle" count="1"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb0" enabled="true" prefix="bv_source_" suffix=".png"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="3" enabled="true" kernel-size="9" property-name="BlurGaussian"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="4" enabled="true" diameter="525" property-name="MaskCircle"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="4b" enabled="true" diameter="100000" property-name="partmask"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="5" enabled="true" conversion="Bgr2HsvFull"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.MaskHsv" name="6" enabled="true" auto="false" fraction-to-mask="0.0" hue-min="60" hue-max="130" saturation-min="32" saturation-max="255" value-min="64" value-max="255" soft-edge="0" soft-factor="1.0" invert="false" binary-mask="false" property-name="MaskHsv"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="7" enabled="true" conversion="Hsv2BgrFull"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="8" enabled="true" conversion="Bgr2Gray"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.Threshold" name="threshold" enabled="true" threshold="100" auto="false" invert="false"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="findCountours" enabled="true" retrieval-mode="List" approximation-method="None"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="filterContours" enabled="true" contours-stage-name="findCountours" min-area="0.01" max-area="900000.0" property-name="FilterContours"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="11" enabled="true" diameter="0" property-name=""/>
<cv-stage class="org.openpnp.vision.pipeline.stages.DrawContours" name="contours" enabled="true" contours-stage-name="filterContours" thickness="1" index="-1">
<color r="255" g="255" b="255" a="255"/>
</cv-stage>
<cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRect" name="results" enabled="true" threshold-min="100" threshold-max="255" expected-angle="0.0" search-angle="45.0" left-edge="true" right-edge="true" top-edge="true" bottom-edge="true" diagnostics="false" property-name="MinAreaRect"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="14" enabled="true" image-stage-name="0"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.DrawRotatedRects" name="15" enabled="true" rotated-rects-stage-name="results" thickness="2" draw-rect-center="false" rect-center-radius="20" show-orientation="false"/>
</stages>
</cv-pipeline>)";

} // namespace

int main() {
    JPXmlElement root;
    std::string error;
    assert(JPXmlReader::parse(kPipeline, root, error));
    JPPipeline pipeline = JPPipeline::fromXml(root);
    assert(pipeline.stages().size() == 19);

    // The camera: a dark picture, a bright 120 x 60 px rectangle at (330, 250) turned 20°.
    auto& ctx = pipeline.context();
    ctx.pixelsPerMmX = ctx.pixelsPerMmY = 20;
    ctx.capture = [](const std::string&, const std::string&, cv::Mat& bgr, std::string&) {
        bgr = cv::Mat(480, 640, CV_8UC3, cv::Scalar(20, 20, 20));
        cv::Point2f pts[4];
        cv::RotatedRect(cv::Point2f(330, 250), cv::Size2f(120, 60), 20).points(pts);
        std::vector<cv::Point> poly(pts, pts + 4);
        cv::fillConvexPoly(bgr, poly, cv::Scalar(230, 230, 230));
        return true;
    };
    std::string why;
    assert(pipeline.process(why));
    const JPPipeline::Result& r = pipeline.expectedResult("results");
    const auto* rect = std::get_if<cv::RotatedRect>(&r.model.value);
    assert(rect);
    assert(std::abs(rect->center.x - 330) < 2 && std::abs(rect->center.y - 250) < 2);
    const float longSide = std::max(rect->size.width, rect->size.height), shortSide = std::min(rect->size.width, rect->size.height);
    assert(std::abs(longSide - 120) < 4 && std::abs(shortSide - 60) < 4);
    // Turned 20° (or the same rectangle described from its other side).
    const double a = std::fmod(std::abs(rect->angle) + 180, 90);
    assert(std::abs(a - 20) < 2 || std::abs(a - 70) < 2);
    // Every stage's result kept; the gray image after "8".
    assert(pipeline.result("8") && pipeline.result("8")->colorSpace == "Gray");
    // The last image is the capture with the rectangle drawn on it.
    assert(pipeline.result("15")->image.channels() == 3);
    // The parameter's default reached its stage, in pixels (0.01 mm² at 20 px/mm: 4 px²).
    assert(std::abs(pipeline.stage("filterContours")->number("min-area") - 4) < 1e-6);
    // An assigned threshold too.
    pipeline.setProperty("pThreshold", JPPipelineValue { 150.0 });
    assert(pipeline.process(why) && pipeline.stage("threshold")->integer("threshold") == 150);
    assert(pipeline.overrides("threshold").at("threshold") == "150");

    // Back to its defaults, written as read.
    pipeline.resetToDefaults();
    assert(pipeline.stage("threshold")->integer("threshold") == 100);

    // No camera: the capture stops the pipeline, and says why.
    JPPipeline blind = JPPipeline::fromXml(root);
    assert(!blind.process(why) && why == "No Camera set on pipeline.");
    return 0;
}
