// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPipelineCamera.h"

#include "JPCameraLook.h"

#include "pipeline/JPStraightPicture.h"

#include <opencv2/imgproc.hpp>

inline namespace jf {

JPCameraCalibration JPPipelineCamera::give(JPPipeline::Context& ctx, JPCameraFeed& feed, const JPCameraCalibration& raw, View view) {
    JPCameraFeed* f = &feed;
    const std::shared_ptr<const JPStraightPicture> straight =
        raw.valid ? JPStraightPicture::of(raw, feed.config().looksUp, feed.config().showAll) : nullptr;
    // A picture, settled unless the stage says to skip it; straightened where the camera is calibrated.
    ctx.capture = [f, straight](const std::string& settle, const std::string&, cv::Mat& bgr, std::string& why) {
        JPFrame frame;
        if (settle == "Skip") f->latest(frame, 0);
        else if (!JPCameraLook::settled(*f, frame, why)) return false;
        if (frame.width <= 0) {
            why = f->config().name + " gives no picture";
            return false;
        }
        cv::Mat rgba(frame.height, frame.width, CV_8UC4, frame.rgba.data());
        if (!straight) {
            cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
            return true;
        }
        cv::Mat taken;
        cv::cvtColor(rgba, taken, cv::COLOR_RGBA2BGR);
        if (!straight->straighten(taken, bgr)) {
            why = f->config().name + " gives " + std::to_string(frame.width) + "x" + std::to_string(frame.height) +
                  " pictures; it is calibrated for " + std::to_string(straight->calibration().width) + "x" +
                  std::to_string(straight->calibration().height);
            return false;
        }
        return true;
    };
    const JPCameraCalibration cal = straight ? straight->calibration() : raw;
    ctx.cameraWidth = cal.width;
    ctx.cameraHeight = cal.height;
    ctx.pixelsPerMmX = cal.valid ? cal.scaleX() : 0;
    ctx.pixelsPerMmY = cal.valid ? cal.scaleY() : 0;
    ctx.pictureMirrored = cal.valid && cal.pictureMirrored();
    ctx.pictureTurnDeg = cal.valid ? cal.pictureTurnDeg() : 0;
    ctx.locationToPixel = nullptr;
    if (cal.valid && view)
        ctx.locationToPixel = [cal, view](double x, double y, double& px, double& py) {
            double vx, vy;
            return view(vx, vy) && cal.pixelFor(x, y, vx, vy, px, py);
        };
    return cal;
}

} // inline namespace jf
