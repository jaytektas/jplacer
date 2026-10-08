// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPJobCheck.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JContainer.h>
#include <j/core/JDataGrid.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>

#include <functional>
#include <memory>
#include <vector>

inline namespace jf {

// The job's data checked (Job > Check Job…, and Start when there is something to say; JPJobCheck): each thing
// to do once, stops first, with what it is about and where it is put right. Opened by Start, Start Anyway runs
// the job when nothing stops it (things to check or note are the operator's to judge); Cancel does not.
class JPlacerJobCheckDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 1000, kH = 520;
    static constexpr size_t   kNamed = 10;         // placements or parts named in a row, before "and n more"
    static constexpr size_t   kDetailNamed = 60;   // and under the list, for the row chosen

    // `start` set: opened by Start, offering Start Anyway (when nothing stops the run).
    JPlacerJobCheckDialog(std::vector<JPJobCheck::Item> items, std::function<void()> start, JGpuHal& hal, int sx, int sy,
                          NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    std::unique_ptr<JContainer>       m_content;
    JLabel*                           m_summary = nullptr;
    JDataGrid*                        m_list = nullptr;
    JLabel*                           m_detail = nullptr;   // the chosen row in full
    std::vector<JPJobCheck::Item>     m_items;
    std::unique_ptr<JDialogButtonBox> m_buttons;
};

} // inline namespace jf
