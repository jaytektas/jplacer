// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPFrame.h"

#include <j/core/JWidget.h>
#include <j/graphics/GpuHal.h>

#include <functional>
#include <memory>
#include <optional>

inline namespace jf {

// OpenPnP's MatView: a picture fitted to the widget, its shape kept and
// centred on a dark ground; the picture's pixel under the mouse is told as
// the mouse moves over it.
class JPMatView : public JWidget {
public:
    JPMatView(JSceneGraph& graph, JGpuHal* hal);
    ~JPMatView() override;

    // The picture to show; null: none.
    void setImage(std::shared_ptr<const JPFrame> image);
    const JPFrame* image() const { return m_image.get(); }
    // The picture's pixel at a window place (MatView.scalePoint), or none off it.
    std::optional<std::pair<int, int>> pixelAt(float mx, float my) const;

    // The mouse over the picture: its pixel.
    std::function<void(int x, int y)> onHover;

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;
    void handleMouseMove(float mx, float my) override;

private:
    // Where the picture is drawn.
    JRect picture() const;

    JGpuHal*                       m_hal = nullptr;
    std::shared_ptr<const JPFrame> m_image;
    TextureHandle                  m_tex = kNullTexture;
};

} // inline namespace jf
