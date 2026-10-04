// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/app/JDialogWindow.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>
#include <j/core/JListView.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's ExistingBoardOrPanelDialog: the boards (or panels) known, one
// to choose (OK), or Browse for a file of them (".board.xml" or
// ".panel.xml"), or Cancel. `onChosen` has the file.
class JPlacerExistingHolderDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 600, kH = 400;

    // `what`: "board" or "panel".
    JPlacerExistingHolderDialog(const std::string& title, const std::string& what, std::vector<std::string> files,
                                std::function<void(std::string)> onChosen, JGpuHal& hal, int sx, int sy,
                                NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    std::vector<std::string>          m_files;
    std::function<void(std::string)>  m_onChosen;
    std::unique_ptr<JLabel>           m_prompt;
    std::unique_ptr<JListView>        m_list;
    std::unique_ptr<JDialogButtonBox> m_buttons;
    JButton*                          m_ok = nullptr;
};

} // inline namespace jf
