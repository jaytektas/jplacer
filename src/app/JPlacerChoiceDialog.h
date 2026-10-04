// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/app/JDialogWindow.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// A question with buttons of its own, as Swing's JOptionPane option dialog
// ("Merge", "Replace", "Cancel"): the buttons in the order given, the one
// at `cancelIndex` taking Escape and the first focus. `onChosen` has the
// index chosen, or -1 when the window is closed.
class JPlacerChoiceDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 520, kH = 300;

    JPlacerChoiceDialog(const std::string& title, const std::string& question, std::vector<std::string> options,
                        int cancelIndex, std::function<void(int)> onChosen, JGpuHal& hal, int sx, int sy,
                        NativeWinHandleType parent);
    ~JPlacerChoiceDialog();

protected:
    void layout(float w, float h) override;

private:
    void choose(int index);

    std::function<void(int)>          m_onChosen;
    bool                              m_answered = false;
    std::unique_ptr<JLabel>           m_question;
    std::unique_ptr<JDialogButtonBox> m_buttons;
};

} // inline namespace jf
