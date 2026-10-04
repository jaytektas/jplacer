// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPJobProcessor.h"

#include "JPFiducialLocator.h"

#include "common/JPlacerLog.h"
#include "model/JPBoardLocation.h"
#include "model/JPPanel.h"
#include "model/JPPanelLocation.h"

#include <j/core/Log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>

inline namespace jf {

namespace {

using Failure = JPJobProcessor::Failure;
using Source  = Failure::Source;

// A failure on its way out of a step (OpenPnP's JobProcessorException).
struct JobError {
    Failure failure;
};

[[noreturn]] void fail(Source source, const std::string& id, const std::string& message, const std::string& placementId = "") {
    Failure f;
    f.source = source;
    f.id = id;
    f.placementId = placementId;
    f.message = message;
    throw JobError { f };
}

std::string format(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    return buf;
}

double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

JPLocation mm(const JPLocation& l) { return l.convertToUnits(JPLengthUnit::Millimeters); }

double distance(const JPLocation& a, const JPLocation& b) {
    const JPLocation x = mm(a), y = mm(b);
    return std::hypot(x.x() - y.x(), x.y() - y.y());
}

// OpenPnP's TravellingSalesman: an order through `points` from `start`
// (and towards `end`), short: nearest first, then improved by 2-opt.
std::vector<size_t> travel(const std::vector<JPLocation>& points, const std::optional<JPLocation>& start,
                           const std::optional<JPLocation>& end) {
    const size_t n = points.size();
    std::vector<size_t> order;
    if (n == 0) return order;
    std::vector<bool> used(n, false);
    std::optional<JPLocation> at = start;
    for (size_t k = 0; k < n; ++k) {
        size_t best = n;
        double bestD = std::numeric_limits<double>::max();
        for (size_t i = 0; i < n; ++i) {
            if (used[i]) continue;
            const double d = at ? distance(*at, points[i]) : 0;
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        used[best] = true;
        order.push_back(best);
        at = points[best];
    }
    // The path's length, from the start and to the end where given.
    auto length = [&](const std::vector<size_t>& o) {
        double l = start ? distance(*start, points[o.front()]) : 0;
        for (size_t i = 1; i < o.size(); ++i) l += distance(points[o[i - 1]], points[o[i]]);
        if (end) l += distance(points[o.back()], *end);
        return l;
    };
    bool better = true;
    for (int pass = 0; better && pass < 50; ++pass) {
        better = false;
        for (size_t i = 0; i + 1 < n; ++i)
            for (size_t j = i + 1; j < n; ++j) {
                std::vector<size_t> trial = order;
                std::reverse(trial.begin() + long(i), trial.begin() + long(j) + 1);
                if (length(trial) + 1e-9 < length(order)) {
                    order = trial;
                    better = true;
                }
            }
    }
    return order;
}

// The middle of some places; none without any.
std::optional<JPLocation> centre(const std::vector<JPLocation>& places) {
    if (places.empty()) return std::nullopt;
    double x = 0, y = 0;
    for (const JPLocation& p : places) {
        const JPLocation m = mm(p);
        x += m.x();
        y += m.y();
    }
    return JPLocation(JPLengthUnit::Millimeters, x / double(places.size()), y / double(places.size()), 0, 0);
}

} // namespace

JPJobProcessor::JPJobProcessor(JPConfiguration& config, JPJob& job, JPJobMachine& machine, Settings settings, Hooks hooks)
    : m_config(config), m_job(job), m_machine(machine), m_settings(settings), m_hooks(std::move(hooks)) {}

const char* JPJobProcessor::jobOrderName(JobOrder o) {
    switch (o) {
        case JobOrder::Part:                    return "Part";
        case JobOrder::PartHeight:              return "Part Height";
        case JobOrder::PartBoard:               return "Part then Board";
        case JobOrder::HeightPartBoard:         return "Height, Part then Board";
        case JobOrder::BoardPart:               return "Board then Part";
        case JobOrder::PickLocation:            return "Pick Location";
        case JobOrder::PickPlaceLocation:       return "Pick and Place Location";
        case JobOrder::NozzleTips:              return "Nozzle Tips";
        case JobOrder::NozzleTipsByFlexibility: return "Nozzle Tips by Flexibility";
        case JobOrder::Unsorted:                return "Unsorted";
    }
    return "";
}

const char* JPJobProcessor::strategyName(Strategy s) {
    switch (s) {
        case Strategy::Minimize:       return "Minimize";
        case Strategy::StartAsPlanned: return "StartAsPlanned";
        case Strategy::FullyAsPlanned: return "FullyAsPlanned";
    }
    return "";
}

void JPJobProcessor::main(const std::function<void()>& fn) {
    // A failure inside is carried back to this thread, not thrown on the other.
    std::exception_ptr failed;
    auto guarded = [&] {
        try {
            fn();
        } catch (...) {
            failed = std::current_exception();
        }
    };
    if (m_hooks.onMain) m_hooks.onMain(guarded);
    else guarded();
    if (failed) std::rethrow_exception(failed);
}

void JPJobProcessor::status(const std::string& s) {
    JLOGC(JPlacerLog::kJob, JLogLevel::Info) << s;
    if (m_hooks.status) m_hooks.status(s);
}

JPJobProcessor::Result JPJobProcessor::next(Failure& failure) {
    if (m_finished) return Result::Finished;
    try {
        m_step = run(m_step);
    } catch (const JobError& e) {
        failure = e.failure;
        JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << "job: " << failure.message;
        return Result::Failed;
    }
    return m_finished ? Result::Finished : Result::More;
}

void JPJobProcessor::abort() {
    try {
        cleanup();
    } catch (const JobError& e) {
        JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << "job stopped: " << e.failure.message;
    }
    m_finished = true;
}

JPJobProcessor::Step JPJobProcessor::run(Step step) {
    switch (step) {
        case Step::PreFlight:           return preFlight();
        case Step::FiducialCheck:       return fiducialCheck();
        case Step::Plan:                return plan();
        case Step::ChangeNozzleTips:    return plannedStep(step, Step::CalibrateNozzleTips);
        // A tip's runout, once measured, is kept and applied by the machine on
        // every move: there is nothing to calibrate before the pick.
        case Step::CalibrateNozzleTips: return Step::OptimizeForPick;
        case Step::OptimizeForPick:     optimize(true); return Step::PrerotateForPick;
        case Step::PrerotateForPick:    prerotate(true); return Step::Pick;
        case Step::Pick:                return plannedStep(step, Step::OptimizeForAlign);
        // No part is aligned (see Align), so there is nothing to order or turn for it.
        case Step::OptimizeForAlign:    return Step::PrerotateForAlign;
        case Step::PrerotateForAlign:   return Step::Align;
        case Step::Align:               return plannedStep(step, Step::OptimizeForPlace);
        case Step::OptimizeForPlace:    optimize(false); return Step::PrerotateForPlace;
        case Step::PrerotateForPlace:   prerotate(false); return Step::Place;
        case Step::Place:               return plannedStep(step, Step::FinishCycle);
        case Step::FinishCycle:         discardAll(); return Step::Plan;
        case Step::Finish:              return finish();
    }
    return Step::Finish;
}

// ---- PreFlight ---------------------------------------------------------------

JPJobProcessor::Step JPJobProcessor::preFlight() {
    m_startSeconds = seconds();
    m_totalPartsPlaced = 0;
    m_jobPlacements.clear();
    m_previousPickStart = m_previousPlaceStart = JPLocation::origin();
    status("Checking job for setup errors.");
    const std::vector<JPJobMachine::Nozzle> nozzles = m_machine.nozzles();
    main([&] {
        for (JPBoardLocation* l : m_job.boardLocations()) {
            if (!l->isEnabled() || !l->holder) continue;
            // A board with an id twice cannot tell its placements apart.
            std::set<std::string> ids;
            for (const JPPlacement& p : l->holder->placements)
                if (!ids.insert(p.id).second)
                    fail(Source::Board, l->uniqueId(),
                         format("This board contains at least one duplicate ID entry: %s ", p.id.c_str()));
            for (const JPPlacement& p : l->holder->placements) {
                if (p.type != JPPlacement::Type::Placement || !m_job.retrieveEnabledState(*l, &p)) continue;
                if (m_job.retrievePlacedStatus(*l, p.id) || p.side != l->globalSide()) continue;
                const JPPart* part = m_config.part(p.partId);
                const std::string boardName = l->holder->name.value_or(l->fileName);
                if (!part)
                    fail(Source::Placement, l->uniqueId(),
                         format("Part not found for board %s, placement %s.", boardName.c_str(), p.id.c_str()), p.id);
                const JPPackage* package = m_config.package(part->packageId);
                if (!package) fail(Source::Part, part->id, format("No package set for part %s.", part->id.c_str()));
                // A nozzle tip that fits the package and one of the nozzles.
                bool tip = false;
                for (const auto& n : nozzles)
                    for (const std::string& t : n.tipIds)
                        tip = tip || std::find(package->compatibleNozzleTipIds.begin(), package->compatibleNozzleTipIds.end(),
                                               t) != package->compatibleNozzleTipIds.end();
                if (!tip) {
                    if (part->isPartHeightUnknown())
                        fail(Source::Part, part->id,
                             format("No part height sensing method found for part %s. Check camera, contact probe nozzle "
                                    "and compatible, loadable nozzle tips for height sensing settings or set part height "
                                    "manually.",
                                    part->id.c_str()));
                    fail(Source::Part, part->id,
                         format("No compatible, loadable nozzle tip found for part %s.", part->id.c_str()));
                }
                if (!m_config.findFeeder(part->id, std::nullopt))
                    fail(Source::Part, part->id, "No compatible, enabled feeder found for part " + part->id);
                JobPlacement j;
                j.board = l;
                j.boardId = l->id;
                j.placementId = p.id;
                j.partId = part->id;
                j.rank = p.rank;
                j.partHeightMm = part->height.convertToUnits(JPLengthUnit::Millimeters).value();
                m_jobPlacements.push_back(j);
            }
        }
    });
    status("Preparing machine.");
    std::string why;
    if (!m_machine.safeZ(why)) fail(Source::Machine, "", why);
    discardAll();
    // A strip or tray feeder needs nothing before a job.
    status("Preparing feeders.");
    m_restart = true;
    return Step::FiducialCheck;
}

// ---- FiducialCheck ---------------------------------------------------------

JPJobProcessor::Step JPJobProcessor::fiducialCheck() {
    struct Nested {
        JPPlacementsHolderLocation* location;
        int                         level;
    };
    std::vector<Nested> locations;
    main([&] {
        std::function<void(JPPlacementsHolderLocation&, int)> collect = [&](JPPlacementsHolderLocation& l, int level) {
            if (!l.isEnabled()) return;
            const bool check = m_job.retrieveCheckFiducialsState(l);
            if (l.kind() == JPPlacementsHolderLocation::Kind::Board) {
                if (check) locations.push_back({ &l, level });
                return;
            }
            if (check) locations.push_back({ &l, level });
            if (auto* panel = static_cast<JPPanelLocation&>(l).panel())
                for (const auto& c : panel->children) collect(*c, level + 1);
        };
        collect(m_job.root(), 0);
    });
    std::erase_if(locations, [this](const Nested& n) { return m_fiducialsDone.count(n.location) != 0; });
    if (locations.empty()) return Step::Plan;
    if (m_fiducialLevel < m_settings.fiducialLevel) {
        status(format("Checking fiducials at level %d.", m_fiducialLevel));
        const int level = locations.front().level;
        std::erase_if(locations, [level](const Nested& n) { return n.level != level; });
    } else {
        status(m_settings.fiducialLevel > 0 ? "Checking remaining fiducials." : "Checking all fiducials.");
    }

    std::vector<JPPlacementsHolderLocation*> those;
    for (const Nested& n : locations) those.push_back(n.location);
    JPFiducialLocator::Tolerances tolerances;
    tolerances.scaling = m_settings.scalingTolerance;
    tolerances.shearing = m_settings.shearingTolerance;
    tolerances.boardLocationMm = m_settings.boardLocationToleranceMm;
    const JPFiducialLocator::Result found =
        JPFiducialLocator::locate(m_config, m_machine, [this](const std::function<void()>& fn) { main(fn); }, those, tolerances);
    if (!found.ok) {
        const Source source = found.about == JPFiducialLocator::Result::About::Placement ? Source::Placement
                            : found.about == JPFiducialLocator::Result::About::Part    ? Source::Part
                                                                                       : Source::Board;
        fail(source, found.id, found.message, found.placementId);
    }
    ++m_fiducialLevel;
    for (const Nested& n : locations) m_fiducialsDone.insert(n.location);
    return Step::FiducialCheck;
}

// ---- Plan ------------------------------------------------------------------

JPFeeder* JPJobProcessor::feederFor(const std::string& partId, const std::string& preferredId) {
    JPFeeder* best = m_config.findFeeder(partId, m_machine.cameraLocation());
    if (!best) fail(Source::Part, partId, "No compatible, enabled feeder found for part " + partId);
    // The feeder planned stays, while it is still one of the best.
    if (JPFeeder* f = preferredId.empty() ? nullptr : m_config.feeder(preferredId))
        if (f->enabled() && f->partId() == partId && f->priority() == best->priority()) return f;
    return best;
}

std::vector<size_t> JPJobProcessor::openPendingWorkable() {
    int currentRank = std::numeric_limits<int>::max();
    for (const JobPlacement& j : m_jobPlacements)
        if (j.status != Status::Complete) currentRank = std::min(currentRank, j.rank);
    if (currentRank == std::numeric_limits<int>::max()) currentRank = 0;
    // A rank ten above the lowest not done waits for it.
    const int blocked = currentRank + 10;
    std::vector<size_t> workable;
    std::map<std::string, std::optional<JPLocation>> pickOf;
    std::optional<JobError> first;
    main([&] {
        for (size_t i = 0; i < m_jobPlacements.size(); ++i) {
            JobPlacement& j = m_jobPlacements[i];
            if (j.status != Status::Pending || j.rank >= blocked) continue;
            if (!pickOf.count(j.partId)) {
                try {
                    const JPFeeder* f = feederFor(j.partId, "");
                    pickOf[j.partId] = f->pickLocation();
                    if (!pickOf[j.partId]) fail(Source::Feeder, f->id(), "Feeder pick location must not be null");
                } catch (const JobError& e) {
                    if (!first) first = e;
                    pickOf[j.partId] = std::nullopt;
                }
            }
            if (const auto& at = pickOf[j.partId]) {
                j.plannedPickLocation = at;
                workable.push_back(i);
            }
        }
    });
    if (workable.empty() && first) throw *first;
    return workable;
}

bool JPJobProcessor::fits(size_t job, const std::string& tipId) const {
    const JPPart* part = m_config.part(m_jobPlacements[job].partId);
    const JPPackage* package = part ? m_config.package(part->packageId) : nullptr;
    return package && std::find(package->compatibleNozzleTipIds.begin(), package->compatibleNozzleTipIds.end(), tipId) !=
                          package->compatibleNozzleTipIds.end();
}

JPLocation JPJobProcessor::placeLocation(size_t job) const {
    const JobPlacement& j = m_jobPlacements[job];
    if (j.board && j.board->holder)
        if (const JPPlacement* p = j.board->holder->find(j.placementId)) return j.board->placementLocation(p->location);
    return JPLocation::origin();
}

std::vector<size_t> JPJobProcessor::byPickLocation(const std::vector<size_t>& in, std::optional<JPLocation>& start) {
    // The feeders, in a short way from `start`; each placement numbered by its part's feeder.
    std::vector<size_t> local = in;
    std::stable_sort(local.begin(), local.end(),
                     [this](size_t a, size_t b) { return m_jobPlacements[a].partId < m_jobPlacements[b].partId; });
    std::vector<std::string> feeders;   // their parts
    std::vector<JPLocation> picks;
    main([&] {
        for (const size_t i : local) {
            const JPFeeder* f = m_config.findFeeder(m_jobPlacements[i].partId, m_previousPickStart);
            if (!f) continue;
            if (std::find(feeders.begin(), feeders.end(), f->partId()) != feeders.end()) continue;
            const auto at = f->pickLocation();
            if (!at) continue;
            feeders.push_back(f->partId());
            picks.push_back(*at);
        }
    });
    if (!feeders.empty()) {
        const std::vector<size_t> order = travel(picks, start, std::nullopt);
        for (const size_t i : local)
            for (size_t k = 0; k < order.size(); ++k)
                if (feeders[order[k]] == m_jobPlacements[i].partId) {
                    m_jobPlacements[i].feederIndex = int(k);
                    break;
                }
        start = picks[order.front()];
    }
    std::stable_sort(local.begin(), local.end(), [this](size_t a, size_t b) {
        const JobPlacement &x = m_jobPlacements[a], &y = m_jobPlacements[b];
        if (x.feederIndex != y.feederIndex) return x.feederIndex < y.feederIndex;
        if (x.partId != y.partId) return x.partId < y.partId;
        return x.boardId < y.boardId;
    });
    return local;
}

std::vector<size_t> JPJobProcessor::byPickPlaceLocation(const std::vector<size_t>& in, std::optional<JPLocation>& start) {
    std::vector<size_t> rest = byPickLocation(in, start);
    // Then each feeder's placements in a short way, one feeder's after the other's.
    std::vector<size_t> out;
    std::optional<JPLocation> from = m_previousPlaceStart;
    bool first = true;
    while (!rest.empty()) {
        const int feeder = m_jobPlacements[rest.front()].feederIndex;
        std::vector<size_t> group;
        for (const size_t i : rest)
            if (m_jobPlacements[i].feederIndex == feeder) group.push_back(i);
        std::erase_if(rest, [&](size_t i) { return m_jobPlacements[i].feederIndex == feeder; });
        std::vector<JPLocation> places;
        main([&] {
            for (const size_t i : group) places.push_back(placeLocation(i));
        });
        for (const size_t k : travel(places, from, std::nullopt)) out.push_back(group[k]);
        main([&] {
            if (first) {
                m_previousPlaceStart = placeLocation(out.front());
                first = false;
            }
            from = placeLocation(out.front());
        });
    }
    return out;
}

std::vector<size_t> JPJobProcessor::ordered(std::vector<size_t> open, std::vector<std::string>& plannedTips) {
    auto by = [this, &open](auto less) {
        std::stable_sort(open.begin(), open.end(), [&](size_t a, size_t b) { return less(m_jobPlacements[a], m_jobPlacements[b]); });
        return open;
    };
    switch (m_settings.jobOrder) {
        case JobOrder::Part: return by([](const JobPlacement& a, const JobPlacement& b) { return a.partId < b.partId; });
        case JobOrder::PartHeight:
            return by([](const JobPlacement& a, const JobPlacement& b) {
                return a.partHeightMm != b.partHeightMm ? a.partHeightMm < b.partHeightMm : a.partId < b.partId;
            });
        case JobOrder::PartBoard:
            return by([](const JobPlacement& a, const JobPlacement& b) {
                return a.partId != b.partId ? a.partId < b.partId : a.boardId < b.boardId;
            });
        case JobOrder::HeightPartBoard:
            return by([](const JobPlacement& a, const JobPlacement& b) {
                if (a.partHeightMm != b.partHeightMm) return a.partHeightMm < b.partHeightMm;
                return a.partId != b.partId ? a.partId < b.partId : a.boardId < b.boardId;
            });
        case JobOrder::BoardPart:
            return by([](const JobPlacement& a, const JobPlacement& b) {
                return a.boardId != b.boardId ? a.boardId < b.boardId : a.partId < b.partId;
            });
        case JobOrder::PickLocation: {
            std::optional<JPLocation> start = m_previousPickStart;
            std::vector<size_t> out = byPickLocation(open, start);
            m_previousPickStart = start;
            return out;
        }
        case JobOrder::PickPlaceLocation: {
            std::optional<JPLocation> start = m_previousPickStart;
            std::vector<size_t> out = byPickPlaceLocation(open, start);
            m_previousPickStart = start;
            return out;
        }
        case JobOrder::NozzleTips:
        case JobOrder::NozzleTipsByFlexibility: break;
        case JobOrder::Unsorted: return open;
    }
    // By nozzle tip: the tips that fit a nozzle, each with the placements it
    // can pick, the busiest first; a placement goes with the first tip that
    // can take it.
    const std::vector<JPJobMachine::Nozzle> nozzles = m_machine.nozzles();
    struct Group {
        std::string         tipId, tipName;
        std::vector<size_t> jobs;
    };
    std::vector<Group> groups;
    std::vector<std::string> tipOrder;
    main([&] {
        for (const auto& [id, name] : m_machine.tips()) {
            bool onANozzle = false;
            for (const auto& n : nozzles)
                onANozzle = onANozzle || std::find(n.tipIds.begin(), n.tipIds.end(), id) != n.tipIds.end();
            if (!onANozzle) continue;
            tipOrder.push_back(id);
            Group g { id, name, {} };
            for (const size_t i : open)
                if (fits(i, id)) g.jobs.push_back(i);
            if (!g.jobs.empty()) groups.push_back(g);
        }
    });
    auto bySize = [](const Group& a, const Group& b) {
        return a.jobs.size() != b.jobs.size() ? a.jobs.size() > b.jobs.size() : a.tipName < b.tipName;
    };
    std::stable_sort(groups.begin(), groups.end(), bySize);
    for (const Group& g : groups) plannedTips.push_back(g.tipId);
    std::map<std::string, int> options;   // placements a tip shares with another
    if (groups.size() > 1) {
        for (size_t i = 0; i + 1 < groups.size(); ++i)
            for (size_t j = i + 1; j < groups.size(); ++j) {
                const size_t before = groups[j].jobs.size();
                std::erase_if(groups[j].jobs, [&](size_t k) {
                    return std::find(groups[i].jobs.begin(), groups[i].jobs.end(), k) != groups[i].jobs.end();
                });
                const int n = int(before - groups[j].jobs.size());
                // As OpenPnP: counted against the tips at these places in the machine's own order.
                if (n > 0 && i < tipOrder.size() && j < tipOrder.size()) {
                    options[tipOrder[i]] += n;
                    options[tipOrder[j]] += n;
                }
            }
        std::erase_if(groups, [](const Group& g) { return g.jobs.empty(); });
    }
    if (m_settings.jobOrder == JobOrder::NozzleTipsByFlexibility)
        std::stable_sort(groups.begin(), groups.end(), [&](const Group& a, const Group& b) {
            const int oa = options[a.tipId], ob = options[b.tipId];
            return oa != ob ? oa < ob : bySize(a, b);
        });
    std::vector<size_t> out;
    std::optional<JPLocation> start = m_previousPickStart;
    bool first = true;
    for (const Group& g : groups) {
        const std::vector<size_t> part = byPickPlaceLocation(g.jobs, start);
        if (first) {
            first = false;
            m_previousPickStart = start;
        }
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

std::optional<JPJobProcessor::Planned> JPJobProcessor::planWithout(const JPJobMachine::Nozzle& n, const std::string& tipId,
                                                                   const std::vector<size_t>& jobs,
                                                                   const std::vector<Planned>& planned) {
    if (tipId.empty()) return std::nullopt;
    std::vector<size_t> compatible;
    main([&] {
        for (const size_t i : jobs)
            if (fits(i, tipId)) compatible.push_back(i);
    });
    if (compatible.empty()) return std::nullopt;
    // The one nearest (to pick and to place) to those already planned.
    std::optional<size_t> best;
    std::optional<double> cost;
    if (m_settings.strategy != Strategy::FullyAsPlanned && !planned.empty()) {
        std::vector<JPLocation> picks, places;
        main([&] {
            for (const Planned& p : planned) {
                if (const auto& at = m_jobPlacements[p.job].plannedPickLocation) picks.push_back(*at);
                places.push_back(placeLocation(p.job));
            }
        });
        const auto pickCentre = centre(picks), placeCentre = centre(places);
        if (pickCentre && placeCentre) {
            double least = std::numeric_limits<double>::max();
            main([&] {
                for (const size_t i : compatible) {
                    const auto& at = m_jobPlacements[i].plannedPickLocation;
                    if (!at) continue;
                    const double c = distance(*at, *pickCentre) + distance(placeLocation(i), *placeCentre);
                    if (c < least) {
                        least = c;
                        best = i;
                        cost = c;
                    }
                }
            });
        }
    }
    if (!best) {
        best = compatible.front();
        cost.reset();
    }
    return Planned { n.id, tipId, *best, cost };
}

std::vector<JPJobProcessor::Planned> JPJobProcessor::planner(std::vector<size_t> jobs, std::vector<std::string> tips) {
    // OpenPnP's SimplePnpJobPlanner: a placement for each nozzle, first with
    // the tip on it (Minimize; StartAsPlanned on the first plan of a run),
    // then changing a tip for one that fits.
    if (tips.empty())
        for (const auto& [id, name] : m_machine.tips()) tips.push_back(id);
    const std::vector<JPJobMachine::Nozzle> allNozzles = m_machine.nozzles();
    struct State {
        std::vector<Planned>              planned;
        std::vector<JPJobMachine::Nozzle> nozzles;
        std::vector<std::string>          tips;
        std::vector<size_t>               jobs;
        void add(const Planned& p) {
            planned.push_back(p);
            std::erase(jobs, p.job);
            std::erase_if(nozzles, [&](const JPJobMachine::Nozzle& n) { return n.id == p.nozzleId; });
            std::erase(tips, p.tipId);
        }
    };
    State state { {}, allNozzles, tips, jobs };
    if (m_settings.strategy == Strategy::Minimize || (m_settings.strategy == Strategy::StartAsPlanned && !m_restart)) {
        int count = 0;
        for (const JPJobMachine::Nozzle& n : std::vector<JPJobMachine::Nozzle>(state.nozzles)) {
            if (const auto p = planWithout(n, n.tipId, state.jobs, state.planned)) state.add(*p);
            ++count;
            // Two nozzles: the other way round, when it is cheaper.
            if (count == 2 && state.planned.size() == 2 && state.planned[1].cost) {
                State alt { {}, allNozzles, tips, jobs };
                auto nozzleOf = [&](const std::string& id) {
                    for (const auto& m : allNozzles)
                        if (m.id == id) return m;
                    return JPJobMachine::Nozzle {};
                };
                const JPJobMachine::Nozzle second = nozzleOf(state.planned[1].nozzleId), firstN = nozzleOf(state.planned[0].nozzleId);
                if (const auto a = planWithout(second, second.tipId, alt.jobs, alt.planned)) {
                    alt.add(*a);
                    if (const auto b = planWithout(firstN, firstN.tipId, alt.jobs, alt.planned)) {
                        alt.add(*b);
                        if (alt.planned[1].cost && *alt.planned[1].cost < *state.planned[1].cost) state = alt;
                    }
                }
            }
        }
    }
    m_restart = false;
    for (const JPJobMachine::Nozzle& n : std::vector<JPJobMachine::Nozzle>(state.nozzles)) {
        // The first placement that a tip still free fits, on this nozzle.
        std::optional<Planned> p;
        for (const size_t i : state.jobs) {
            std::string good;
            for (const std::string& t : state.tips)
                if (fits(i, t) && std::find(n.tipIds.begin(), n.tipIds.end(), t) != n.tipIds.end()) {
                    good = t;
                    break;
                }
            if (!good.empty()) {
                p = planWithout(n, good, state.jobs, state.planned);
                break;
            }
        }
        if (p) state.add(*p);
    }
    return state.planned;
}

JPJobProcessor::Step JPJobProcessor::plan() {
    status("Planning placements.");
    std::vector<std::string> plannedTips;
    std::vector<size_t> jobs = ordered(openPendingWorkable(), plannedTips);
    if (jobs.empty()) return Step::Finish;
    std::stable_sort(jobs.begin(), jobs.end(), [this](size_t a, size_t b) { return m_jobPlacements[a].rank < m_jobPlacements[b].rank; });
    m_planned = planner(jobs, plannedTips);
    if (m_planned.empty()) fail(Source::None, "", "Planner failed to plan any placements. Please contact support.");
    for (const Planned& p : m_planned) {
        m_jobPlacements[p.job].status = Status::Processing;
        ++m_jobPlacements[p.job].processingCount;
    }
    m_completed.clear();
    return Step::ChangeNozzleTips;
}

// ---- The planned placements' steps -------------------------------------------

JPJobProcessor::Step JPJobProcessor::plannedStep(Step step, Step after) {
    // The sort by nozzle (OpenPnP's planner.sort) comes with the optimizing steps.
    size_t index = m_planned.size();
    for (size_t i = 0; i < m_planned.size(); ++i)
        if (m_jobPlacements[m_planned[i].job].status == Status::Processing && !m_completed.count(i)) {
            index = i;
            break;
        }
    if (index == m_planned.size()) {
        m_completed.clear();
        return after;
    }
    Planned& p = m_planned[index];
    try {
        switch (step) {
            case Step::ChangeNozzleTips: changeNozzleTip(p); break;
            case Step::Pick:             pick(p); break;
            case Step::Place:            place(p); break;
            // No part aligner yet: a part is placed as it was picked, as
            // OpenPnP does when no enabled aligner is defined.
            case Step::Align:
                JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "not aligning " << m_jobPlacements[p.job].partId
                                                          << ": no part aligner";
                break;
            default: break;
        }
        m_completed.insert(index);
        return step;
    } catch (const JobError& e) {
        JobPlacement& j = m_jobPlacements[p.job];
        JPPlacement::ErrorHandling handling = JPPlacement::ErrorHandling::Alert;
        main([&] {
            if (j.board && j.board->holder)
                if (const JPPlacement* placement = j.board->holder->find(j.placementId))
                    handling = m_job.effectiveErrorHandling(*placement);
        });
        if (handling != JPPlacement::ErrorHandling::Defer || e.failure.interrupting) throw;
        // Deferred: the feeder's fault counted; tried again later, or marked in error.
        std::string feederId;
        if (e.failure.source == Source::Feeder) feederId = e.failure.id;
        else if (e.failure.source == Source::Nozzle && m_partsFeeder.count(e.failure.id)) feederId = m_partsFeeder[e.failure.id];
        bool hasFeeder = false;
        main([&] {
            if (JPFeeder* f = feederId.empty() ? nullptr : m_config.feeder(feederId)) {
                f->recordJobFault(m_settings.feederFaultLimit, m_settings.feederFaultWindowSize);
                hasFeeder = true;
            }
        });
        if (hasFeeder && m_hooks.feedersChanged) m_hooks.feedersChanged();
        if (hasFeeder && j.processingCount < m_settings.maxPlacementRetries) {
            j.status = Status::Pending;
        } else {
            j.error = e.failure.message;
            j.status = Status::Errored;
        }
        return step;
    }
}

JPJobProcessor::Step JPJobProcessor::changeNozzleTip(Planned& p) {
    for (const JPJobMachine::Nozzle& n : m_machine.nozzles()) {
        if (n.id != p.nozzleId || n.tipId == p.tipId) continue;
        std::string tipName = p.tipId;
        for (const auto& [id, name] : m_machine.tips())
            if (id == p.tipId) tipName = name;
        status(format("Change nozzle tip on nozzle %s to %s.", n.name.c_str(), tipName.c_str()));
        std::string why;
        if (!m_machine.changeTip(n.id, p.tipId, why)) fail(Source::NozzleTip, p.tipId, why);
    }
    return Step::ChangeNozzleTips;
}

void JPJobProcessor::optimize(bool byPick) {
    std::stable_sort(m_planned.begin(), m_planned.end(), [this](const Planned& a, const Planned& b) {
        std::string na = a.nozzleId, nb = b.nozzleId;
        for (const auto& n : m_machine.nozzles()) {
            if (n.id == a.nozzleId) na = n.name;
            if (n.id == b.nozzleId) nb = n.name;
        }
        return na < nb;
    });
    if (!m_settings.optimizeMultipleNozzles || m_planned.size() <= 1) return;
    std::vector<JPLocation> places;
    main([&] {
        for (const Planned& p : m_planned) {
            if (byPick) {
                if (const auto& at = m_jobPlacements[p.job].plannedPickLocation) places.push_back(*at);
            } else {
                places.push_back(placeLocation(p.job));
            }
        }
    });
    if (places.size() != m_planned.size()) return;   // not every one has a place: as planned
    std::vector<Planned> order;
    for (const size_t i : travel(places, m_machine.cameraLocation(), std::nullopt)) order.push_back(m_planned[i]);
    m_planned = order;
}

void JPJobProcessor::prerotate(bool forPick) {
    for (Planned& p : m_planned) {
        JobPlacement& j = m_jobPlacements[p.job];
        std::optional<double> angle;
        main([&] {
            if (forPick) {
                const JPFeeder* f = feederFor(j.partId, "");
                j.plannedFeederId = f->id();
                const auto at = f->pickLocation();
                if (!at) fail(Source::Feeder, f->id(), "Feeder pick location must not be null");
                angle = at->rotation();
            } else {
                angle = placeLocation(p.job).rotation();
            }
        });
        // As OpenPnP: a nozzle that cannot turn now turns when it gets there.
        std::string why;
        if (m_settings.preRotateAllNozzles && angle) m_machine.rotate(p.nozzleId, *angle, why);
    }
}

JPJobProcessor::Step JPJobProcessor::pick(Planned& p) {
    JobPlacement& j = m_jobPlacements[p.job];
    int pickRetries = 0;
    main([&] {
        if (const JPPart* part = m_config.part(j.partId)) pickRetries = part->pickRetryCount;
    });
    std::optional<JobError> last;
    int tryLimit = 1 + pickRetries;
    for (int attempt = 0; attempt < tryLimit; ++attempt) {
        if (const auto on = m_partOn.find(p.nozzleId); on != m_partOn.end()) {
            if (on->second == j.partId) return Step::Pick;   // picked before a pause
            fail(Source::Part, j.partId,
                 "Part mismatch with part on nozzle before pick. Found " + on->second +
                     " but expected the nozzle to be empty.");
        }
        // The feed, each feeder fault retried; an empty feeder turned off.
        std::string feederId, feederName, why;
        int feedRetries = 0, feederPickRetries = 0;
        bool fed = false, empty = false;
        try {
            main([&] {
                JPFeeder* f = feederFor(j.partId, j.plannedFeederId);
                feederId = f->id();
                feederName = f->name();
                feedRetries = f->feedRetryCount();
                feederPickRetries = f->pickRetryCount();
            });
        } catch (const JobError& e) {
            last = e;
            continue;
        }
        for (int i = 0; i < 1 + feedRetries && !fed && !empty; ++i) {
            status(format("Feed %s on %s.", feederName.c_str(), j.partId.c_str()));
            main([&] {
                if (JPFeeder* f = m_config.feeder(feederId)) fed = f->feed(why, &empty);
            });
        }
        if (!fed) {
            main([&] {
                if (JPFeeder* f = m_config.feeder(feederId)) f->setEnabled(false);
            });
            if (m_hooks.feedersChanged) m_hooks.feedersChanged();
            JLOGC(JPlacerLog::kJob, JLogLevel::Info) << feederName << " disabled: " << why;
            // An empty feeder is tried once more, with the next one.
            if (empty && tryLimit == 1) tryLimit = 2;
            Failure f;
            f.source = Source::Feeder;
            f.id = feederId;
            f.message = why;
            last = JobError { f };
            continue;
        }
        // The pick, retried as the feeder says.
        bool picked = false;
        std::string pickWhy;
        for (int i = 0; i < 1 + feederPickRetries && !picked; ++i) {
            std::optional<JPLocation> at;
            main([&] {
                if (const JPFeeder* f = m_config.feeder(feederId)) at = f->pickLocation();
            });
            if (!at) {
                pickWhy = "Feeder pick location must not be null";
                continue;
            }
            std::string nozzleName = p.nozzleId;
            for (const auto& n : m_machine.nozzles())
                if (n.id == p.nozzleId) nozzleName = n.name;
            status(format("Pick %s from %s for %s using nozzle %s.", j.partId.c_str(), feederName.c_str(),
                          j.placementId.c_str(), nozzleName.c_str()));
            picked = m_machine.pick(p.nozzleId, *at, pickWhy);
        }
        if (!picked) {
            Failure f;
            f.source = Source::Feeder;
            f.id = feederId;
            f.message = pickWhy;
            last = JobError { f };
            // What may be on the nozzle is dropped before trying again.
            std::string why;
            if (!m_machine.discard(p.nozzleId, why)) fail(Source::Nozzle, p.nozzleId, why);
            continue;
        }
        m_partOn[p.nozzleId] = j.partId;
        m_partsFeeder[p.nozzleId] = feederId;
        if (m_hooks.feedersChanged) m_hooks.feedersChanged();
        return Step::Pick;
    }
    if (last) throw *last;
    return Step::Pick;
}

JPJobProcessor::Step JPJobProcessor::place(Planned& p) {
    JobPlacement& j = m_jobPlacements[p.job];
    const auto on = m_partOn.find(p.nozzleId);
    if (on == m_partOn.end()) fail(Source::Nozzle, p.nozzleId, "No part on nozzle before place.");
    if (on->second != j.partId) fail(Source::Nozzle, p.nozzleId, "Part mismatch with part on nozzle before place.");
    // Where it goes: on the board, as high as the part.
    JPLocation at(JPLengthUnit::Millimeters);
    main([&] { at = mm(placeLocation(p.job)); });
    at = at.add(JPLocation(JPLengthUnit::Millimeters, 0, 0, j.partHeightMm, 0));
    std::string nozzleName = p.nozzleId;
    for (const auto& n : m_machine.nozzles())
        if (n.id == p.nozzleId) nozzleName = n.name;
    status(format("Placing %s for %s using nozzle %s.", j.partId.c_str(), j.placementId.c_str(), nozzleName.c_str()));
    std::string why;
    if (!m_machine.place(p.nozzleId, at, why)) fail(Source::Nozzle, p.nozzleId, why);
    m_partOn.erase(p.nozzleId);
    const std::string feederId = m_partsFeeder[p.nozzleId];
    m_partsFeeder.erase(p.nozzleId);
    j.status = Status::Complete;
    ++m_totalPartsPlaced;
    main([&] {
        if (JPFeeder* f = m_config.feeder(feederId)) f->recordJobSuccess(m_settings.feederFaultWindowSize);
        if (j.board) m_job.storePlacedStatus(*j.board, j.placementId, true);
    });
    if (m_hooks.placed) m_hooks.placed();
    return Step::Place;
}

// ---- Finishing -------------------------------------------------------------

void JPJobProcessor::discardAll() {
    for (auto it = m_partOn.begin(); it != m_partOn.end();) {
        std::string why;
        if (!m_machine.discard(it->first, why)) fail(Source::Nozzle, it->first, why);
        m_partsFeeder.erase(it->first);
        it = m_partOn.erase(it);
    }
}

void JPJobProcessor::cleanup() {
    status("Cleaning up.");
    std::string why;
    if (!m_machine.safeZ(why)) fail(Source::Machine, "", why);
    discardAll();
    if (!m_machine.safeZ(why)) fail(Source::Machine, "", why);
    status("Park head.");
    if (!m_machine.park(why)) fail(Source::Machine, "", why);
}

JPJobProcessor::Step JPJobProcessor::finish() {
    cleanup();
    const double dt = seconds() - m_startSeconds;
    const double cph = dt > 0 ? m_totalPartsPlaced / (dt / 3600.0) : 0;
    int errored = 0;
    for (const JobPlacement& j : m_jobPlacements)
        if (j.status == Status::Errored) {
            ++errored;
            JLOGC(JPlacerLog::kJob, JLogLevel::Info) << j.boardId << " " << j.placementId << ": " << j.error;
        }
    if (errored > 0)
        status(format("Job finished with %d errors, placed %d parts in %.1f sec. (%.1f CPH)", errored, m_totalPartsPlaced,
                      dt, cph));
    else
        status(format("Job finished without error, placed %d parts in %.1f sec. (%.1f CPH)", m_totalPartsPlaced, dt, cph));
    m_finished = true;
    return Step::Finish;
}

} // inline namespace jf
