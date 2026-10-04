// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPFrame.h"

#include <j/core/JWidget.h>
#include <j/graphics/GpuHal.h>

#include <memory>

inline namespace jf {

// A picture shown whole in a sunken box, its shape kept and centred (as
// OpenPnP shows a drag feeder's template image): empty, just the box.
class JPImageBox : public JWidget {
public:
    JPImageBox(JSceneGraph& graph, JGpuHal* hal);
    ~JPImageBox() override;

    // The picture to show (the same one again changes nothing); null: none.
    void setImage(std::shared_ptr<const JPFrame> image);

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    JGpuHal*                        m_hal = nullptr;
    std::shared_ptr<const JPFrame>  m_image;
    TextureHandle                   m_tex = kNullTexture;
};

} // inline namespace jf
