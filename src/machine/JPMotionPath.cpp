// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A port of OpenPnP's AbstractMotionPath (Copyright (C) 2020 <mark@makr.zone>, GPL-3.0-or-later), kept to its
// structure so the two can be read side by side.

#include "JPMotionPath.h"

#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

constexpr int kSeg = JPMotionProfile::kSegments;
using P = JPMotionProfile;

double signum(double x) { return x > 0 ? 1.0 : x < 0 ? -1.0 : x; }

} // namespace

void JPMotionPath::solve(double approximation, int iterations) {
    const int size = int(m_moves.size());
    const int last = size - 1;
    if (last < 0) return;
    // The path's data.
    const size_t n = size_t(size);
    std::vector<int> leadAxis(n);
    std::vector<std::vector<double>> unitVector(n);
    std::vector<double> junctionCosineFromPrev(n);
    std::vector<int> colinearWithPrev(n);
    std::vector<bool> simplified(n);
    const std::vector<P>* prevProfiles0 = nullptr;
    for (int i = 0; i <= last; i++) {
        std::vector<P>& profiles = *m_moves[size_t(i)];
        if (profiles.empty()) return;   // zero dimensions
        unitVector[size_t(i)] = P::unitVector(profiles);
        leadAxis[size_t(i)] = P::leadAxisIndex(unitVector[size_t(i)]);
        simplified[size_t(i)] = false;
        for (const P& p : profiles)
            if (!p.isEmpty() && !p.isSupportingUncoordinated()) simplified[size_t(i)] = true;
        if (i > 0) {
            junctionCosineFromPrev[size_t(i)] = P::dotProduct(unitVector[size_t(i) - 1], unitVector[size_t(i)]);
            colinearWithPrev[size_t(i)] = 0;
            const int lead = leadAxis[size_t(i)];
            const P& p = profiles[size_t(lead)];
            const P& q = (*prevProfiles0)[size_t(lead)];
            if (lead == leadAxis[size_t(i) - 1] && simplified[size_t(i)] == simplified[size_t(i) - 1]
                && std::abs(p.vMax - q.vMax) < P::kVtol && std::abs(p.aMaxEntry - q.aMaxEntry) < P::kAtol
                && std::abs(p.aMaxExit - q.aMaxExit) < P::kAtol)
                colinearWithPrev[size_t(i)] = junctionCosineFromPrev[size_t(i)] >= 1.0 - P::kEps    ? 1
                                              : junctionCosineFromPrev[size_t(i)] <= -1.0 + P::kEps ? -1
                                                                                                  : 0;
        }
        // All solved, their first times kept.
        if (P::isCoordinated(profiles)) {
            if (profiles[size_t(leadAxis[size_t(i)])].assertSolved()) P::coordinateProfiles(profiles);
        } else {
            bool sync = false;
            for (P& p : profiles) sync = p.assertSolved() || sync;
            if (sync) P::synchronizeProfiles(profiles);
        }
        for (P& p : profiles) p.m_initialTime = p.m_time;
        prevProfiles0 = &profiles;
    }
    const int dimensions = int(unitVector[0].size());

    for (int iteration = 0; iteration < iterations; iteration++) {
        int iNext;
        bool hasUncoordinated = false;
        for (int i = 0; i <= last; i = iNext) {
            iNext = i + 1;
            std::vector<P>& profiles = *m_moves[size_t(i)];
            const std::vector<P>* prevProfiles = i > 0 ? m_moves[size_t(i) - 1] : nullptr;
            const int lead = leadAxis[size_t(i)];
            if (simplified[size_t(i)]) {
                // Only as single coordinated moves for now: no acceleration != 0 in junctions.
                if (profiles[size_t(lead)].assertSolved()) P::coordinateProfiles(profiles);
            } else if (P::isCoordinated(profiles)) {
                // Maybe a sequence of co-linear moves: a profile spanning them.
                P solverProfile = P::like(profiles[size_t(lead)]);
                const std::vector<P>* exitProfiles = &profiles;
                for (int j = i + 1; j <= last; j++) {
                    const std::vector<P>& seqProfiles = *m_moves[size_t(j)];
                    if (!(P::isCoordinated(seqProfiles) && colinearWithPrev[size_t(j)] == 1)) break;   // the sequence ends
                    // Extended to include this move, and its exit constraints (perhaps the path's).
                    solverProfile.s[kSeg] = seqProfiles[size_t(lead)].s[kSeg];
                    solverProfile.v[kSeg] = seqProfiles[size_t(lead)].v[kSeg];
                    solverProfile.a[kSeg] = seqProfiles[size_t(lead)].a[kSeg];
                    iNext = j + 1;
                    exitProfiles = &seqProfiles;
                }
                const std::vector<P>* nextProfiles = iNext <= last ? m_moves[size_t(iNext)] : nullptr;
                if (iteration > 0) {
                    // A further refinement.
                    controlOvershoot(prevProfiles, profiles, *exitProfiles, nextProfiles, lead, solverProfile, approximation);
                } else {
                    bool expandEntry = false, expandExit = false;
                    if (prevProfiles) {
                        // A coordinated previous one is a corner (co-linear ones are in the sequence): from zero
                        // velocity/acceleration. An uncoordinated one: towards its limit.
                        if (!P::isCoordinated(*prevProfiles)) {
                            const P& prev = (*prevProfiles)[size_t(lead)];
                            if (unitVector[size_t(i)][size_t(lead)] > 0) {
                                // Going positive: sMin.
                                if (std::isfinite(prev.sMin)) solverProfile.s[0] = prev.sMin;
                                else expandEntry = true;
                            } else {
                                // Going negative: sMax.
                                if (std::isfinite(prev.sMax)) solverProfile.s[0] = prev.sMax;
                                else expandEntry = true;
                            }
                        }
                        solverProfile.v[0] = 0;
                        solverProfile.a[0] = 0;
                    }
                    if (nextProfiles) {
                        if (!P::isCoordinated(*nextProfiles)) {
                            const P& next = (*nextProfiles)[size_t(lead)];
                            if (unitVector[size_t(i)][size_t(lead)] < 0) {
                                if (std::isfinite(next.sMin)) solverProfile.s[kSeg] = next.sMin;
                                else expandExit = true;
                            } else {
                                if (std::isfinite(next.sMax)) solverProfile.s[kSeg] = next.sMax;
                                else expandExit = true;
                            }
                        }
                        solverProfile.v[kSeg] = 0;
                        solverProfile.a[kSeg] = 0;
                    }
                    if (iNext > last && solverProfile.hasOption(P::Jog)) {
                        // The last move, a jog: open velocity/acceleration.
                        expandExit = true;
                        solverProfile.v[kSeg] = 0;
                        solverProfile.a[kSeg] = 0;
                    }
                    if (expandEntry || expandExit) solverProfile.solveByExpansion(signum(unitVector[size_t(i)][size_t(lead)]), expandEntry, expandExit);
                    else solverProfile.solve();   // to the boundary conditions
                }
                // Cut along the sequence (forward crossing times: a coordinated move never reverses).
                double t0 = solverProfile.forwardCrossingTime(profiles[size_t(lead)].s[0], false).value_or(0);
                for (int j = i; j < iNext; j++) {
                    std::vector<P>& seqProfiles = *m_moves[size_t(j)];
                    const double t1 = solverProfile.forwardCrossingTime(seqProfiles[size_t(lead)].s[kSeg], false).value_or(solverProfile.m_time);
                    seqProfiles[size_t(lead)].extractProfileSectionFrom(solverProfile, t0, t1);
                    P::coordinateProfilesToLead(seqProfiles, seqProfiles[size_t(lead)]);
                    t0 = t1;
                }
            } else {
                // Every solved flag cleared.
                for (int axis = 0; axis < dimensions; axis++) {
                    profiles[size_t(axis)].clearOption(P::Solved);
                    profiles[size_t(axis)].setTimeMin(0);
                }
                hasUncoordinated = true;
            }
        }

        while (hasUncoordinated) {
            hasUncoordinated = false;
            for (int i = 0; i <= last; i++) {
                std::vector<P>& profiles = *m_moves[size_t(i)];
                const std::vector<P>* prevProfiles = i > 0 ? m_moves[size_t(i) - 1] : nullptr;
                const std::vector<P>* nextProfiles = i < last ? m_moves[size_t(i) + 1] : nullptr;
                if (P::isCoordinated(profiles)) continue;
                bool hasSolved = false;
                for (int axis = 0; axis < dimensions; axis++) {
                    P& p = profiles[size_t(axis)];
                    if (p.hasOption(P::Solved)) continue;
                    bool solve = false;
                    if (!prevProfiles) {
                        solve = true;   // the path's entry conditions as they are
                    } else if ((*prevProfiles)[size_t(axis)].hasOption(P::Solved)) {
                        p.v[0] = (*prevProfiles)[size_t(axis)].v[kSeg];
                        p.a[0] = (*prevProfiles)[size_t(axis)].a[kSeg];
                        solve = true;
                    }
                    if (!nextProfiles) {
                        solve = true;   // the path's exit conditions as they are
                    } else if ((*nextProfiles)[size_t(axis)].hasOption(P::Solved)) {
                        p.v[kSeg] = (*nextProfiles)[size_t(axis)].v[0];
                        p.a[kSeg] = (*nextProfiles)[size_t(axis)].a[0];
                        solve = true;
                    }
                    if (solve) {
                        // With the given entry/exit conditions.
                        p.solve();
                        hasSolved = true;
                    } else {
                        hasUncoordinated = true;   // another pass
                    }
                }
                if (hasSolved) P::synchronizeProfiles(profiles);
            }
        }
    }
}

