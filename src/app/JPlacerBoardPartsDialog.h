// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"
#include "model/JPPartChoice.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JButton.h>
#include <j/core/JCheckBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDataGrid.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// A board's parts, one row each (DESIGN.md, the matching wizard): its
// placements, what the files said (value, footprint, MPN), what it is now
// (the library's part, or to be chosen) and the library's best
// match with why. Choose… (or a double-click, or Return) opens the part
// picker for it; Use Best Matches takes, for each part still to be chosen,
// its best match where the evidence is strong (an MPN, a supplier's part
// number, OpenPnP's footprint-value name, the value by name, or the value and
// the size), never one on its value alone. Only Those to Be Chosen narrows the
// list. A choice is made as the placements' Part cell makes it (`apply`).
// A part whose library part the library has not got (deleted since, or
// named by a board from another library) says so, to be chosen again.
class JPlacerBoardPartsDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 1000, kH = 620;

    using Apply = std::function<void(const std::string& placementId, const JPPartChoice& choice)>;
    using Pick = std::function<void(const std::string& placementId, std::function<void(const JPPartChoice&)> chosen)>;
    JPlacerBoardPartsDialog(const JPConfiguration& config, const JPBoard& board, Apply apply, Pick pick, JGpuHal& hal,
                            int sx, int sy, NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;
    void onMouse(float mx, float my, bool pressed, bool released, bool held) override;

private:
    void fill();
    void chooseSelected();
    void useBest();

    const JPConfiguration&   m_config;
    const JPBoard&           m_board;
    Apply                    m_apply;
    Pick                     m_pick;
    std::shared_ptr<bool>    m_alive = std::make_shared<bool>(true);
    std::vector<std::string> m_rowKeys;   // each row's board part
    int                      m_activated = -1;
    bool                     m_refill = false;

    std::unique_ptr<JContainer>       m_content;
    JLabel*                           m_summary = nullptr;
    JDataGrid*                        m_list = nullptr;
    JCheckBox*                        m_onlyToChoose = nullptr;
    std::unique_ptr<JDialogButtonBox> m_buttons;
    JButton*                          m_choose = nullptr;
    JButton*                          m_best = nullptr;
    void enableFor(int row);
};

} // inline namespace jf
