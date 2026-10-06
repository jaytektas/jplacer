// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPipelineMarkFinder.h"

#include "openpnp/JPXmlReader.h"
#include "vision/JPRoundMarkFinder.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// OpenPnP's names: the stage giving the marks, and the properties the calibration sets.
constexpr const char* kResults = "results";
// Each size tried a fifth bigger than the last (findAnySize).
constexpr double kSizeStep = 1.2;
// The pipeline's find refined to a fraction of a pixel (its DetectCircularSymmetry settles on a
// 1 / super-sampling grid) within this share of the mark's diameter of it: no other mark that near.
constexpr double kRefineShare = 0.5;

cv::Mat toBgr(const JPGrayImage& image) {
    cv::Mat gray(image.height, image.width, CV_8UC1);
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x)
            gray.at<uint8_t>(y, x) = uint8_t(std::clamp(std::lround(image.at(x, y)), 0L, 255L));
    cv::Mat bgr;
    cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);
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
    // Its ImageCapture: the picture the calibration took (already settled, the light as it was).
    m_pipeline.context().capture = [this](const std::string&, const std::string&, cv::Mat& bgr, std::string& why) {
        if (m_picture.empty()) {
            why = "no picture";
            return false;
        }
        bgr = m_picture.clone();
        return true;
    };
}

JPRoundMark JPPipelineMarkFinder::find(const JPGrayImage& image, double x, double y, double maxDistance, double diameter) {
    JPRoundMark m;
    if (!m_parsed) {
        m.why = "the camera's calibration pipeline could not be read";
        return m;
    }
    if (image.width <= 0 || image.height <= 0) {
        m.why = "no picture";
        return m;
    }
    m_picture = toBgr(image);
    m_pipeline.context().cameraWidth = image.width;
    m_pipeline.context().cameraHeight = image.height;
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
    // Its centre to a fraction of a pixel, near where the pipeline found it.
    JPRoundMarkFinder::Request near;
    near.expectedX = m.x;
    near.expectedY = m.y;
    near.searchRadius = kRefineShare * diameter;
    near.diameter = diameter;
    if (const JPRoundMark fine = JPRoundMarkFinder::find(image, near); fine.found) {
        m.x = fine.x;
        m.y = fine.y;
        m.diameter = fine.diameter;
    }
    // The symmetry of the circle it came from, where the stage gave one (for choosing among sizes).
    for (const JPPipelineStage& s : m_pipeline.stages())
        if (const JPPipeline::Result* r = m_pipeline.result(s.name()))
            if (const auto* circles = std::get_if<std::vector<JPPipelineModel::Circle>>(&r->model.value))
                for (const JPPipelineModel::Circle& c : *circles)
                    if (std::hypot(c.x - m.x, c.y - m.y) < 1 && std::isfinite(c.score)) m.symmetry = c.score;
    return m;
}

JPRoundMark JPPipelineMarkFinder::findAnySize(const JPGrayImage& image, double x, double y, double maxDistance, double minDiameter,
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

} // inline namespace jf
