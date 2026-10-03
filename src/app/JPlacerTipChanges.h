// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCell.h"

#include <j/app/JAppWindow.h>

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

inline namespace jf {

// Changing the tip on a nozzle (the Jog panel's tip menu): the tip on it now
// unloaded first, then the one asked for loaded, each by its changer steps
// (JPTipChanger) on a thread of its own. With "every step", the person is
// asked before each step runs, and can stop. What is on the nozzle is said
// once each half is done (`setTip`), and never guessed: stopped or failed, the
// person looks and says which tip is on it (setting it by hand moves nothing).
class JPlacerTipChanges {
public:
    // `setTip(nozzleId, tipId)`: the tip now on a nozzle ("" none), to keep.
    JPlacerTipChanges(JAppWindow& window, JPCell& cell, std::function<void(const std::string&, const std::string&)> setTip);
    // Stops a change under way at its next step (a question waiting is
    // answered no), then waits for it.
    ~JPlacerTipChanges();

    JPlacerTipChanges(const JPlacerTipChanges&)            = delete;
    JPlacerTipChanges& operator=(const JPlacerTipChanges&) = delete;

    // Put `tipId` on `nozzleId` ("" to take its tip off). Refused (the status
    // bar says why) while another change runs, or when it cannot be done.
    void change(const std::string& nozzleId, const std::string& tipId, bool everyStep);
    bool busy() const { return m_busy; }

private:
    // On the main thread: a question for the person; the answer to `answer`.
    void ask(const std::string& question, std::shared_ptr<std::promise<bool>> answer);
    // What stops this change, in words; empty when it can be done.
    std::string refusal(const std::string& nozzleId, const std::string& tipId) const;

    JAppWindow&                                                  m_window;
    JPCell&                                                      m_cell;
    std::function<void(const std::string&, const std::string&)>  m_setTip;
    std::thread                                                  m_worker;
    std::atomic<bool>                                            m_busy{ false };
    std::atomic<bool>                                            m_quitting{ false };
    std::mutex                                                   m_mutex;
    std::shared_ptr<std::promise<bool>>                          m_waiting;   // a question not yet answered
    std::shared_ptr<bool>                                        m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
