// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"
#include "model/JPJob.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JContainer.h>
#include <j/core/JDataGrid.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>

#include <memory>

inline namespace jf {

// Job > Shortages… (DESIGN.md, Ready to run; JPShortages): each part the job places, the shortest first: how
// many are left to place, the attrition allowed and where it comes from (set on the part, or measured by its
// ledger), how many are in stock, how many short, where its lots are kept, and, short, where it is bought. It
// never stops a run: the feeders hold the material, and a part may be loaded as the run reaches it.
class JPlacerShortagesDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 1000, kH = 560;

    JPlacerShortagesDialog(const JPConfiguration& config, const JPJob& job, JGpuHal& hal, int sx, int sy,
                           NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    std::unique_ptr<JContainer>       m_content;
    JLabel*                           m_summary = nullptr;
    JDataGrid*                        m_list = nullptr;
    std::unique_ptr<JDialogButtonBox> m_buttons;
};

} // inline namespace jf