bool JPMotionPath::controlOvershoot(const std::vector<P>* prev, const std::vector<P>& entry, const std::vector<P>& exit,
                                    const std::vector<P>* next, int axis, P& solver, double approximation) {
    // Excess overshoot into uncoordinated moves reduced, as the time the uncoordinated move takes beyond the straight
    // line move from/to still-stand.
    bool changed = false;
    const double minf = 0.0;
    const P& e = entry[size_t(axis)];
    if (e.hasOption(P::CroppedEntry)) {
        solver.s[0] = e.sEntryControl;
        if (prev && !P::isCoordinated(*prev)) {
            const P& pp = (*prev)[size_t(axis)];
            const double tDeltaOuter = pp.m_time - pp.m_initialTime;
            const double tControl = e.tEntryControl;
            if (tControl != 0) {
                const double factor = std::max(minf, std::min(1.0, (tControl - tDeltaOuter * approximation * 0.5) / tControl));
                const double sControl = e.sEntryControl - pp.s[kSeg];
                solver.s[0] = pp.s[kSeg] + sControl * factor * factor;
                changed = true;
            }
        }
        solver.v[0] = 0;
        solver.a[0] = 0;
    }
    const P& x = exit[size_t(axis)];
    if (x.hasOption(P::CroppedExit)) {
        solver.s[kSeg] = x.sExitControl;
        if (next && !P::isCoordinated(*next)) {
            const P& np = (*next)[size_t(axis)];
            const double tDeltaOuter = np.m_time - np.m_initialTime;
            const double tControl = x.tExitControl;
            if (tControl != 0) {
                const double factor = std::max(minf, std::min(1.0, (tControl - tDeltaOuter * approximation * 0.5) / tControl));
                const double sControl = x.sExitControl - np.s[0];
                solver.s[kSeg] = np.s[0] + sControl * factor * factor;
                changed = true;
            }
        }
        solver.v[kSeg] = 0;
        solver.a[kSeg] = 0;
    }
    solver.solve();   // to the boundary conditions
    return changed;
}

