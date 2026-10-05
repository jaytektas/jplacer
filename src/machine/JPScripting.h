// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <mutex>
#include <set>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's scripting, as far as it goes outside OpenPnP's Java: scripts in
// the scripts folder (Python .py, JavaScript .js run by node, shell .sh),
// each run as a program of its own, with what it is run for as JSON in the
// environment variable JPLACER_GLOBALS (and JPLACER_EVENT, the event's name);
// what it prints goes to the log. The Scripts menu lists them; the Events
// folder's are run at OpenPnP's events ("Job.Starting", "Nozzle.BeforePick"),
// those named the event (or the event, a dot and more: Job.Starting.2.py),
// in name order; one that fails (exits other than 0) stops what it is run for.
class JPScripting {
public:
    // How long a script may run before it is stopped, in ms.
    static constexpr int kTimeoutMs = 60000;

    explicit JPScripting(std::string scriptsDirectory);
    const std::string& directory() const { return m_directory; }
    std::string eventsDirectory() const;
    // The file extensions run, and the program each is run by.
    static const std::vector<std::pair<std::string, std::string>>& interpreters();
    static bool runnable(const std::string& path);

    // A script run, waited for; false with why (its exit, its last words) when it fails.
    bool execute(const std::string& path, const JJson& globals, std::string& why, const std::string& event = "");
    // An event's scripts run; false with why when one fails.
    bool on(const std::string& event, const JJson& globals, std::string& why);
    // The scripts looked for afresh (Refresh Scripts).
    void refresh();

private:
    std::string           m_directory;
    std::mutex            m_mutex;
    std::set<std::string> m_eventsWithout;   // events found with no scripts, not looked for again
};

} // inline namespace jf
