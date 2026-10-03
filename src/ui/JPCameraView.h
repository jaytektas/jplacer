// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPViewMark.h"

#include "camera/JPCameraFeed.h"
#include "camera/JPStraightener.h"

#include <j/core/JWidget.h>
#include <j/graphics/GpuHal.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// A camera's live picture, fitted to the widget with its shape kept, and a
// crosshair through the centre: the point the camera is looking at.
//
// The mouse wheel zooms in and out about the centre, so the crosshair stays
// on the point the camera is looking at; zoomed, the zoom is shown in a
// corner. Fitted (1x) is as far out as it goes.
//
// Each new frame becomes a GPU texture on the main thread (the feed's signal
// is re-posted there); the previous texture is released.
class JPCameraView : public JWidget {
public:
    JPCameraView(JSceneGraph& graph, JGpuHal& hal);
    ~JPCameraView() override;

    // The feed to show; null shows nothing. The view does not own it.
    void setFeed(JPCameraFeed* feed);
    // What to say in place of a picture (no camera, why it stopped).
    void setMessage(const std::string& text);
    // Marks drawn over the picture, asked for at each frame drawn.
    void setMarks(std::function<std::vector<JPViewMark>()> marks) { m_marks = std::move(marks); }
    // Show the picture straightened (JPStraightener, drawn as its mesh), or
    // as taken (null). Marks and clicks stay in the picture-as-taken's pixels.
    void setStraightener(std::shared_ptr<const JPStraightener> straightener) {
        m_straight = std::move(straightener);
        invalidate();
    }

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;
    void handleMousePress(float x, float y) override;
    bool handleScroll(float mx, float my, float wheel) override;

    // How far zoomed in: 1 is the picture fitted to the view.
    double zoom() const { return m_zoom; }
    static constexpr double kMostZoom = 64.0;

    // The picture double-clicked, at this pixel of it.
    std::function<void(double px, double py)> onPictureDoubleClicked;

private:
    void showLatest();
    void dropTexture();

    JGpuHal&                           m_hal;
    JPCameraFeed*                      m_feed = nullptr;
    TextureHandle                      m_tex  = kNullTexture;
    int                                m_w = 0, m_h = 0;
    uint64_t                           m_have = 0;
    JPFrame                            m_frame;
    std::string                        m_message;
    std::function<std::vector<JPViewMark>()> m_marks;
    std::shared_ptr<const JPStraightener>    m_straight;
    // Where a pixel of the picture as taken is shown: straightened when straightening.
    bool shown(double rawX, double rawY, double& x, double& y) const;
    // Where the picture was last drawn (widget coordinates) and at what scale,
    // to turn a click into a pixel of it; and the last press, for a double.
    float                              m_picX = 0, m_picY = 0, m_picScale = 0;
    double                             m_zoom = 1.0;
    std::chrono::steady_clock::time_point m_lastPress;
    float                              m_lastPressX = 0, m_lastPressY = 0;
    std::function<void()>              m_unwatch;
    std::shared_ptr<bool>              m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
