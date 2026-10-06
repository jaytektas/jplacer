// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// An interpreter jplacer runs a script in (JPScripting): its output (both
// streams) read line by line into the log, and what it asks of the machine
// (a line of JSON on fd 3) answered (a line on fd 4). Run once, it is the
// script, and its exit is the script's. Served (OpenPnP's pooled script
// engines), it stays: each run is a line of JSON to it on fd 5 ({ "path",
// "globals", "event" }), answered on fd 6 ({ "exit": code }) once the script
// is done, and it waits for the next.
class JPScriptProcess {
public:
    static constexpr int kRequestFd = 3, kAnswerFd = 4, kServeFd = 5, kServedFd = 6;

    struct Result {
        enum class End { Exited, TimedOut, Lost };
        End         end = End::Lost;
        int         code = 0;     // Exited: the script's exit code
        std::string last;         // the last line it printed
    };

    // `argv` started with `env`, in `cwd`; served or once. Null with why when it cannot be started.
    static std::unique_ptr<JPScriptProcess> spawn(const std::vector<std::string>& argv, const std::vector<std::string>& env,
                                                  const std::string& cwd, bool served, std::string& why);
    ~JPScriptProcess();
    JPScriptProcess(const JPScriptProcess&) = delete;
    JPScriptProcess& operator=(const JPScriptProcess&) = delete;

    // Run once: until it ends. Served: `request` sent, until its exit is told.
    // Its output logged under `name`; at `timeoutMs` it is killed (TimedOut,
    // and a served one is gone). A served one that ends by itself is Lost.
    Result run(const std::string& name, const JJson& request, const std::function<JJson(const JJson&)>& api, int timeoutMs);
    // Whether it is still there to serve another run.
    bool serving() const { return m_served && m_pid > 0; }

private:
    JPScriptProcess() = default;
    void kill();
    // Lines out of `text` into the log, the last kept.
    void logLines(const std::string& name, std::string& text, std::string& last);

    int  m_pid = -1;
    bool m_served = false;
    // Ours of its pipes: its output, its requests, our answers; served, the runs we send and their ends.
    int  m_out = -1, m_requests = -1, m_answers = -1, m_runs = -1, m_ends = -1;
    std::string m_asked, m_told;
};

} // inline namespace jf
