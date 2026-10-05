// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/app/JDialogWindow.h>
#include <j/core/JCheckBox.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDialogButtonBox.h>

#include <functional>
#include <memory>

inline namespace jf {

// OpenPnP's Window > Change Appearance… (its Appearance Settings): the Theme,
// the Font Size (the interface scale) and Alternating Rows Style (every other
// table row shaded). Apply shows the choice; Save shows and keeps it; Cancel
// puts back what was kept. Opened through JAppWindow::openModal.
class JPlacerAppearanceDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 420, kH = 300;

    // `onScale`: an interface scale to show (JPlacerAppearance::scales), for the main window to apply.
    JPlacerAppearanceDialog(std::function<void(double)> onScale, JGpuHal& hal, int sx, int sy, NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    // What is chosen shown; what was kept shown again.
    void show(int theme, double scale, bool rows);
    void keep();

    std::function<void(double)>       m_onScale;
    std::unique_ptr<JContainer>       m_page;
    JComboBox*                        m_theme = nullptr;
    JComboBox*                        m_scale = nullptr;
    JCheckBox*                        m_rows = nullptr;
    std::unique_ptr<JDialogButtonBox> m_buttons;
};

} // inline namespace jf
