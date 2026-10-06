// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JTreeView.h>

#include <string>

inline namespace jf {

// Machine Setup's tree, each part's row with OpenPnP's icon for its kind
// before its name (a nozzle, a camera, a driver, a linear or rotation axis, a feeder),
// as OpenPnP's tree shows them. A node's icon is iconOf(its OpenPnP icon's
// name); 0 for none.
class JPSetupTreeView : public JTreeView {
public:
    explicit JPSetupTreeView(JSceneGraph& graph) : JTreeView(graph, 0.f, 0.f) {}

    // The JTreeViewNode::icon for an OpenPnP icon (JPOpenPnpIcons' name); 0 for none, or not one the tree shows.
    static int iconOf(const std::string& name);

protected:
    float drawNodeIcon(JPrimitiveBuffer& buf, const JTreeViewNode& node, float x, float rowY, float rowH) override;
};

} // inline namespace jf
