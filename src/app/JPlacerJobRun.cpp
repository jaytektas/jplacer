// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerJobRun.h"

#include "common/JPlacerLog.h"
#include "model/JPBoardLocation.h"
#include "tasks/JPFiducialLocator.h"

#include <j/core/Dialog.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <chrono>

inline namespace jf {

namespace {

using RunState = JPJobPanel::RunState;

// How long the status bar shows what the job does, and how often a wait
// for the screen's thread looks whether it is quitting.
constexpr int kStatusMs = 60000;
constexpr auto kQuitPoll = std::chrono::milliseconds(50);

// Whether every placement of the job is placed (OpenPnP's isAllPlaced).
bool allPlaced(const JPJob& job) {
    for (const JPBoardLocation* l : job.boardLocations()) {
        if (!l->isEnabled() || !l->holder) continue;
        for (const JPPlacement& p : l->holder->placements)
            if (p.type == JPPlacement::Type::Placement && job.retrieveEnabledState(*l, &p) && p.side == l->globalSide()
                && !job.retrievePlacedStatus(*l, p.id))
                return false;
    }
    return true;
}

} // namespace

JPlacerJobRun::JPlacerJobRun(JAppWindow& window, JPlacerJob& job, JPlacerMachine& machine, JPJobPanel& panel)
    : m_window(window), m_job(job), m_machine(machine), m_panel(panel), m_mainThread(std::this_thread::get_id()) {
    m_jobMachine = std::make_unique<JPlacerJobMachine>(
        machine, job.configuration(), [this](const std::function<void()>& fn) { onMain(fn); },
        [this](const std::string& q) { return ask(q); },
        [this](const std::string& s) {
            post([this, s] { m_window.showStatus(s, kStatusMs); });
        });
    panel.onStartPauseResume = [this] { startPauseResume(); };
    panel.onStep = [this] { step(); };
    panel.onStop = [this] { stop(); };
    panel.onFiducialCheck = [this](JPPlacementsHolderLocation* l) { fiducialCheck(l); };
    machine.jobRunning = [this] { return running(); };
    job.running = [this] { return running(); };
    machine.onConnectedChanged = [this](bool connected) { m_panel.setMachineEnabled(connected); };
    panel.setMachineEnabled(machine.isConnected());
    panel.setRunState(RunState::Stopped);
}

JPlacerJobRun::~JPlacerJobRun() {
    // Stopped where it is: the step under way ends, nothing more is done.
    m_quitting = true;
    *m_alive = false;
    m_state = RunState::Stopped;
    join();
    m_machine.jobRunning = nullptr;
    m_machine.onConnectedChanged = nullptr;
    m_job.running = nullptr;
}

void JPlacerJobRun::join() {
    if (m_worker.joinable()) m_worker.join();
}

void JPlacerJobRun::post(std::function<void()> fn) {
    std::weak_ptr<bool> alive = m_alive;
    JMainThreadDispatcher::instance().post([alive, fn = std::move(fn)] {
        if (const auto a = alive.lock(); a && *a) fn();
    });
}

void JPlacerJobRun::onMain(const std::function<void()>& fn) {
    // Already there (a hand-off made inside another): done now.
    if (std::this_thread::get_id() == m_mainThread) {
        fn();
        return;
    }
    if (m_quitting) return;
    auto done = std::make_shared<std::promise<void>>();
    auto result = done->get_future();
    post([fn, done] {
        fn();
        done->set_value();
    });
    // Waited for, but not past quitting (the screen's thread may be waiting for this one).
    while (result.wait_for(kQuitPoll) != std::future_status::ready)
        if (m_quitting) return;
}

bool JPlacerJobRun::ask(const std::string& question) {
    if (m_quitting) return false;
    auto answer = std::make_shared<std::promise<bool>>();
    auto reply = answer->get_future();
    post([question, answer] {
        auto once = std::make_shared<bool>(false);
        auto give = [answer, once](bool yes) {
            if (*once) return;
            *once = true;
            answer->set_value(yes);
        };
        JDialog::confirm("Nozzle Tip Change", question, [give] { give(true); }, [give] { give(false); });
    });
    while (reply.wait_for(kQuitPoll) != std::future_status::ready)
        if (m_quitting) return false;
    return reply.get();
}

void JPlacerJobRun::setState(RunState s) {
    m_state = s;
    m_panel.setRunState(s);
}

void JPlacerJobRun::startPauseResume() {
    switch (m_state.load()) {
        case RunState::Stopped: start(RunState::Running); break;
        case RunState::Paused:
            join();
            setState(RunState::Running);
            run();
            break;
        case RunState::Running: setState(RunState::Pausing); break;
        default: break;
    }
}

void JPlacerJobRun::step() {
    if (m_state == RunState::Stopped) {
        start(RunState::Pausing);
    } else if (m_state == RunState::Paused) {
        join();
        setState(RunState::Pausing);
        run();
    }
}

void JPlacerJobRun::stop() {
    const RunState was = m_state;
    if (was == RunState::Stopped || was == RunState::Stopping) return;
    setState(RunState::Stopping);
    // Running, the worker stops after its step; paused, one is started to stop it.
    if (was == RunState::Paused) {
        join();
        run();
    }
}

void JPlacerJobRun::start(RunState as) {
    JPCell* cell = m_machine.cell();
    if (!cell || !cell->isConnected()) {
        m_window.showStatus("Connect the machine first", kStatusMs);
        return;
    }
    if (!cell->isHomed()) {
        m_window.showStatus("Home the machine first", kStatusMs);
        return;
    }
    std::weak_ptr<bool> alive = m_alive;
    auto go = [this, alive, as] {
        if (const auto a = alive.lock(); !a || !*a) return;
        join();
        JPJobProcessor::Hooks hooks;
        hooks.onMain = [this](const std::function<void()>& fn) { onMain(fn); };
        hooks.status = [this](const std::string& s) {
            post([this, s] { m_window.showStatus(s, kStatusMs); });
        };
        hooks.placed = [this] {
            post([this] {
                m_job.changed();
                if (onPlaced) onPlaced();
            });
        };
        hooks.feedersChanged = [this] {
            post([this] { m_job.configurationChanged(); });
        };
        // OpenPnP's scripting events, run on the job's thread.
        hooks.event = [this](const std::string& event, const JJson& globals, std::string& why) {
            return m_machine.scripting().on(event, globals, why);
        };
        // As the cell's Machine Setup says (Job Processor).
        JPJobProcessorConfig settings;
        if (const JPCell* c = m_machine.cell()) settings = c->config().jobProcessor;
        m_stepToMotion = settings.steppingToNextMotion;
        m_processor = std::make_unique<JPJobProcessor>(m_job.configuration(), m_job.job(), *m_jobMachine, settings, hooks);
        if (const JPCell* c = m_machine.cell()) m_processor->setVision(c->config().vision);
        m_signalSetUp = true;
        setState(as);
        run();
    };
    // As OpenPnP: a job placed already is offered to be placed again.
    if (allPlaced(m_job.job())) {
        JDialogOptions opts;
        opts.okLabel = "Yes";
        opts.cancelLabel = "No";
        JDialog::confirm("Reset placement status?",
                         "All placements have been placed already. Reset all placements before starting job?",
                         [this, alive, go] {
                             if (const auto a = alive.lock(); !a || !*a) return;
                             m_job.job().removeAllPlacedStatus();
                             m_job.changed();
                             go();
                         },
                         go, opts);
        return;
    }
    go();
}

void JPlacerJobRun::run() {
    m_worker = std::thread([this] {
        using JobState = JPSignalerConfig::JobState;
        auto signal = [this](JobState state) {
            m_signalers.signal(state, [this](const std::function<void()>& fn) { onMain(fn); });
        };
        if (m_signalSetUp) {
            m_signalSetUp = false;
            signal(JobState::Stopped);
        }
        for (;;) {
            if (m_quitting) return;
            const RunState s = m_state;
            if (s == RunState::Stopping) {
                m_processor->abort();
                signal(JobState::Stopped);
                post([this] {
                    setState(RunState::Stopped);
                    m_window.showStatus("Job stopped.", kStatusMs);
                });
                return;
            }
            if (s != RunState::Running && s != RunState::Pausing) return;
            JPJobProcessor::Failure f;
            const int motionsBefore = m_jobMachine->motions();
            signal(JobState::Running);
            const JPJobProcessor::Result r = m_processor->next(f);
            if (r == JPJobProcessor::Result::Finished) {
                signal(JobState::Finished);
                post([this] { setState(RunState::Stopped); });
                return;
            }
            if (r == JPJobProcessor::Result::Failed) {
                signal(JobState::Error);
                post([this, f] {
                    // Paused there (stopped, when stopping), then said.
                    setState(m_state == RunState::Stopping ? RunState::Stopped : RunState::Paused);
                    if (showSource) showSource(f);
                    JDialog::message("Job Error", f.message);
                });
                return;
            }
            // Pausing (a Step), at a step that moved the machine when Step Next Motion says so.
            if (m_state == RunState::Pausing && !(m_stepToMotion && m_jobMachine->motions() == motionsBefore)) {
                post([this] { setState(RunState::Paused); });
                return;
            }
        }
    });
}

bool JPlacerJobRun::machineTask(
    std::function<bool(JPJobMachine&, const std::function<void(const std::function<void()>&)>&, std::string&)> work) {
    if (m_state == RunState::Running || m_state == RunState::Pausing || m_state == RunState::Stopping) {
        m_window.showStatus("The job is running: pause it first", kStatusMs);
        return false;
    }
    JPCell* cell = m_machine.cell();
    if (!cell || !cell->isConnected() || !cell->isHomed()) {
        m_window.showStatus(!cell || !cell->isConnected() ? "Connect the machine first" : "Home the machine first", kStatusMs);
        return false;
    }
    join();
    m_worker = std::thread([this, work = std::move(work)] {
        std::string why;
        const bool ok = work(*m_jobMachine, [this](const std::function<void()>& fn) { onMain(fn); }, why);
        if (m_quitting) return;
        post([this, ok, why] {
            m_job.configurationChanged();
            if (!ok) JDialog::message("Error", why);
        });
    });
    return true;
}

void JPlacerJobRun::fiducialCheck(JPPlacementsHolderLocation* location) {
    if (m_state == RunState::Running || m_state == RunState::Pausing || m_state == RunState::Stopping) {
        m_window.showStatus("The job is running: pause it first", kStatusMs);
        return;
    }
    JPCell* cell = m_machine.cell();
    if (!cell || !cell->isConnected() || !cell->isHomed()) {
        m_window.showStatus(!cell || !cell->isConnected() ? "Connect the machine first" : "Home the machine first", kStatusMs);
        return;
    }
    join();
    JPFiducialLocator::Tolerances tolerances;
    tolerances.scaling = cell->config().jobProcessor.scalingTolerance;
    tolerances.shearing = cell->config().jobProcessor.shearingTolerance;
    tolerances.boardLocationMm = cell->config().jobProcessor.boardLocationToleranceMm;
    tolerances.vision = cell->config().vision;
    // The board or panel set by its fiducials (its own location too, straight
    // in the job), then the camera taken to it.
    m_worker = std::thread([this, location, tolerances] {
        const JPFiducialLocator::Result r = JPFiducialLocator::locate(
            m_job.configuration(), *m_jobMachine, [this](const std::function<void()>& fn) { onMain(fn); }, { location },
            tolerances);
        if (m_quitting) return;
        post([this, location, r] {
            if (!r.ok) {
                JDialog::message("Error", r.message);
                return;
            }
            if (location->parent == &m_job.job().root()) {
                const JPAffineTransform tx = location->localToGlobalTransform();
                location->setLocation(r.location);
                location->setLocalToGlobalTransform(tx);
            }
            m_job.changed();
            m_machine.moveToolTo(JPSetupForm::Tool::Camera, r.location);
        });
    });
}

} // inline namespace jf
