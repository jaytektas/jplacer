// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/app/JDialogWindow.h>
#include <j/core/JCheckBox.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>

#include <functional>
#include <memory>

inline namespace jf {

// Edit > Preferences.
//
// Live-apply: each control writes its setting and saves the file the moment it
// changes, so there is nothing to lose by closing the window and no Apply
// button to forget.
//
// Opened through JAppWindow::openModal, which supplies the trailing hal /
// position / parent arguments.
class JPlacerPreferencesDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 460, kH = 260;   // openModal reads these statically

    // `onCheckNow` runs after the dialog has closed: the answer can take seconds
    // to arrive and is reported in the main window, not in this one.
    JPlacerPreferencesDialog(std::function<void()> onCheckNow,
                             JGpuHal& hal, int sx, int sy, NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    static float pad();

    std::function<void()>             m_onCheckNow;
    std::unique_ptr<JLabel>           m_heading;
    std::unique_ptr<JCheckBox>        m_atStartup;
    std::unique_ptr<JCheckBox>        m_beta;
    std::unique_ptr<JLabel>           m_betaNote;
    std::unique_ptr<JDialogButtonBox> m_buttons;
};

} // inline namespace jf
