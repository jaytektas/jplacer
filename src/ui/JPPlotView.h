// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "setup/JPPlot.h"

#include <j/core/JWidget.h>

#include <memory>

inline namespace jf {

// Draws a JPPlot: lines and scatter plots through the framework's chart, a
// map as a grid of cells coloured cool to hot by the spots nearest each, kept
// to the area's shape.
class JPPlotView : public JWidget {
public:
    JPPlotView(JSceneGraph& graph, std::shared_ptr<const JPPlot> plot);

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    void drawChart(JPrimitiveBuffer& buf, const JRect& b) const;
    void drawMap(JPrimitiveBuffer& buf, const JRect& b) const;

    std::shared_ptr<const JPPlot> m_plot;
};

} // inline namespace jf
