// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPGroupFrame.h"

#include <j/core/JButton.h>
#include <j/core/JLabel.h>
#include <j/core/JSpinBox.h>

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
    // The button that goes on, offered or not (OpenPnP's proceed enabled).
    void setProceedEnabled(bool on) { m_proceed->setEnabled(on); }
    // A number to set beside the buttons (OpenPnP's Detection Diameter, for a step that sizes what is
    // looked for): its label, value and range; `changed` told each time it is set. hideNumber: none.
    void showNumber(const std::string& label, int value, int min, int max, std::function<void(int)> changed);
    void hideNumber();
    // How tall it is shown; and, at `width`, with room for all its text
    // folded (for a narrow place: a camera's panel), and its text that tall.
    static float height();
    float heightFor(float width);
    // How tall the number's row is now (none while not asked for).
    float numberHeight() const;

private:
    JLabel*               m_text = nullptr;
    JButton*              m_cancel = nullptr;
    JButton*              m_proceed = nullptr;
    JContainer*           m_numberRow = nullptr;
    JLabel*               m_numberLabel = nullptr;
    JSpinBox*             m_number = nullptr;
    std::function<void(int)> m_onNumber;
    std::function<void()> m_onCancel, m_onProceed;
};

} // inline namespace jf
