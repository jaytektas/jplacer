// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"
#include "tasks/JPJobMachine.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>
#include <j/core/JSpinBox.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

inline namespace jf {

// OpenPnP's Program Feeder Slot Wizard (Photon feeders): first, take every
// Photon feeder out of the machine (Next); then put a feeder into the slot
// whose address is shown (it can be changed): a feeder not yet set up is
// asked for on the bus over and over, given that address as its slot's
// (its floor address), set up there, and the address moved on to the next,
// until Finish (or the highest address, 254). The search's highest address
// is raised to the last programmed.
class JPlacerPhotonSlotsDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 450, kH = 300;
    using OnMain = std::function<void(const std::function<void()>&)>;
    using Work = std::function<bool(JPJobMachine&, const OnMain&, std::string&)>;

    // `runTask`: the work run on the machine's thread (JPlacerJobRun::machineTask); false when refused.
    JPlacerPhotonSlotsDialog(JPConfiguration& config, std::function<bool(Work)> runTask, JGpuHal& hal, int sx, int sy,
                             NativeWinHandleType parent);
    ~JPlacerPhotonSlotsDialog();

protected:
    void layout(float w, float h) override;

private:
    // What the programming loop and the dialog share: stop asked, the dialog still there.
    struct Shared {
        std::atomic<bool> stop { false };
        bool              alive = true;   // the main thread's
    };
    void next();
    void start();
    void setStatus(const std::string& text) { m_status->setText(text); }

    JPConfiguration&                  m_config;
    std::function<bool(Work)>         m_runTask;
    std::shared_ptr<Shared>           m_shared = std::make_shared<Shared>();
    bool                              m_programming = false;
    std::unique_ptr<JLabel>           m_text, m_addressLabel, m_status;
    std::unique_ptr<JSpinBox>         m_address;
    std::unique_ptr<JDialogButtonBox> m_buttons;
    JButton*                          m_next = nullptr;
};

} // inline namespace jf
