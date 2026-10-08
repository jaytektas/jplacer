// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include "machine/JPJobProcessorConfig.h"

#include <j/config/Json.h>
#include "machine/JPVisionConfig.h"

#include "model/JPConfiguration.h"
#include "model/JPJob.h"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

inline namespace jf {

// Runs a job, as OpenPnP's ReferencePnpJobProcessor: a step at a time
// (next()), each step moving the machine (JPJobMachine) and saying what it
// does. PreFlight gathers the placements to place (enabled, not placed,
// their side up on an enabled board) and checks each has a part, a package
// and a nozzle tip that fits, then puts the head at safe Z and drops what the
// nozzles hold. A part no feeder holds is not a failure (load as you go,
// jplacer's): what is loaded is placed first, then the run fails with the
// part to load (Failure::loadPartId), and goes on once it is loaded or
// skipped (skipPart). FiducialCheck locates each board and panel
// whose fiducials are checked (the outer ones first, as many levels as the
// fiducial level says, then the rest) and sets where it lies from them.
// Plan orders the placements still to do (by the job order), the lowest rank
// first, and gives one to each nozzle (OpenPnP's SimplePnpJobPlanner);
// then for those: the nozzle tips changed, the nozzles turned ready, Pick
// (feed, pick, each retried as the feeder and part say), Align, Place, and
// the next cycle, until all are placed; then Cleanup (safe Z, discard, park).
//
// A step's failure on a placement is shown (Alert), or, when the placement's
// error handling defers it, put off: the feeder's fault counted, the
// placement tried again later (up to the retries) or marked in error, and
// the job goes on. The model (job, boards, feeders) is only touched through
// Hooks::onMain, so a job run on its own thread never races the screen.
class JPJobProcessor {
public:
    using Settings = JPJobProcessorConfig;
    using JobOrder = JPJobProcessorConfig::JobOrder;
    using Strategy = JPJobProcessorConfig::Strategy;

    // OpenPnP's PlannedPlacement: a nozzle, the tip it places with, the placement (its board's unique id), its part
    // and rank, and what moving to it costs from those planned before it (its TravelCost; none: not chosen by it).
    struct PlannedPlacement {
        std::string           nozzleId, tipId, boardId, placementId, partId;
        int                   rank = 0;
        std::optional<double> planningCost;
    };

    struct JobPlacement;
    struct Hooks {
        // Runs `fn` where the model lives (the screen's thread), waiting for it; none: here.
        std::function<void(const std::function<void()>& fn)> onMain;
        // What the job is doing now ("Pick R0603-10K from …").
        std::function<void(const std::string&)> status;
        // A placement placed (the placed counts to show again).
        std::function<void()> placed;
        // A feeder changed (fed, turned off, a fault counted): to be kept and shown.
        std::function<void()> feedersChanged;
        // OpenPnP's scripting events ("Job.Starting", "Nozzle.BeforePick"), with what they are for; a failing
        // script stops the job (false, with why).
        std::function<bool(const std::string& event, const JJson& globals, std::string& why)> event;
        // Material (DESIGN.md, Stock ledger): a part fed from a feeder for a placement (`placed` false), and a
        // placement placed with a part from a feeder (`placed` true). Fed and not placed is lost.
        std::function<void(bool placed, const std::string& feederId, const JobPlacement& j)> material;
        // Each plan made (OpenPnP's PlannerStepResults): what each nozzle is to place, in turn.
        std::function<void(const std::vector<PlannedPlacement>&)> planned;
    };

    // What went wrong, and on what (OpenPnP's JobProcessorException and its source).
    struct Failure {
        enum class Source { None, Board, Placement, Part, Feeder, Nozzle, NozzleTip, Machine };
        std::string message;
        Source      source = Source::None;
        std::string id;            // the part, feeder, nozzle or tip; a board's unique id
        std::string placementId;   // Placement: the placement, `id` its board's unique id
        bool        interrupting = false;
        // Load as you go: the part to be loaded on a feeder for the run to go on (no feeder holds it, and
        // nothing loaded is left to place); empty: not that.
        std::string loadPartId;
    };

    enum class Status { Pending, Processing, Errored, Complete };
    enum class Result { More, Finished, Failed };

    // A placement this run places, and how it stands.
    struct JobPlacement {
        JPBoardLocation*          board = nullptr;
        std::string               boardId;   // the board's unique id
        std::string               placementId, partId;
        int                       rank = 0;
        double                    partHeightMm = 0;
        Status                    status = Status::Pending;
        std::string               error;
        int                       processingCount = 0;
        int                       feederIndex = 0;
        std::string               plannedFeederId;
        std::optional<JPLocation> plannedPickLocation;
    };

    JPJobProcessor(JPConfiguration& config, JPJob& job, JPJobMachine& machine, Settings settings, Hooks hooks);

    // OpenPnP's next(): one step. More while there is more to do; Finished
    // when done; Failed with `failure` (the job paused there: next() goes on
    // from the same step).
    Result next(Failure& failure);
    // Stopped: the nozzles emptied, the head parked.
    void abort();
    // Load as you go: a part not loaded set aside, its placements left in error ("Skipped: not loaded").
    void skipPart(const std::string& partId);

    const std::vector<JobPlacement>& jobPlacements() const { return m_jobPlacements; }
    // The machine's vision: bottom vision (on or off, its default settings,
    // passes, pre-rotation, angular offset) and the fiducial locator's settings.
    void setVision(const JPVisionConfig& v) { m_vision = v; }
    int totalPartsPlaced() const { return m_totalPartsPlaced; }


private:
    struct Planned {
        std::string           nozzleId, tipId;
        size_t                job = 0;   // into m_jobPlacements
        std::optional<double> cost;
        std::optional<JPJobMachine::AlignResult> alignment;   // what bottom vision found
    };
    enum class Step {
        PreFlight, FiducialCheck, Plan, ChangeNozzleTips, CalibrateNozzleTips, OptimizeForPick, PrerotateForPick,
        Pick, OptimizeForAlign, PrerotateForAlign, Align, OptimizeForPlace, PrerotateForPlace, Place, FinishCycle,
        Finish
    };

    void main(const std::function<void()>& fn);
    void status(const std::string& s);
    Step run(Step step);
    // A step over the planned placements, one each call; Alert and Defer as the placement says.
    Step plannedStep(Step step, Step after);

    Step preFlight();
    Step fiducialCheck();
    Step plan();
    void calibrateNozzleTip(Planned& p);
    Step changeNozzleTip(Planned& p);
    Step pick(Planned& p);
    Step align(Planned& p);
    Step place(Planned& p);
    void optimize(bool byPick);
    void prerotate(bool forPick);
    // An event's scripts run; one failing fails the job (OpenPnP's Scripting.on).
    // The part on a nozzle dropped at the discard location, with OpenPnP's discard events round it.
    bool discard(const std::string& nozzleId, std::string& why);
    // OpenPnP's ContactProbeNozzle.moveToPickLocation / moveToPlacementLocation:
    // where a contact probing nozzle picks (from `feederId`) or places, `at`
    // (the part's height in, `base` without it): the height probed, as its
    // trigger says (and the part's height, when not known), else the offset it
    // found before applied. Unchanged for a nozzle that does not probe.
    JPLocation probedPick(const std::string& nozzleId, const std::string& feederId, JobPlacement& j, JPLocation at,
                          const JPLocation& base, bool heightAbove);
    JPLocation probedPlace(const std::string& nozzleId, JobPlacement& j, JPLocation at, const JPLocation& base);
    // A part's height found by probing: kept with the part, and for the job's placements of it.
    void setPartHeight(const std::string& partId, double heightMm);
    void script(const std::string& event, JJson globals);
    // What a placement's events are given: the job, its board and placement, its part.
    JJson placementGlobals(const JobPlacement& j) const;
    // OpenPnP's prepareForPickAndPlaceArticulation (JPRotationMode): the
    // nozzle's rotation mode offset for the part, given it and kept.
    void prepareArticulation(const std::string& nozzleId, double pickAngle, double placeAngle);
    void cleanup();
    Step finish();
    void discardAll();

    // OpenPnP's getOpenPendingWorkableJobPlacements and its rank blocking.
    std::vector<size_t> openPendingWorkable();
    std::vector<size_t> ordered(std::vector<size_t> open, std::vector<std::string>& plannedTips);
    std::vector<size_t> byPickLocation(const std::vector<size_t>& in, std::optional<JPLocation>& start);
    std::vector<size_t> byPickPlaceLocation(const std::vector<size_t>& in, std::optional<JPLocation>& start);
    std::vector<Planned> planner(std::vector<size_t> jobs, std::vector<std::string> tips);
    std::optional<Planned> planWithout(const JPJobMachine::Nozzle& n, const std::string& tipId,
                                       const std::vector<size_t>& jobs, const std::vector<Planned>& planned);
    bool fits(size_t job, const std::string& tipId) const;
    JPLocation placeLocation(size_t job) const;
    JPFeeder* feederFor(const std::string& partId, const std::string& preferredId);

    JPConfiguration&                   m_config;
    JPJob&                             m_job;
    JPJobMachine&                      m_machine;
    Settings                           m_settings;
    Hooks                              m_hooks;
    Step                               m_step = Step::PreFlight;
    std::vector<JobPlacement>          m_jobPlacements;
    std::vector<Planned>               m_planned;
    std::set<size_t>                   m_completed;   // of m_planned, in the step under way
    std::set<const JPPlacementsHolderLocation*> m_fiducialsDone;
    int                                m_fiducialLevel = 0;
    bool                               m_restart = true;
    bool                               m_finished = false;
    JPVisionConfig                     m_vision;   // the planner's: the first plan of a run
    std::map<std::string, std::string> m_partOn;           // nozzle: the part it holds
    std::map<std::string, std::string> m_partsFeeder;      // nozzle: the feeder its part came from
    // OpenPnP's Rotation Mode offset, by nozzle, from its pick to its place or
    // discard: its turn is the part's angle less this.
    std::map<std::string, double>      m_rotationOffset;
    std::optional<JPLocation>          m_previousPickStart, m_previousPlaceStart;
    int                                m_totalPartsPlaced = 0;
    double                             m_startSeconds = 0;
};

} // inline namespace jf
