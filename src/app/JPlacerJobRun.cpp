// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerJobRun.h"

#include "JPlacerJobCheckDialog.h"
#include "JPlacerLoadDialog.h"

#include "common/JPlacerLog.h"
#include "model/JPBoardLocation.h"
#include "model/JPJobCheck.h"
#include "model/JPLaneChoice.h"
#include "model/JPRunLedger.h"
#include "setup/JPFeederForms.h"
#include "tasks/JPFiducialLocator.h"

#include <j/core/Dialog.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <algorithm>
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
    : m_window(window), m_job(job), m_machine(machine), m_host(machine), m_panel(panel), m_mainThread(std::this_thread::get_id()) {
    m_jobMachine = std::make_unique<JPCellJobMachine>(
        m_host, job.configuration(), [this](const std::function<void()>& fn) { onMain(fn); },
        [this](const std::string& q) { return ask(q); },
        [this](const std::string& s) {
            post([this, s] { m_window.showStatus(s, kStatusMs); });
        });
    m_jobMachine->onLookingFor = [this](const std::string& partId) {
        post([this, partId] { if (onLookingFor) onLookingFor(partId); });
    };
    panel.onStartPauseResume = [this] { startPauseResume(); };
    panel.onStep = [this] { step(); };
    panel.onStop = [this] { stop(); };
    panel.onFiducialCheck = [this](JPPlacementsHolderLocation* l) { fiducialCheck(l); };
    machine.jobRunning = [this] { return running(); };
    job.running = [this] { return running(); };
    machine.onConnectedChanged = [this](bool connected) {
        m_panel.setMachineEnabled(connected);
        if (connected) forgetTunes();   // the light may be another now: every tune made afresh
    };
    panel.setMachineEnabled(machine.isConnected());
    panel.setRunState(RunState::Stopped);
}

JPlacerJobRun::~JPlacerJobRun() {
    // Stopped where it is: the step under way ends, nothing more is done.
    m_quitting = true;
    *m_alive = false;
    m_state = RunState::Stopped;
    join();
    endRun(JPRunStore::Outcome::Stopped);
    m_machine.jobRunning = nullptr;
    m_machine.onConnectedChanged = nullptr;
    m_job.running = nullptr;
}

void JPlacerJobRun::beginRun() {
    endRun(JPRunStore::Outcome::Stopped);   // one left open (a run started again while one was paused)
    JPRunStore::Run run;
    run.job = m_job.job().file;
    for (const JPBoardLocation* l : m_job.job().boardLocations())
        if (l && l->board()) run.boards.push_back({ l->uniqueId(), l->board()->file, l->board()->revisionLabel() });
    m_warnedLots.clear();
    if (m_job.configuration().runs().begin(run)) m_runUuid = run.uuid;
    else JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << "the run could not be recorded (runs.db): its parts will not reach the ledger";
}

void JPlacerJobRun::endRun(JPRunStore::Outcome outcome) {
    if (m_runUuid.empty()) return;
    JPConfiguration& config = m_job.configuration();
    config.runs().end(m_runUuid, outcome);
    std::string why;
    if (!JPRunLedger::write(config.runs(), config.stock(), m_runUuid, why))
        JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << "the run's parts could not be written to the stock's ledger: " << why
                                                 << " (written when jplacer next opens)";
    m_runUuid.clear();
}

void JPlacerJobRun::join() {
    if (m_worker.joinable()) m_worker.join();
}

bool JPlacerJobRun::taskUnderWay() {
    if (!m_taskUnderWay) return false;
    m_window.showStatus("The machine is busy with a task: wait for it to finish", kStatusMs);
    return true;
}

void JPlacerJobRun::launchTask(std::function<void()> body) {
    m_taskUnderWay = true;
    m_worker = std::thread([this, body = std::move(body)] {
        body();
        m_taskUnderWay = false;
    });
}

void JPlacerJobRun::post(std::function<void()> fn) {
    std::weak_ptr<bool> alive = m_alive;
    JMainThreadDispatcher::instance().post([alive, fn = std::move(fn)] {
        if (const auto a = alive.lock(); a && *a) fn();
    });
}

