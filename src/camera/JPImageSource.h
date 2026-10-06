// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"
#include "machine/JPCameraConfig.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string>

inline namespace jf {

// OpenPnP's ImageCamera: a camera that shows the part of one big picture (a
// PNG of the machine's table, say) under where it is looking, so jobs and
// vision can be tried without a camera. The picture is `unitsPerPixelX` x
// `unitsPerPixelY` mm a pixel, its bottom left at the machine's origin less
// `offsetX`, `offsetY`; the view turned by `rotation` degrees, scaled by
// `scale` and mirrored (`flipped`) as OpenPnP's simulated ones are. As
// OpenPnP's, a Simulated Calibration Rig: two 1 mm white fiducials (none
// where not set), the secondary at another height, so smaller as a lens of
// `focalLengthMm` over a sensor `sensorDiagonalMm` across sees it, blurred,
// and moved by the camera's tilt; and the lens's `distortion` (%, barrel
// when positive) and the camera tilted `yRotation` degrees about Y.
class JPImageSource : public JPCaptureSource {
public:
    struct Settings {
        std::string path;
        int         width = 640, height = 480;
        double      fps = 10;
        double      unitsPerPixelX = JPCameraConfig::kDefaultImageUnitsPerPixel, unitsPerPixelY = JPCameraConfig::kDefaultImageUnitsPerPixel;
        double      offsetX = 0, offsetY = 0;
        double      rotation = 0, scale = 1;
        bool        flipped = false;
        double      distortion = 0, yRotation = 0;
        double      focalLengthMm = 6, sensorDiagonalMm = 4.4;
        struct Fiducial { double x = 0, y = 0, z = 0; };
        std::optional<Fiducial> primaryFiducial, secondaryFiducial;
    };
    // Where the camera is looking (machine X, Y); false when unknown.
    using ViewProvider = std::function<bool(double&, double&)>;
    JPImageSource(std::string name, Settings settings, ViewProvider view);

    bool open(std::string& error) override;
    void close() override {}
    std::vector<JPCaptureMode> modes() const override;
    bool start(const JPCaptureMode& mode, std::string& error) override;
    bool grab(JPFrame& frame, int timeoutMs, std::string& error) override;
    bool canFreeze() const override { return false; }   // a still machine: the same picture
    std::string describe() const override { return m_name + " (image " + m_settings.path + ")"; }

    // The view at (x, y): what grab shows, without waiting.
    void render(double x, double y, JPFrame& frame) const;

private:
    // A fiducial `at` (mm from where the camera looks) drawn as OpenPnP's
    // drawFiducial does, at `uppX` x `uppY` mm a pixel; blurred, the
    // secondary's focal blur.
    void drawFiducial(JPFrame& frame, double atX, double atY, double uppX, double uppY, bool blurred) const;
    // OpenPnP's lens distortion and Y tilt, the picture remapped.
    void distort(JPFrame& frame, double cameraDistance) const;

    std::string  m_name;
    Settings     m_settings;
    ViewProvider m_view;
    JPFrame      m_image;
    std::chrono::steady_clock::time_point m_next {};
};

} // inline namespace jf
