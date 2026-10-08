// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JContainer.h>
#include <j/core/JDataGrid.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>

#include <memory>

inline namespace jf {

// Job > Runs… (DESIGN.md, Storage: runs.db): the runs of jobs, newest first: when each started and ended, how it
// ended (finished, stopped, interrupted: jplacer closed while it ran), the job, the boards at their revisions,
// how many placements it placed, and whether its parts are in the stock's ledger.
class JPlacerRunsDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 1000, kH = 520;
    static constexpr size_t   kShown = 200;   // the newest runs listed

    JPlacerRunsDialog(const JPConfiguration& config, JGpuHal& hal, int sx, int sy, NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    std::unique_ptr<JContainer>       m_content;
    JLabel*                           m_summary = nullptr;
    JDataGrid*                        m_list = nullptr;
    std::unique_ptr<JDialogButtonBox> m_buttons;
};

} // inline namespace jf
