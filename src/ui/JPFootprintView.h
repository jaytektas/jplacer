// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "library/JPFootprint.h"

#include <j/core/JWidget.h>

#include <optional>

inline namespace jf {

// A picture of a footprint, fitted to the view with its shape kept, Y up,
// pin 1's dot on it (JPFootprintDraw); empty without one.
class JPFootprintView : public JWidget {
public:
    explicit JPFootprintView(JSceneGraph& graph);

    void show(std::optional<JPFootprint> footprint);

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    std::optional<JPFootprint> m_footprint;
};

} // inline namespace jf