std::string JPMotionPath::validate() const {
    const double sErr = std::sqrt(P::kEps), vErr = P::kVtol * 0.1, aErr = P::kAtol * 0.1;
    const std::vector<P>* prev = nullptr;
    char buf[160];
    for (size_t i = 0; i < m_moves.size(); i++) {
        const std::vector<P>& profiles = *m_moves[i];
        for (size_t axis = 0; axis < profiles.size(); axis++) {
            const P& p = profiles[axis];
            if (const auto error = p.checkValidity()) {
                std::snprintf(buf, sizeof buf, "move %zu axis %zu has error %d", i, axis, int(*error));
                return buf;
            }
            if (!prev) {
                if (p.v[0] != 0) return "axis " + std::to_string(axis) + " v[0] is not zero";
                if (!p.isConstantAcceleration() && p.a[0] != 0) return "axis " + std::to_string(axis) + " a[0] is not zero";
                continue;
            }
            const P& q = (*prev)[axis];
            if (std::abs(p.s[0] - q.s[kSeg]) > sErr) return "axis " + std::to_string(axis) + " location discontinous into move " + std::to_string(i);
            if (std::abs(p.v[0] - q.v[kSeg]) > vErr) return "axis " + std::to_string(axis) + " velocity discontinous into move " + std::to_string(i);
            if (!p.isConstantAcceleration() && std::abs(p.a[0] - q.a[kSeg]) > aErr)
                return "axis " + std::to_string(axis) + " acceleration discontinous into move " + std::to_string(i);
        }
        prev = &profiles;
    }
    return {};
}

} // inline namespace jf