JPCellJobMachine& JPlacerJobRun::jobMachine() { return *m_jobMachine; }

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
    if (taskUnderWay()) return;
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
    if (taskUnderWay()) return;
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
    if (was == RunState::Paused && taskUnderWay()) return;
    setState(RunState::Stopping);
    // Running, the worker stops after its step; paused, one is started to stop it.
    if (was == RunState::Paused) {
        join();
        run();
    }
}

void JPlacerJobRun::forgetTunes() {
    m_jobMachine->newRun();
    for (JPFeeder& f : m_job.configuration().feeders()) f.cameraTune.reset();
}

void JPlacerJobRun::start(RunState as) {
    JPCell* cell = m_machine.cell();
    // A run begun afresh (not resumed): its parts' and feeders' camera settings tuned anew.
    if (as == RunState::Running && !running()) forgetTunes();
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
        if (taskUnderWay()) return;
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
        // Each part fed and placed, with the lot its feeder carries, recorded as it happens.
        hooks.material = [this](bool placed, const std::string& feederId, const JPJobProcessor::JobPlacement& j) {
            if (m_runUuid.empty()) return;
            JPConfiguration& config = m_job.configuration();
            const JPStockLot lot = config.stock().lotOnFeeder(feederId);
            if (placed) {
                config.runs().placed(m_runUuid, feederId, lot.uuid, j.partId, j.boardId, j.placementId);
                return;
            }
            config.runs().fed(m_runUuid, feederId, lot.uuid, j.partId, j.boardId, j.placementId);
            if (lot.uuid.empty() || m_warnedLots.count(lot.uuid)) return;
            // Running out: fewer left by its count than this run still places of the part (said once a lot).
            const auto taken = config.runs().fedByOpenRuns();
            const auto t = taken.find(lot.uuid);
            const long long left = lot.onHand - (t == taken.end() ? 0 : t->second);
            long long toPlace = 0;
            for (const JPJobProcessor::JobPlacement& p : m_processor->jobPlacements())
                toPlace += p.partId == j.partId && p.status == JPJobProcessor::Status::Pending;
            if (left >= toPlace) return;
            m_warnedLots.insert(lot.uuid);
            std::string feederName = feederId;
            if (const JPFeeder* f = config.feeder(feederId)) feederName = f->name();
            const std::string said = feederName + ": " + lot.label + " has about " + std::to_string(std::max(0LL, left)) +
                                     " left by its count, and " + std::to_string(toPlace) + " of " + j.partId +
                                     " are still to place";
            JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << said;
            post([this, said] { m_window.showStatus(said, kStatusMs); });
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
        beginRun();
        setState(as);
        run();
    };
    // The job's data checked first: with anything to put right or check, the list, and Start Anyway when
    // nothing stops it.
    auto checked = [this, alive, go] {
        if (const auto a = alive.lock(); !a || !*a) return;
        std::vector<JPJobCheck::Item> items = JPJobCheck::of(m_job.configuration(), m_job.job(), machineTipIds());
        if (!JPJobCheck::asks(items)) {
            go();
            return;
        }
        const bool stops = JPJobCheck::stops(items);
        m_window.openModal<JPlacerJobCheckDialog>(std::move(items), stops ? std::function<void()>() : std::function<void()>(go));
    };
    // As OpenPnP: a job placed already is offered to be placed again.
    if (allPlaced(m_job.job())) {
        JDialogOptions opts;
        opts.okLabel = "Yes";
        opts.cancelLabel = "No";
        JDialog::confirm("Reset placement status?",
                         "All placements have been placed already. Reset all placements before starting job?",
                         [this, alive, checked] {
                             if (const auto a = alive.lock(); !a || !*a) return;
                             m_job.job().removeAllPlacedStatus();
                             m_job.changed();
                             checked();
                         },
                         checked, opts);
        return;
    }
    checked();
}

