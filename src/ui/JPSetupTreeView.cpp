// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSetupTreeView.h"

#include "JPOpenPnpIcons.h"

#include <j/core/JStyle.h>

#include <iterator>

inline namespace jf {

namespace {

// The icons OpenPnP's tree shows (getPropertySheetHolderIcon), in the order of their numbers.
constexpr const char* kIcons[] = { "capture-nozzle", "capture-camera", "driver", "axis-cartesian", "axis-rotate" };
// An icon as tall as this share of its row, drawn at twice that and shown smaller to stay sharp.
constexpr float kRowShare = 0.8f;
constexpr float kOversample = 2.f;

} // namespace

int JPSetupTreeView::iconOf(const std::string& name) {
    for (size_t i = 0; i < std::size(kIcons); ++i)
        if (name == kIcons[i]) return int(i) + 1;
    return 0;
}

float JPSetupTreeView::drawNodeIcon(JPrimitiveBuffer& buf, const JTreeViewNode& node, float x, float rowY, float rowH) {
    if (node.icon < 1 || size_t(node.icon) > std::size(kIcons)) return 0;
    const float side = rowH * kRowShare;
    if (JPOpenPnpIcons* icons = JPOpenPnpIcons::instance()) {
        const TextureHandle tex = icons->texture(kIcons[node.icon - 1], int(side * kOversample));
        if (tex != kNullTexture) buf.pushImage(x, rowY + (rowH - side) * 0.5f, side, side, tex);
    }
    return side + JStyle::current().spacing;
}

} // inline namespace jf
