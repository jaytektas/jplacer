// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JControl.h>

#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// A slider standing up, as OpenPnP's jog Distance and Speed: its value
// (0 at the bottom .. 1 at the top) shown above it, a track with marks and
// their labels beside it. With `snap`, it only rests on the marks (a choice
// of steps); else anywhere between.
class JPVerticalSlider : public JControl {
public:
    // `marks`: (where, 0..1; its label), bottom first.
    JPVerticalSlider(JSceneGraph& graph, std::vector<std::pair<double, std::string>> marks, bool snap);

    double value() const { return m_value; }
    void setValue(double v);
    // The text above the track (the value, as its owner says it).
    void setCaption(const std::string& text);
    // How wide it needs to be for its labels.
    float naturalWidth() const;

    jf::JSignal<double> onValueChanged;

    void handleMousePress(float mx, float my) override;
    void handleMouseMove(float mx, float my) override;
    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    // The track's top and bottom in the widget's box.
    void track(float& top, float& bottom) const;
    void follow(float my);

    std::vector<std::pair<double, std::string>> m_marks;
    bool        m_snap;
    double      m_value = 0;
    std::string m_caption;
};

} // inline namespace jf
