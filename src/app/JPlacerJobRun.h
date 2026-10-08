// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerJob.h"
#include "JPlacerJobHost.h"
#include "tasks/JPCellJobMachine.h"
#include "JPlacerMachine.h"
#include "JPlacerSignalers.h"

#include "tasks/JPJobProcessor.h"
#include "ui/JPJobPanel.h"

#include <j/app/JAppWindow.h>

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

inline namespace jf {

// Running the job, as OpenPnP's JobPanel runs it: Start (then Pause while it
// runs, Resume while paused), Step (one step, then paused) and Stop (the
// nozzles emptied and the head parked). The job processor works on a
// thread of its own; what it changes in the job, the boards and the feeders
// is changed on the screen's thread, which it waits for. A failure pauses
// the job and is shown ("Job Error"), its board, placement, part or feeder
// chosen where it is shown; Resume goes on from there. Also the Job tab's
// Fiducial Check of one board or panel.
class JPlacerJobRun {
public:
    JPlacerJobRun(JAppWindow& window, JPlacerJob& job, JPlacerMachine& machine, JPJobPanel& panel);
    // A run under way is stopped where it is (it is not parked) and waited for.
    ~JPlacerJobRun();
    // The machine a job runs on (for a script's vision, off the screen's thread).
    JPCellJobMachine& jobMachine();

    JPlacerJobRun(const JPlacerJobRun&)            = delete;
    JPlacerJobRun& operator=(const JPlacerJobRun&) = delete;

    bool running() const { return m_state != JPJobPanel::RunState::Stopped; }
    // OpenPnP's submitUiMachineTask: `work` with the machine, on the job's
    // thread while no job step runs; its failure (false, why) shown as an
    // Error. Refused while the job runs.
    // False when it is refused (a job running, the machine not connected or homed; the status line says so).
    bool machineTask(std::function<bool(JPJobMachine&, const std::function<void(const std::function<void()>&)>& onMain,
                                        std::string& why)>
                         work);

    // A failure's source, to be chosen where it is shown (the Feeders tab's
    // feeder, the Parts tab's part; a board and placement are chosen here).
    std::function<void(const JPJobProcessor::Failure&)> showSource;
    // A placement placed: the placed counts and tables shown again.
    std::function<void()> onPlaced;
    // The Job tab's Fiducial Check of one board or panel (also the job viewer's):
    // it set by its fiducials (where it is straight in the job), the camera taken to it.
    void fiducialCheck(JPPlacementsHolderLocation* location);
    // Job > Check Job…: the job's data checked (JPJobCheck), as Start checks it.
    void checkJob();

private:
    void startPauseResume();
    void step();
    void stop();
    // Starts the processor anew (asking to reset a job all placed), then runs it.
    void start(JPJobPanel::RunState as);
    // The run on the worker: steps while Running (one while Pausing).
    void run();
    void setState(JPJobPanel::RunState s);
    // `fn` on the screen's thread, waited for (not while quitting).
    void onMain(const std::function<void()>& fn);
    void post(std::function<void()> fn);
    bool ask(const std::string& question);
    void join();
    // The run record (JPRunStore): begun with the boards at their revisions; ended (Finished or Stopped) and
    // its parts written to the stock's ledger (JPRunLedger).
    void beginRun();
    // The nozzle tips the machine's nozzles can load.
    std::vector<std::string> machineTipIds() const;
    void endRun(JPRunStore::Outcome outcome);

    JAppWindow&                          m_window;
    JPlacerJob&                          m_job;
    JPlacerMachine&                      m_machine;
    JPlacerJobHost                       m_host;   // the machine as the job machine's host
    JPJobPanel&                          m_panel;
    const std::thread::id                m_mainThread;   // the screen's
    std::unique_ptr<JPCellJobMachine>   m_jobMachine;
    std::unique_ptr<JPJobProcessor>      m_processor;
    std::thread                          m_worker;
    std::atomic<JPJobPanel::RunState>    m_state { JPJobPanel::RunState::Stopped };
    std::atomic<bool>                    m_quitting { false };
    bool                                 m_stepToMotion = true;   // Step Next Motion
    JPlacerSignalers                     m_signalers { m_machine };
    bool                                 m_signalSetUp = false;   // a job set up: Stopped to be signalled first
    std::shared_ptr<bool>                m_alive = std::make_shared<bool>(true);
    std::string                          m_runUuid;   // the run under way; empty: none
    std::set<std::string>                m_warnedLots;   // lots this run has said are running out (job thread)
};

} // inline namespace jf
