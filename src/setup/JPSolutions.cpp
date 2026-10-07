// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSolutions.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

inline namespace jf {

namespace {

constexpr const char* kWiki = "https://github.com/openpnp/openpnp/wiki/Issues-and-Solutions";

struct MilestoneText {
    const char* name;
    const char* tag;
    const char* description;
};
// OpenPnP's milestones, their wiki anchors and what each is for.
constexpr MilestoneText kMilestones[] = {
    { "Welcome", "welcome", "Get to know jplacer by using the simulated machine. Choose your nozzle configuration." },
    { "Connect", "connect", "Connect jplacer to real controllers and cameras." },
    { "Basics", "basics", "Configure basic machine axes, motion, vacuum switching, light switching." },
    { "Kinematics", "kinematics",
      "Define machine kinematics: Safe Z, soft limits, motion control model, feed-rates, accelerations etc." },
    { "Vision", "vision", "Setup cameras and computer vision." },
    { "Calibration", "calibration", "Calibrate the machine for precision motion and vision." },
    { "Production", "production", "Configure feeders and solve other production related issues." },
    { "Advanced", "advanced", "Enable more advanced features for a faster and more automatic machine." },
};

std::string escaped(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else out += c;
    }
    return out;
}

} // namespace

const char* JPSolutions::name(Milestone m) { return kMilestones[int(m)].name; }
const char* JPSolutions::description(Milestone m) { return kMilestones[int(m)].description; }
const char* JPSolutions::tag(Milestone m) { return kMilestones[int(m)].tag; }

const char* JPSolutions::name(Severity s) {
    switch (s) {
        case Severity::None:        return "None";
        case Severity::Information: return "Information";
        case Severity::Suggestion:  return "Suggestion";
        case Severity::Warning:     return "Warning";
        case Severity::Error:       return "Error";
        case Severity::Fundamental: return "Fundamental";
    }
    return "";
}

const char* JPSolutions::name(State s) {
    switch (s) {
        case State::Open:      return "Open";
        case State::Solved:    return "Solved";
        case State::Dismissed: return "Dismissed";
    }
    return "";
}

std::string JPSolutions::Issue::fingerprint() const {
    // FNV-1a over what OpenPnP's SHA-1 covers: stable between sessions.
    const std::string text = subject + "\n" + (openpnpIssue.empty() ? issue : openpnpIssue) + "\n"
                             + (openpnpSolution.empty() ? solution : openpnpSolution);
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char out[17];
    std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
    return out;
}

void JPSolutions::find() {
    m_pending = std::make_unique<std::vector<std::shared_ptr<Issue>>>();
    for (const Check& check : m_checks) check(*this);
    addMilestoneIssue();
    // Dismissed or solved before: kept away, or shown as such.
    auto& pending = *m_pending;
    for (auto it = pending.begin(); it != pending.end();) {
        Issue& i = **it;
        const std::string f = i.fingerprint();
        if (m_dismissed.count(f)) {
            if (!m_showDismissed) {
                it = pending.erase(it);
                continue;
            }
            i.state = State::Dismissed;
        } else if (m_solved.count(f)) {
            if (i.forcedUnsolved) {
                m_solved.erase(f);
            } else if (!m_showSolved) {
                it = pending.erase(it);
                continue;
            } else {
                i.state = State::Solved;
            }
        }
        ++it;
    }
}

void JPSolutions::publish() {
    if (!m_pending) return;
    // Fundamentals first, the rest in the order found.
    std::stable_sort(m_pending->begin(), m_pending->end(), [](const auto& a, const auto& b) {
        return a->severity == Severity::Fundamental && b->severity != Severity::Fundamental;
    });
    m_issues = std::move(*m_pending);
    m_pending.reset();
}

bool JPSolutions::add(Issue issue) {
    if (!m_pending) return false;
    const std::string f = issue.fingerprint();
    for (const auto& p : *m_pending)
        if (p->fingerprint() == f) return true;
    // Nothing to do about it: only to be dismissed.
    if (issue.solution.empty()) issue.state = State::Dismissed;
    m_pending->push_back(std::make_shared<Issue>(std::move(issue)));
    return m_solved.count(f) != 0;
}

