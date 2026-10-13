// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPLcscFetch.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JDataGrid.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>
#include <j/core/JProgressBar.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

class JButton;
class JPConfiguration;

// Packages given their footprints and dimensions from LCSC (JPLcscFetch), in view: how far it has got, the
// package being asked for, and what went wrong for each that did not get everything. Cancel stops it after the
// one being asked for (what was given is kept); once done, Retry asks again for those that went wrong, and Close
// ends it. With nothing gone wrong it closes by itself, `onDone` saying what was given.
class JPlacerLcscFetchDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 680, kH = 400;

    // `onChanged`: packages were given something (once a run, on the screen's thread); `onDone`: the run's summary.
    JPlacerLcscFetchDialog(JPConfiguration& config, std::string cacheDir, std::vector<JPLcscFetch::Ask> asks,
                           std::function<void()> onChanged, std::function<void(const std::string&)> onDone, JGpuHal& hal,
                           int sx, int sy, NativeWinHandleType parent);
    ~JPlacerLcscFetchDialog() override;

protected:
    void layout(float w, float h) override;

private:
    struct Problem {
        JPLcscFetch::Ask ask;
        std::string      why;
    };
    void run(std::vector<JPLcscFetch::Ask> asks);
    void answered(size_t index, size_t of, const JPLcscFetch::Ask& ask, const JPLcscFetch::Answer& answer);
    void finished();
    void retry();
    void showProblems();

    JPConfiguration&                            m_config;
    std::string                                 m_cacheDir;
    std::function<void()>                       m_onChanged;
    std::function<void(const std::string&)>     m_onDone;
    std::unique_ptr<JLabel>                     m_what, m_note, m_problemsLabel;
    std::unique_ptr<JProgressBar>               m_bar;
    std::unique_ptr<JDataGrid>                  m_problems;
    std::unique_ptr<JDialogButtonBox>           m_buttons;
    JButton*                                    m_retry = nullptr;
    JButton*                                    m_close = nullptr;
    std::vector<Problem>                        m_found;   // this run's
    int                                         m_given = 0, m_asked = 0, m_kept = 0;   // m_kept: given from the cache alone
    bool                                        m_running = false, m_changed = false;
    std::shared_ptr<std::atomic<bool>>          m_cancel;  // the running thread's
    std::shared_ptr<bool>                       m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
