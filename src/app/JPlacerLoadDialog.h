// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"
#include "model/JPLaneChoice.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JButton.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// Load as you go (DESIGN.md): the run has placed what is loaded and needs a part no feeder holds. One prompt:
// the part, how it comes (its packaging, the part's turn in the tape), the lane to lay it in (the best free
// one chosen, any other free one offered, what is taken off first said), and, where its stock is kept, which
// lot is being loaded. Continue loads it there and the run goes on; Skip This Part leaves its placements
// unplaced (listed at the end) and goes on; Stop stops the run. Escape and the [x] leave the run paused.
class JPlacerLoadDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 760, kH = 380;

    struct Actions {
        std::function<void(const std::string& laneId, const std::string& lotUuid)> loaded;   // lane empty: none free
        std::function<void()> skip;
        std::function<void()> stop;
    };
    JPlacerLoadDialog(const JPConfiguration& config, const std::string& partId, std::vector<JPLaneChoice::Lane> lanes,
                      Actions actions, JGpuHal& hal, int sx, int sy, NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    void describe();

    std::string                    m_partId, m_packaging;
    std::vector<JPLaneChoice::Lane> m_lanes;
    std::vector<std::string>       m_lotUuids;   // the Lot choice's, after None
    Actions                        m_actions;

    std::unique_ptr<JContainer>       m_content;
    JLabel*                           m_what = nullptr;
    JLabel*                           m_how = nullptr;
    JComboBox*                        m_lane = nullptr;
    JComboBox*                        m_lot = nullptr;
    std::unique_ptr<JDialogButtonBox> m_buttons;
};

} // inline namespace jf
