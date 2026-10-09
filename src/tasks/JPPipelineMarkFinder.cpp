// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPipelineMarkFinder.h"

#include "openpnp/JPXmlReader.h"
#include "pipeline/JPDefaultPipelines.h"
#include "pipeline/JPStraightPicture.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// OpenPnP's names: the stage giving the marks, and the properties the calibration sets.
constexpr const char* kResults = "results";
// Each size tried a fifth bigger than the last (findAnySize).
constexpr double kSizeStep = 1.2;

// The picture in colour, as OpenPnP's ImageCapture gives a pipeline it.
cv::Mat toBgr(const JPFrame& frame) {
    cv::Mat rgba(frame.height, frame.width, CV_8UC4, const_cast<uint8_t*>(frame.rgba.data()));
    cv::Mat bgr;
    cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
    return bgr;
}

} // namespace

JPPipelineMarkFinder::JPPipelineMarkFinder(const std::string& pipelineXml, std::string control) : m_control(std::move(control)) {
    JPXmlElement root;
    std::string error;
    if (JPXmlReader::parse(pipelineXml, root, error)) {
        m_pipeline = JPPipeline::fromXml(root);
        m_parsed = !m_pipeline.stages().empty();
    }
    useCapture();
}

JPPipelineMarkFinder::JPPipelineMarkFinder(JPPipeline prepared, std::string control, double pxPerMmX, double pxPerMmY)
    : m_pipeline(std::move(prepared)), m_control(std::move(control)) {
    m_parsed = !m_pipeline.stages().empty();
    m_pipeline.context().pixelsPerMmX = pxPerMmX;
    m_pipeline.context().pixelsPerMmY = pxPerMmY;
    useCapture();
}

void JPPipelineMarkFinder::useCapture() {
    // Named for vision debugging's folders: the nozzle tip's, else a round mark's (a camera's calibration).
    m_pipeline.context().label = m_control == "nozzleTip" ? "nozzle tip" : "round mark";
    // Its ImageCapture: the picture taken (already settled, the light as it was).
    m_pipeline.context().capture = [this](const std::string&, const std::string&, cv::Mat& bgr, std::string& why) {
        if (m_picture.empty()) {
            why = "no picture";
            return false;
        }
        bgr = m_picture.clone();
        return true;
    };
}

JPRoundMark JPPipelineMarkFinder::find(const JPFrame& image, double x, double y, double maxDistance, double diameter) {
    if (image.width <= 0 || image.height <= 0) {
        JPRoundMark m;
        m.why = "no picture";
        return m;
    }
    return find(toBgr(image), x, y, maxDistance, diameter);
}

