// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "ui/JPDoubleClick.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>
#include <j/core/JListView.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's ClassSelectionDialog: a description over the classes (their
// simple names), one to Accept (or double-click), or Cancel. `onChosen`
// has the class's full name.
class JPlacerClassSelectionDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 400, kH = 400;

    JPlacerClassSelectionDialog(const std::string& title, const std::string& description, std::vector<std::string> classes,
                                std::function<void(std::string)> onChosen, JGpuHal& hal, int sx, int sy,
                                NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    void accept(int index);

    std::vector<std::string>          m_classes;
    std::function<void(std::string)>  m_onChosen;
    std::unique_ptr<JLabel>           m_description;
    std::unique_ptr<JListView>        m_list;
    JPDoubleClick                     m_clicks;
    std::unique_ptr<JDialogButtonBox> m_buttons;
    JButton*                          m_accept = nullptr;
};

} // inline namespace jf
