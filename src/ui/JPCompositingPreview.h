// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPFootprint.h"
#include "tasks/JPVisionComposite.h"

#include <j/core/JWidget.h>

#include <memory>
#include <optional>

inline namespace jf {

// OpenPnP's VisionCompositingPreview: a package's footprint (body and pads)
// with the shots vision compositing takes of it, each shot's least and
// largest mask radius and its corners' edges, needed shots in one colour and
// optional ones in another. Over a shot, it alone, with what the camera
// sees there and the roaming radius round it; pressed, the pads as
// compositing rectifies and fuses them. An invalid solution is shown
// crossed out. Nothing to show: "no data".
class JPCompositingPreview : public JWidget {
public:
    explicit JPCompositingPreview(JSceneGraph& graph);

    // What to show: the composite, the footprint in mm, what the camera sees (mm) and its roaming radius.
    void setComposite(std::shared_ptr<const JPVisionComposite> composite, const JPFootprint& footprintMm, double cameraWidthMm,
                      double cameraHeightMm, double roamingRadiusMm);
    void clear();

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;
    void handleMousePress(float mx, float my) override;
    void handleMouseRelease(float mx, float my) override;
    void handleMouseMove(float mx, float my) override;
    // The mouse gone (Normal again): nothing under it, nothing pressed.
    void setState(JWidgetState s) override;

private:
    std::shared_ptr<const JPVisionComposite> m_composite;
    JPFootprint                              m_footprint;
    double                                   m_cameraWidth = 0, m_cameraHeight = 0, m_roaming = 0;
    std::optional<std::pair<float, float>>   m_mouse;
    bool                                     m_pressed = false;
};

} // inline namespace jf