JPRoundMark JPPipelineMarkFinder::find(const cv::Mat& bgr, double x, double y, double maxDistance, double diameter) {
    JPRoundMark m;
    if (!m_parsed) {
        m.why = "the camera's calibration pipeline could not be read";
        return m;
    }
    if (bgr.empty()) {
        m.why = "no picture";
        return m;
    }
    m_picture = bgr;
    m_pipeline.context().cameraWidth = bgr.cols;
    m_pipeline.context().cameraHeight = bgr.rows;
    m_pipeline.setProperty(m_control + ".center", JPPipelineValue { JPPipelineValue::Pixel { x, y } });
    m_pipeline.setProperty(m_control + ".maxDistance", JPPipelineValue { maxDistance });
    m_pipeline.setProperty(m_control + ".diameter", JPPipelineValue { diameter });
    std::string why;
    if (!m_pipeline.process(why)) {
        m.why = why;
        return m;
    }
    // The mark: the "results" keypoint (OpenPnP's getExpectedResult(PIPELINE_RESULTS_NAME)).
    const JPPipeline::Result* results = m_pipeline.result(kResults);
    if (!results) {
        m.why = "the calibration pipeline has no \"results\" stage";
        return m;
    }
    // As OpenPnP takes them: keypoints, circles or rotated rectangles, their centres (and sizes).
    struct Spot { double x, y, size; };
    std::vector<Spot> spots;
    const auto& v = results->model.value;
    if (const auto* k = std::get_if<std::vector<cv::KeyPoint>>(&v)) for (const auto& p : *k) spots.push_back({ p.pt.x, p.pt.y, p.size });
    else if (const auto* one = std::get_if<cv::KeyPoint>(&v)) spots.push_back({ one->pt.x, one->pt.y, one->size });
    else if (const auto* c = std::get_if<std::vector<JPPipelineModel::Circle>>(&v)) for (const auto& p : *c) spots.push_back({ p.x, p.y, p.diameter });
    else if (const auto* circle = std::get_if<JPPipelineModel::Circle>(&v)) spots.push_back({ circle->x, circle->y, circle->diameter });
    else if (const auto* r = std::get_if<std::vector<cv::RotatedRect>>(&v)) for (const auto& p : *r) spots.push_back({ p.center.x, p.center.y, 0 });
    else if (const auto* rect = std::get_if<cv::RotatedRect>(&v)) spots.push_back({ rect->center.x, rect->center.y, 0 });
    // Further than asked: not it (OpenPnP drops a result beyond its threshold).
    std::erase_if(spots, [&](const Spot& p) { return std::hypot(p.x - x, p.y - y) > maxDistance; });
    if (spots.size() != 1) {
        if (const auto* failed = std::get_if<JPPipelineModel::Failure>(&v)) m.why = failed->message;
        else if (spots.empty()) m.why = "no round mark found";
        else m.why = "the pipeline found more than one; it should find exactly one";
        return m;
    }
    m.found = true;
    m.x = spots.front().x;
    m.y = spots.front().y;
    m.diameter = spots.front().size > 0 ? spots.front().size : diameter;
    m.confidence = 1;
    // The symmetry of the circle it came from, where the stage gave one (for choosing among sizes).
    for (const JPPipelineStage& s : m_pipeline.stages())
        if (const JPPipeline::Result* r = m_pipeline.result(s.name()))
            if (const auto* circles = std::get_if<std::vector<JPPipelineModel::Circle>>(&r->model.value))
                for (const JPPipelineModel::Circle& c : *circles)
                    if (std::hypot(c.x - m.x, c.y - m.y) < 1 && std::isfinite(c.score)) m.symmetry = c.score;
    return m;
}

JPRoundMark JPPipelineMarkFinder::findAnySize(const JPFrame& image, double x, double y, double maxDistance, double minDiameter,
                                              double maxDiameter) {
    return findAnySize(toBgr(image), x, y, maxDistance, minDiameter, maxDiameter);
}

JPRoundMark JPPipelineMarkFinder::findAnySize(const cv::Mat& image, double x, double y, double maxDistance, double minDiameter,
                                              double maxDiameter) {
    JPRoundMark best;
    best.why = "no round mark found";
    for (double d = std::max(1.0, minDiameter); d <= maxDiameter; d *= kSizeStep) {
        JPRoundMark m = find(image, x, y, maxDistance, d);
        if (!m.found) {
            if (!best.found && !m.why.empty()) best.why = m.why;
            continue;
        }
        if (!best.found || m.symmetry > best.symmetry) best = m;
    }
    return best;
}

JPRoundMark JPPipelineMarkFinder::onMachine(const JPCameraConfig& cam, const JPCameraCalibration& calibration, const JPFrame& frame,
                                            double x, double y, double viewX, double viewY, double searchMm, double& diameterMm,
                                            double leastMm, double mostMm, double& mx, double& my) {
    cv::Mat bgr = toBgr(frame);
    JPCameraCalibration cal = calibration;
    if (const auto straight = JPStraightPicture::of(calibration, cam.looksUp, cam.showAll)) {
        cv::Mat flat;
        if (straight->straighten(bgr, flat)) {
            bgr = flat;
            cal = straight->calibration();
        }
    }
    JPPipelineMarkFinder finder(cam.calibrationPipeline.empty() ? JPDefaultPipelines::cameraCalibration() : cam.calibrationPipeline);
    const double scale = cal.scale();
    double ex = bgr.cols / 2.0, ey = bgr.rows / 2.0;
    cal.pixelFor(x, y, viewX, viewY, ex, ey);
    JPRoundMark m = diameterMm > 0 ? finder.find(bgr, ex, ey, searchMm * scale, diameterMm * scale)
                                   : finder.findAnySize(bgr, ex, ey, searchMm * scale, leastMm * scale, mostMm * scale);
    if (!m.found) return m;
    if (!cal.machinePoint(m.x, m.y, viewX, viewY, mx, my)) {
        m.found = false;
        m.why = "the camera's calibration cannot place it on the machine";
        return m;
    }
    if (diameterMm <= 0 && scale > 0) diameterMm = m.diameter / scale;
    return m;
}

} // inline namespace jf
