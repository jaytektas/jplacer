// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"

#include <chrono>
#include <functional>
#include <string>

inline namespace jf {

// OpenPnP's ImageCamera: a camera that shows the part of one big picture (a
// PNG of the machine's table, say) under where it is looking, so jobs and
// vision can be tried without a camera. The picture is `unitsPerPixelX` x
// `unitsPerPixelY` mm a pixel, its bottom left at the machine's origin less
// `offsetX`, `offsetY`; the view turned by `rotation` degrees, scaled by
// `scale` and mirrored (`flipped`) as OpenPnP's simulated ones are.
class JPImageSource : public JPCaptureSource {
public:
    struct Settings {
        std::string path;
        int         width = 640, height = 480;
        double      fps = 10;
        double      unitsPerPixelX = 0.04, unitsPerPixelY = 0.04;
        double      offsetX = 0, offsetY = 0;
        double      rotation = 0, scale = 1;
        bool        flipped = false;
    };
    // Where the camera is looking (machine X, Y); false when unknown.
    using ViewProvider = std::function<bool(double&, double&)>;
    JPImageSource(std::string name, Settings settings, ViewProvider view);

    bool open(std::string& error) override;
    void close() override {}
    std::vector<JPCaptureMode> modes() const override;
    bool start(const JPCaptureMode& mode, std::string& error) override;
    bool grab(JPFrame& frame, int timeoutMs, std::string& error) override;
    std::string describe() const override { return m_name + " (image " + m_settings.path + ")"; }

    // The view at (x, y): what grab shows, without waiting.
    void render(double x, double y, JPFrame& frame) const;

private:
    std::string  m_name;
    Settings     m_settings;
    ViewProvider m_view;
    JPFrame      m_image;
    std::chrono::steady_clock::time_point m_next {};
};

} // inline namespace jf