void JPlacerJobRun::askToLoad(const std::string& partId) {
    std::set<std::string> stillNeeded;
    for (const JPJobProcessor::JobPlacement& j : m_processor->jobPlacements())
        if (j.status == JPJobProcessor::Status::Pending || j.status == JPJobProcessor::Status::Processing)
            stillNeeded.insert(j.partId);
    JPConfiguration& config = m_job.configuration();
    JLOGC(JPlacerLog::kJob, JLogLevel::Info) << "load as you go: " << partId << " is needed and no feeder holds it";
    m_window.showStatus("Load " + partId + ": no feeder holds it.", kStatusMs);
    std::weak_ptr<bool> alive = m_alive;
    JPlacerLoadDialog::Actions actions;
    actions.loaded = [this, alive, partId](const std::string& laneId, const std::string& lotUuid) {
        if (const auto a = alive.lock(); !a || !*a) return;
        JPConfiguration& config = m_job.configuration();
        if (JPFeeder* f = laneId.empty() ? nullptr : config.feeder(laneId)) {
            const std::string was = f->partId();
            f->setPartId(partId);
            f->setEnabled(true);
            std::string why;
            if (!JPFeederForms::act(config, laneId, "resetFeedCount", why))
                JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << f->name() << ": its feed count was not reset: " << why;
            // The lot laid in it: the one there before comes off.
            JPStockStore& stock = config.stock();
            const JPStockLot before = stock.lotOnFeeder(laneId);
            if (!before.uuid.empty() && before.uuid != lotUuid && !stock.loadLot(before.uuid, "", why))
                JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << why;
            if (!lotUuid.empty() && !stock.loadLot(lotUuid, laneId, why)) JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << why;
            JLOGC(JPlacerLog::kJob, JLogLevel::Info) << "load as you go: " << partId << " loaded on " << f->name()
                                                     << (was.empty() ? std::string() : ", " + was + " taken off");
            m_job.configurationChanged();
        }
        if (m_state == RunState::Paused) startPauseResume();
    };
    actions.skip = [this, alive, partId] {
        if (const auto a = alive.lock(); !a || !*a) return;
        if (m_state != RunState::Paused) return;
        m_processor->skipPart(partId);
        JLOGC(JPlacerLog::kJob, JLogLevel::Info) << "load as you go: " << partId << " skipped";
        startPauseResume();
    };
    actions.stop = [this, alive] {
        if (const auto a = alive.lock(); !a || !*a) return;
        stop();
    };
    m_window.openModal<JPlacerLoadDialog>(config, partId, JPLaneChoice::free(config, partId, stillNeeded), std::move(actions));
}

std::vector<std::string> JPlacerJobRun::machineTipIds() const {
    std::vector<std::string> ids;
    for (const JPJobMachine::Nozzle& n : m_jobMachine->nozzles())
        for (const std::string& t : n.tipIds)
            if (std::find(ids.begin(), ids.end(), t) == ids.end()) ids.push_back(t);
    return ids;
}

void JPlacerJobRun::checkJob() {
    m_window.openModal<JPlacerJobCheckDialog>(JPJobCheck::of(m_job.configuration(), m_job.job(), machineTipIds()),
                                              std::function<void()>());
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
                    endRun(JPRunStore::Outcome::Stopped);
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
                post([this] {
                    endRun(JPRunStore::Outcome::Finished);
                    setState(RunState::Stopped);
                });
                return;
            }
            if (r == JPJobProcessor::Result::Failed) {
                signal(JobState::Error);
                post([this, f] {
                    // Paused there (stopped, when stopping), then said.
                    if (m_state == RunState::Stopping) endRun(JPRunStore::Outcome::Stopped);
                    setState(m_state == RunState::Stopping ? RunState::Stopped : RunState::Paused);
                    if (!f.loadPartId.empty() && m_state == RunState::Paused) {
                        askToLoad(f.loadPartId);
                        return;
                    }
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
    if (taskUnderWay()) return false;
    join();
    launchTask([this, work = std::move(work)] {
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
    if (taskUnderWay()) return;
    join();
    JPFiducialLocator::Tolerances tolerances;
    tolerances.scaling = cell->config().jobProcessor.scalingTolerance;
    tolerances.shearing = cell->config().jobProcessor.shearingTolerance;
    tolerances.boardLocationMm = cell->config().jobProcessor.boardLocationToleranceMm;
    tolerances.vision = cell->config().vision;
    // The board or panel set by its fiducials (its own location too, straight
    // in the job), then the camera taken to it.
    launchTask([this, location, tolerances] {
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
