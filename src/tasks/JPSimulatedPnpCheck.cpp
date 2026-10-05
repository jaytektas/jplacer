// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSimulatedPnpCheck.h"

#include "model/JPLength.h"
#include "pipeline/JPStageUtil.h"

#include <opencv2/imgproc.hpp>

#include <cfloat>
#include <cmath>
#include <optional>
#include <stdexcept>

inline namespace jf {

namespace {

// OpenPnP's: the template a margin of half its size around the footprint (at
// least 8 pixels), and the picture searched five times the template (at
// least 80 pixels).
constexpr double kMarginFactor = 1.5;
constexpr double kMinimumMarginPx = 8;
constexpr int    kSearchFactor = 5;
constexpr int    kLeastSearchPx = 80;
// Matches a third of the minimum score or better are considered; a nearer
// one wins over a better scoring one unless that scores more than 1/0.85 as much.
constexpr double kConsideredShare = 1.0 / 3;
constexpr double kNearerShare = 0.85;
// Drawing and rounding slack: 1.5 pixel diagonals, and 2.5% of the template's diagonal.
constexpr double kSlackPx = 1.5, kSlackShare = 0.025;
// OpenPnP's test picture: part bodies and pad marks (in HSV, full hue range):
// bright enough, not too saturated; pads also not grey.
constexpr double kMaskLeastValue = 64, kMaskMostSaturation = 180;

} // namespace

const cv::Mat* JPSimulatedPnpCheck::picture(const std::string& path, std::string& detail) {
    if (const auto it = m_pictures.find(path); it != m_pictures.end()) return &it->second;
    try {
        return &(m_pictures[path] = JPStageUtil::readPicture(path));
    } catch (const std::exception& e) {
        detail = e.what();
        return nullptr;
    }
}

bool JPSimulatedPnpCheck::isPartLocation(const Picture& pic, const JPFootprint& footprint, double x, double y,
                                         double rotation, bool pick, const Tolerance& tolerance, std::string& detail) {
    std::lock_guard lk(m_mutex);
    const cv::Mat* source = picture(pic.path, detail);
    if (!source) return false;
    if (pic.unitsPerPixelX <= 0 || pic.unitsPerPixelY <= 0) {
        detail = "the image camera's picture has no scale";
        return false;
    }
    // The template: the footprint turned as the nozzle is, at the picture's
    // scale, Y up. To pick: all black (the body over the pads) on white; to
    // place: white pads over a black body, on black.
    const double mm = JPLength(1, footprint.units).convertToUnits(JPLengthUnit::Millimeters).value();
    auto outline = [mm](const JPFootprint::Outline& o) {
        JPPipelineValue::Outline out;
        for (const JPFootprint::Point& p : o) out.push_back({ p.x * mm, p.y * mm });
        return out;
    };
    std::vector<JPPipelineValue::Outline> pads;
    for (const JPFootprint::Outline& o : footprint.padsOutlines()) pads.push_back(outline(o));
    const std::vector<JPPipelineValue::Outline> body { outline(footprint.bodyOutline()) };
    const double a = -rotation * M_PI / 180, ca = std::cos(a), sa = std::sin(a);
    auto px = [&](const JPPipelineValue::LocationMm& m) {
        const double u = m.x / pic.unitsPerPixelX, v = -m.y / pic.unitsPerPixelY;
        return cv::Point2d(u * ca - v * sa, u * sa + v * ca);
    };
    std::vector<JPPipelineValue::Outline> all = pads;
    all.push_back(body.front());
    const cv::Rect2d b = JPStageUtil::bounds(all, px);
    if (b.width <= 0 || b.height <= 0) {
        detail = "its footprint has no size";
        return false;
    }
    const int tw = int(JPStageUtil::javaRound(std::max(b.width * kMarginFactor, b.width + 2 * kMinimumMarginPx)));
    const int th = int(JPStageUtil::javaRound(std::max(b.height * kMarginFactor, b.height + 2 * kMinimumMarginPx)));
    const cv::Scalar black = cv::Scalar::all(0), white = cv::Scalar::all(255);
    cv::Mat templ(th, tw, CV_8UC3, pick ? white : black);
    auto centred = [&](const JPPipelineValue::LocationMm& m) { return px(m) + cv::Point2d(tw / 2.0, th / 2.0); };
    if (!pick) JPStageUtil::fill(templ, body, black, centred);
    JPStageUtil::fill(templ, pads, pick ? black : white, centred);
    if (pick) JPStageUtil::fill(templ, body, black, centred);
    const int templateDimension = int(std::sqrt(double(tw) * tw + double(th) * th));
    const int kernel = (templateDimension / 4) | 1;
    cv::GaussianBlur(templ, templ, cv::Size(kernel, kernel), 0);

    // The picture around the place, as the camera would see it there (sub-pixel, bicubic).
    const int dimension = std::max(std::max(tw, th) * kSearchFactor, kLeastSearchPx);
    const double pixelX = (x + pic.offsetX) / pic.unitsPerPixelX, pixelY = (y + pic.offsetY) / pic.unitsPerPixelY;
    const double dx = pixelX - dimension / 2.0, dy = source->rows - (pixelY + dimension / 2.0);
    const cv::Mat shift = (cv::Mat_<double>(2, 3) << 1, 0, -dx, 0, 1, -dy);
    cv::Mat target;
    cv::warpAffine(*source, target, shift, cv::Size(dimension, dimension), cv::INTER_CUBIC, cv::BORDER_CONSTANT, black);
    if (pic.filterTestImage) {
        cv::Mat channel;
        cv::extractChannel(templ, channel, 2);
        templ = channel;
        cv::Mat hsv, masked;
        cv::cvtColor(target, hsv, cv::COLOR_BGR2HSV_FULL);
        cv::inRange(hsv, cv::Scalar(0, pick ? 0 : 1, kMaskLeastValue), cv::Scalar(255, kMaskMostSaturation, 255), masked);
        target = masked;
    }
    cv::Mat result;
    cv::matchTemplate(target, templ, result, cv::TM_CCOEFF_NORMED);

    // The best match: the nearest of the good ones.
    std::optional<double> bestScore;
    double bestDistance = DBL_MAX;
    for (const cv::Point& p : JPStageUtil::matMaxima(result, tolerance.minimumScore * kConsideredShare, DBL_MAX)) {
        const double ox = (p.x + tw / 2.0 - dimension / 2.0) * pic.unitsPerPixelX;
        const double oy = (dimension / 2.0 - (p.y + th / 2.0)) * pic.unitsPerPixelY;
        const double distance = std::hypot(ox, oy), score = result.at<float>(p.y, p.x);
        if (!bestScore || (bestDistance > distance && *bestScore * kNearerShare < score)) {
            bestScore = score;
            bestDistance = distance;
        }
    }
    const double pixelTolerance = std::hypot(pic.unitsPerPixelX, pic.unitsPerPixelY) * (kSlackPx + templateDimension * kSlackShare);
    if (!bestScore) {
        detail = "nothing recognized there";
        return false;
    }
    if (*bestScore < tolerance.minimumScore) {
        detail = "no good match there: the best scores " + std::to_string(*bestScore) + ", under "
               + std::to_string(tolerance.minimumScore) + ", " + std::to_string(bestDistance) + " mm off";
        return false;
    }
    if (bestDistance > tolerance.distanceMm + pixelTolerance) {
        detail = "the match is " + std::to_string(bestDistance) + " mm off, more than the " + std::to_string(tolerance.distanceMm)
               + " mm allowed (+ " + std::to_string(pixelTolerance) + " mm for the pixels)";
        return false;
    }
    detail = "recognized " + std::to_string(bestDistance) + " mm off, scoring " + std::to_string(*bestScore);
    return true;
}

} // inline namespace jf
