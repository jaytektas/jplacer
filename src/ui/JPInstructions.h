// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPGroupFrame.h"

#include <j/core/JButton.h>
#include <j/core/JLabel.h>

#include <functional>
#include <string>

inline namespace jf {

// OpenPnP's instructions panel (MainFrame.showInstructions): a step's title,
// what to do, and Cancel and the button that goes on (Next, Finish), for a
// process the person works through while still using the rest of the
// window (jogging the camera, choosing rows).
class JPInstructions : public JPGroupFrame {
public:
    explicit JPInstructions(JSceneGraph& graph);

    void set(const std::string& title, const std::string& text, const std::string& proceedLabel,
             std::function<void()> onCancel, std::function<void()> onProceed);
    // How tall it is shown.
    static float height();

private:
    JLabel*               m_text = nullptr;
    JButton*              m_cancel = nullptr;
    JButton*              m_proceed = nullptr;
    std::function<void()> m_onCancel, m_onProceed;
};

} // inline namespace jf
