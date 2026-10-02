// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPCameraFeed.h"

#include <j/core/JWidget.h>
#include <j/graphics/GpuHal.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// A camera's live picture, fitted to the widget with its shape kept, and a
// crosshair through the centre: the point the camera is looking at.
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

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

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
    std::function<void()>              m_unwatch;
    std::shared_ptr<bool>              m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
