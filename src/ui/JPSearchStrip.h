// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JWidget.h>

#include <vector>

inline namespace jf {

// How a search over a row of addresses is going, as OpenPnP's
// FeederSearchProgressBar: a cell for each, side by side across the width,
// each coloured by its state (not yet asked, being asked, found, missing),
// framed.
class JPSearchStrip : public JWidget {
public:
    enum State { Unknown = 0, Searching = 1, Found = 2, Missing = 3 };

    explicit JPSearchStrip(JSceneGraph& graph) : JWidget(graph, "JPSearchStrip") {}

    // One state a cell, in order.
    void setStates(std::vector<int> states);

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    std::vector<int> m_states;
};

} // inline namespace jf
