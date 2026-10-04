// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's Issues & Solutions (Solutions): what is wrong with, or could be
// better in, the machine and the job's setup, each with what to do about
// it. A search (find) asks every check for its issues; those dismissed
// before stay away (or are shown as Dismissed with Include Dismissed?), and
// those solved before likewise (with Include Solved?, unless their cause is
// back). The checks are limited by the target milestone: the machine is set
// up a milestone at a time, Welcome to Advanced, and the last issue is
// always to complete the milestone (Accept goes on to the next, after
// asking when issues are still open; or back to the previous).
class JPSolutions {
public:
    // OpenPnP's Milestone, in order.
    enum class Milestone { Welcome, Connect, Basics, Kinematics, Vision, Calibration, Production, Advanced };
    enum class Severity { None, Information, Suggestion, Warning, Error, Fundamental };
    enum class State { Open, Solved, Dismissed };

    static const char* name(Milestone m);
    static const char* description(Milestone m);
    // Its tag in OpenPnP's wiki page anchor ("#welcome-milestone").
    static const char* tag(Milestone m);
    static const char* name(Severity s);
    static const char* name(State s);

    // What an issue offers to change, shown with it (OpenPnP's CustomProperty).
    struct Property {
        enum class Kind { Text, Flag, Integer, Number, Length, Action };
        Kind        kind = Kind::Text;
        std::string label, tooltip;
        int         min = 0, max = 0;   // Integer
        std::function<std::string()>          getText;
        std::function<void(const std::string&)> setText;
        std::function<bool()>                 getFlag;
        std::function<void(bool)>             setFlag;
        std::function<double()>               getNumber;   // Integer, Number, Length (mm)
        std::function<void(double)>           setNumber;
        std::string                           actionLabel;   // Action: its button
        std::function<void()>                 action;
    };
    // One of several ways to solve it (OpenPnP's Choice).
    struct Choice {
        std::string value, description;
    };
    struct Issue {
        std::string subject;          // what it is about ("ReferenceNozzle N1")
        std::string issue, solution;  // an empty solution: nothing to do but dismiss
        Severity    severity = Severity::Information;
        std::string uri;              // more about it (OpenPnP's wiki), empty: none
        State       state = State::Open;
        std::string extendedDescription;
        std::vector<Property> properties;
        std::vector<Choice>   choices;
        std::string           choice;   // the value chosen
        bool canBeAccepted = true, canBeUndone = true;
        // Never counted as unhandled (a conservative fall-back offered).
        bool neverUnhandled = false;
        // Its cause is back: shown again though solved before.
        bool forcedUnsolved = false;
        // Done when it goes to a new state: Solved applies the solution,
        // leaving Solved undoes it. False, and why, when it could not be.
        std::function<bool(State to, std::string& why)> apply;
        // Selected: what it needs ready (the nozzle chosen on the Jog panel).
        std::function<void()> activate;

        bool        unhandled() const { return !neverUnhandled && state == State::Open; }
        // Stable across searches: what it is about, the issue and the solution.
        std::string fingerprint() const;
    };
    // What a search adds issues to.
    using Check = std::function<void(JPSolutions&)>;

    // The checks a search runs, in order.
    void setChecks(std::vector<Check> checks) { m_checks = std::move(checks); }

    Milestone targetMilestone() const { return m_target; }
    void      setTargetMilestone(Milestone m) { m_target = m; }
    bool      isTargeting(Milestone m) const { return int(m_target) >= int(m); }
    bool      isAtMostTargeting(Milestone m) const { return int(m_target) <= int(m); }
    bool      showSolved() const { return m_showSolved; }
    void      setShowSolved(bool on) { m_showSolved = on; }
    bool      showDismissed() const { return m_showDismissed; }
    void      setShowDismissed(bool on) { m_showDismissed = on; }

    // The fingerprints solved and dismissed, kept between sessions.
    const std::set<std::string>& solvedFingerprints() const { return m_solved; }
    const std::set<std::string>& dismissedFingerprints() const { return m_dismissed; }
    void setFingerprints(std::set<std::string> solved, std::set<std::string> dismissed) {
        m_solved = std::move(solved);
        m_dismissed = std::move(dismissed);
    }

    // A search: every check run, then the milestone's issue; what was
    // solved or dismissed before kept away or shown as such. Shown once
    // published (OpenPnP's findIssues, publishIssues): fundamentals first.
    void find();
    void publish();
    // From inside a check: an issue found (one already found is not added
    // twice). True when it was solved before.
    bool add(Issue issue);
    // Every issue found by the last search not yet published, open: for a
    // milestone's completion to say what is still open.
    std::vector<const Issue*> pendingOpen() const;

    const std::vector<std::shared_ptr<Issue>>& issues() const { return m_issues; }
    // Move an issue to a state, doing (or undoing) its solution; the
    // fingerprints kept. False, and why, when it could not be.
    bool setState(Issue& issue, State state, std::string& why);

    // Asks before going on with a milestone's issues still open (OpenPnP's
    // confirm); `yes` goes on.
    std::function<void(const std::string& message, std::function<void()> yes)> confirm;
    // Kept: the target milestone, the fingerprints, what is shown.
    std::function<void()> onChanged;
    // A milestone completed or gone back to: searched again.
    std::function<void()> onMilestoneChanged;

private:
    void addMilestoneIssue();

    std::vector<Check>                  m_checks;
    Milestone                           m_target = Milestone::Welcome;
    bool                                m_showSolved = false, m_showDismissed = false;
    std::set<std::string>               m_solved, m_dismissed;
    std::vector<std::shared_ptr<Issue>> m_issues;
    std::unique_ptr<std::vector<std::shared_ptr<Issue>>> m_pending;
};

} // inline namespace jf
