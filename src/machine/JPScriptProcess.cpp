// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPScriptProcess.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <chrono>

inline namespace jf {

std::unique_ptr<JPScriptProcess> JPScriptProcess::spawn(const std::vector<std::string>& argv, const std::vector<std::string>& env,
                                                        const std::string& cwd, bool served, std::string& why) {
    // Its requests to the machine, our answers; served, the runs we send it and its word that each ended.
    using Way = JPChildProcess::Way;
    std::vector<JPChildProcess::Channel> channels { { kRequestFd, Way::FromChild }, { kAnswerFd, Way::ToChild } };
    if (served) {
        channels.push_back({ kServeFd, Way::ToChild });
        channels.push_back({ kServedFd, Way::FromChild });
    }
    // Where they are, in its environment.
    const auto environment = [&env, served](const std::vector<std::string>& at) {
        std::vector<std::string> e = env;
        e.push_back("JPLACER_API=" + at[0] + "," + at[1]);
        if (served) e.push_back("JPLACER_SERVE=" + at[2] + "," + at[3]);
        return e;
    };
    std::string failed;
    std::unique_ptr<JPChildProcess> child = JPChildProcess::start(argv, environment, cwd, channels, failed);
    if (!child) {
        why = argv.empty() ? "cannot be run" : argv[0] + " is not installed";
        return nullptr;
    }
    std::unique_ptr<JPScriptProcess> p(new JPScriptProcess());
    p->m_child = std::move(child);
    p->m_served = served;
    return p;
}

JPScriptProcess::~JPScriptProcess() {
    kill();
}

void JPScriptProcess::kill() {
    if (!m_child) return;
    // Served, closing its runs ends it; at once otherwise (a run that went on too long).
    for (const size_t c : { kRequests, kAnswers, kRuns, kEnds, kOut }) m_child->close(c);
    if (!m_served || !m_child->exited()) m_child->kill();
    m_child->wait();
    m_child.reset();
}

void JPScriptProcess::logLines(const std::string& name, std::string& text, std::string& last) {
    for (size_t nl; (nl = text.find('\n')) != std::string::npos;) {
        last = text.substr(0, nl);
        if (!last.empty() && last.back() == '\r') last.pop_back();
        JLOGC(JPlacerLog::kApp, JLogLevel::Info) << name << ": " << last;
        text.erase(0, nl + 1);
    }
}

JPScriptProcess::Result JPScriptProcess::run(const std::string& name, const JJson& request,
                                             const std::function<JJson(const JJson&)>& api, int timeoutMs) {
    Result r;
    if (!m_child) return r;
    if (m_served && !m_child->write(kRuns, request.dump() + "\n")) {
        kill();
        return r;
    }
    // What it prints, line by line to the log, until it ends (once) or says the run ended (served),
    // or its time is up; what it asks of the machine, a line of JSON each, answered a line each.
    std::string text;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool asking = true, outOpen = true, ended = false;
    char buf[4096];
    while (!ended) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            r.end = Result::End::TimedOut;
            break;
        }
        std::vector<size_t> watched;
        if (outOpen) watched.push_back(kOut);
        if (asking) watched.push_back(kRequests);
        if (m_served) watched.push_back(kEnds);
        if (watched.empty()) break;
        const int ready = m_child->waitAny(watched, int(left));
        if (ready < 0) continue;
        if (size_t(ready) == kRequests) {
            const int n = m_child->read(kRequests, buf, sizeof buf, 0);
            if (n < 0) asking = false;   // closed its end
            else m_asked.append(buf, size_t(n));
            for (size_t nl; (nl = m_asked.find('\n')) != std::string::npos;) {
                const std::string line = m_asked.substr(0, nl);
                m_asked.erase(0, nl + 1);
                JJson answer = JJson::object();
                const JJson q = JJson::parse(line);
                if (!q.isObject()) answer["error"] = std::string("not a request: ") + line;
                else if (!api) answer["error"] = std::string("no machine to ask");
                else answer = api(q);
                if (!m_child->write(kAnswers, answer.dump() + "\n")) asking = false;
            }
        } else if (size_t(ready) == kOut) {
            const int n = m_child->read(kOut, buf, sizeof buf, 0);
            if (n < 0) {
                outOpen = false;
                if (!m_served) ended = true;   // run once: it has ended
            } else {
                text.append(buf, size_t(n));
                logLines(name, text, r.last);
            }
        } else if (size_t(ready) == kEnds) {
            const int n = m_child->read(kEnds, buf, sizeof buf, 0);
            if (n < 0) break;   // it has gone: Lost
            m_told.append(buf, size_t(n));
            if (const size_t nl = m_told.find('\n'); nl != std::string::npos) {
                const JJson told = JJson::parse(m_told.substr(0, nl));
                m_told.erase(0, nl + 1);
                r.end = Result::End::Exited;
                r.code = int(told["exit"].number(1));
                ended = true;
                // What it printed before saying so is waiting already.
                for (int k; (k = m_child->read(kOut, buf, sizeof buf, 0)) > 0;) text.append(buf, size_t(k));
                logLines(name, text, r.last);
            }
        }
        if (m_served && !outOpen && !ended) break;   // it has gone: Lost
    }
    if (!text.empty()) {
        r.last = text;
        JLOGC(JPlacerLog::kApp, JLogLevel::Info) << name << ": " << text;
    }
    if (!m_served) {
        // Run once: its exit is the script's.
        if (r.end == Result::End::TimedOut) m_child->kill();
        const int code = m_child->wait();
        m_child.reset();
        if (r.end != Result::End::TimedOut) {
            r.end = Result::End::Exited;
            r.code = code;
        }
    } else if (r.end != Result::End::Exited) {
        // Served, and it did not say the run ended: it is gone, or stopped now.
        kill();
    }
    return r;
}

} // inline namespace jf
