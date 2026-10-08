// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPJobPlan.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JButton.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDataGrid.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>

#include <functional>
#include <memory>
#include <vector>

inline namespace jf {

// Job > Plan… (DESIGN.md, Planner view; JPJobPlan): the job's part groups in the order a run places them, each
// with its placements left, height, package and where its part comes from (the feeder holding it, or a load
// the run asks for); the sort rule (choosing one sorts all again), Move Up and Move Down to place a group by
// hand, Sort Again to drop what was moved by hand. Kept with the job as it is changed.
class JPlacerPlanDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 900, kH = 560;

    // `changed`: the plan changed (the job to be saved).
    JPlacerPlanDialog(JPConfiguration& config, JPJob& job, std::function<void()> changed, JGpuHal& hal, int sx, int sy,
                      NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    void fill(int select);
    void move(int by);

    JPConfiguration&                  m_config;
    JPJob&                            m_job;
    std::function<void()>             m_changed;
    std::vector<JPJobPlan::Group>     m_groups;

    std::unique_ptr<JContainer>       m_content;
    JLabel*                           m_summary = nullptr;
    JComboBox*                        m_sort = nullptr;
    JDataGrid*                        m_list = nullptr;
    JButton*                          m_up = nullptr;
    JButton*                          m_down = nullptr;
    JButton*                          m_again = nullptr;
    std::unique_ptr<JDialogButtonBox> m_buttons;
};

} // inline namespace jf
