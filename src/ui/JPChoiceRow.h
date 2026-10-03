// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JContainer.h>
#include <j/core/JToggleButton.h>
#include <j/core/Signal.h>

#include <string>
#include <vector>

inline namespace jf {

// One choice out of a few, every option in view: a row (or rows) of toggle
// buttons of which exactly one is down. For choices short enough to read at a glance
// (a tool, a jog step, a speed), where a drop-down hides what the options are.
class JPChoiceRow : public JContainer {
public:
    // `perRow` > 0: the options in rows of that many, stacked (a narrow
    // place), each as wide as the widest label; 0: all on one row.
    JPChoiceRow(JSceneGraph& graph, const std::vector<std::string>& labels, int chosen, int perRow = 0);

    int chosen() const { return m_chosen; }
    void choose(int index);
    // Every option on or off at once (a container's own enabled state does
    // not reach the buttons in it).
    void setChoicesEnabled(bool enabled);

    JSignal<int> onChosen;

private:
    std::vector<JToggleButton*> m_buttons;
    int                         m_chosen = -1;
    bool                        m_updating = false;
};

} // inline namespace jf