std::vector<const JPSolutions::Issue*> JPSolutions::pendingOpen() const {
    std::vector<const Issue*> out;
    if (m_pending)
        for (const auto& p : *m_pending)
            if (p->unhandled()) out.push_back(p.get());
    return out;
}

bool JPSolutions::setState(Issue& issue, State state, std::string& why) {
    if (issue.state == state) return true;
    if (issue.apply && !issue.apply(state, why)) return false;
    const std::string f = issue.fingerprint();
    if (state == State::Dismissed) {
        m_solved.erase(f);
        m_dismissed.insert(f);
    } else if (state == State::Solved) {
        m_dismissed.erase(f);
        m_solved.insert(f);
    } else {
        m_dismissed.erase(f);
        m_solved.erase(f);
    }
    issue.state = state;
    if (onChanged) onChanged();
    return true;
}

void JPSolutions::addMilestoneIssue() {
    const Milestone target = m_target;
    const bool hasNext = int(target) + 1 <= int(Milestone::Advanced);
    const bool hasPrevious = int(target) > 0;
    Issue i;
    i.subject = std::string("Milestone ") + name(target);
    i.issue = std::string("Complete milestone ") + name(target);
    i.solution = description(target);
    i.severity = Severity::Information;
    i.uri = std::string(kWiki) + "#" + tag(target) + "-milestone";
    if (hasNext) {
        const Milestone next = Milestone(int(target) + 1);
        i.choices.push_back({ name(next), std::string("Proceed to ") + name(next) + ": confirm to have completed and tested milestone "
                                              + name(target) + " (" + description(target) + "). All issues from this milestone "
                                              "should be resolved or dismissed before you proceed. You can then proceed to the next "
                                              "milestone " + name(next) + ": " + description(next) });
        i.choice = name(next);
    }
    if (hasPrevious) {
        const Milestone previous = Milestone(int(target) - 1);
        i.choices.push_back({ name(previous), std::string("Go back to ") + name(previous) + ": to limit the scope for Issues & "
                                                  "Solutions, you can go back to the previous milestone " + name(previous) + " ("
                                                  + description(previous) + ")." });
        if (i.choice.empty()) i.choice = name(previous);
    }
    i.canBeUndone = false;
    i.apply = [this, target](State to, std::string& why) {
        if (to != State::Solved) return true;
        // The issue being solved: its choice is in the published list.
        std::string choice;
        for (const auto& p : m_issues)
            if (p->issue == std::string("Complete milestone ") + name(target)) choice = p->choice;
        Milestone chosen = target;
        for (int m = 0; m <= int(Milestone::Advanced); ++m)
            if (choice == name(Milestone(m))) chosen = Milestone(m);
        if (int(chosen) == int(target) + 1) {
            // Going on: still open issues asked about first.
            find();
            std::string open;
            const std::string completing = std::string("Complete milestone ") + name(target);
            for (const Issue* p : pendingOpen())
                if (p->issue != completing) open += "\n- " + escaped(p->subject) + ": " + escaped(p->issue);
            m_pending.reset();
            if (!open.empty() && confirm) {
                // Gone on only when the answer is yes.
                confirm(std::string("Issues for milestone ") + name(target) + " are still open:" + open
                            + "\n\nIt is not recommended to switch to the next target milestone before these are resolved "
                              "or dismissed!\n\nAre you sure you still want to proceed?",
                        [this, chosen] {
                            m_target = chosen;
                            if (onChanged) onChanged();
                            if (onMilestoneChanged) onMilestoneChanged();
                        });
                why.clear();
                return false;
            }
        }
        m_target = chosen;
        if (onChanged) onChanged();
        if (onMilestoneChanged) onMilestoneChanged();
        return true;
    };
    m_pending->push_back(std::make_shared<Issue>(std::move(i)));
}

} // inline namespace jf
