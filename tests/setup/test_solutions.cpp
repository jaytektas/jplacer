// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Issues & Solutions: a search's issues (twice found, kept once) with the
// milestone's last; dismissed and solved ones kept away on the next search,
// or shown so when asked; a solution done on Accept and undone on Reopen;
// the milestone completed (asked first while issues are open) and gone back.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "setup/JPSolutions.h"

#include <functional>

using namespace jf;
using S = JPSolutions;

int main() {
    S s;
    int applied = 0;
    s.setChecks({ [&applied](S& sol) {
        S::Issue a;
        a.subject = "Camera Top";
        a.issue = "Camera Top is not calibrated.";
        a.solution = "Calibrate it.";
        a.severity = S::Severity::Error;
        sol.add(a);
        sol.add(a);   // the same again: once
        S::Issue b;
        b.subject = "Machine";
        b.issue = "Fundamental thing";
        b.solution = "Do it.";
        b.severity = S::Severity::Fundamental;
        b.apply = [&applied](S::State to, std::string&) {
            applied += to == S::State::Solved ? 1 : -1;
            return true;
        };
        sol.add(b);
    } });
    s.find();
    s.publish();
    // Fundamentals first; the milestone's issue last.
    assert(s.issues().size() == 3);
    assert(s.issues()[0]->severity == S::Severity::Fundamental);
    assert(s.issues()[2]->issue == "Complete milestone Welcome");

    std::string why;
    // Accept the fundamental: done; Reopen: undone.
    assert(s.setState(*s.issues()[0], S::State::Solved, why) && applied == 1);
    assert(s.setState(*s.issues()[0], S::State::Open, why) && applied == 0);
    // Solve it and dismiss the camera's: gone on the next search.
    assert(s.setState(*s.issues()[0], S::State::Solved, why));
    assert(s.setState(*s.issues()[1], S::State::Dismissed, why));
    s.find();
    s.publish();
    assert(s.issues().size() == 1);
    // Shown so when asked.
    s.setShowSolved(true);
    s.setShowDismissed(true);
    s.find();
    s.publish();
    assert(s.issues().size() == 3);
    int solved = 0, dismissed = 0;
    for (const auto& i : s.issues()) {
        solved += i->state == S::State::Solved;
        dismissed += i->state == S::State::Dismissed;
    }
    assert(solved == 1 && dismissed == 1);

    // The milestone: going on with an open issue asks; refused, it stays.
    s.setShowSolved(false);
    s.setShowDismissed(false);
    assert(s.setState(*s.issues()[0], S::State::Open, why));   // the fundamental open again
    s.setFingerprints({}, {});
    s.find();
    s.publish();
    std::function<void()> yes;
    s.confirm = [&yes](const std::string&, std::function<void()> onYes) { yes = std::move(onYes); };
    int searched = 0;
    s.onMilestoneChanged = [&searched] { ++searched; };
    S::Issue& milestone = *s.issues().back();
    assert(milestone.choice == "Connect");
    // Asked, not gone on until the answer is yes.
    assert(!s.setState(milestone, S::State::Solved, why) && yes && s.targetMilestone() == S::Milestone::Welcome);
    yes();
    assert(s.targetMilestone() == S::Milestone::Connect && searched == 1);
    // Back again.
    s.find();
    s.publish();
    S::Issue& next = *s.issues().back();
    next.choice = "Welcome";
    assert(s.setState(next, S::State::Solved, why) && s.targetMilestone() == S::Milestone::Welcome);
    assert(s.isTargeting(S::Milestone::Welcome) && !s.isTargeting(S::Milestone::Connect));
    return 0;
}
