// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPScriptProcess.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <chrono>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

inline namespace jf {

namespace {

void closeFd(int& fd) {
    if (fd >= 0) ::close(fd);
    fd = -1;
}

} // namespace

std::unique_ptr<JPScriptProcess> JPScriptProcess::spawn(const std::vector<std::string>& argv, const std::vector<std::string>& env,
                                                        const std::string& cwd, bool served, std::string& why) {
    // Its output; the machine's API (its requests, our answers); served, the runs and their ends.
    int out[2] = { -1, -1 }, req[2] = { -1, -1 }, rep[2] = { -1, -1 }, runs[2] = { -1, -1 }, ends[2] = { -1, -1 };
    auto closeAll = [&] {
        for (int* p : { out, req, rep, runs, ends }) {
            closeFd(p[0]);
            closeFd(p[1]);
        }
    };
    if (::pipe2(out, O_CLOEXEC) != 0 || ::pipe2(req, O_CLOEXEC) != 0 || ::pipe2(rep, O_CLOEXEC) != 0
        || (served && (::pipe2(runs, O_CLOEXEC) != 0 || ::pipe2(ends, O_CLOEXEC) != 0))) {
        closeAll();
        why = "cannot be run";
        return nullptr;
    }
    std::vector<std::string> args = argv, envs = env;
    std::vector<char*> argp, envp;
    for (std::string& a : args) argp.push_back(a.data());
    argp.push_back(nullptr);
    for (std::string& e : envs) envp.push_back(e.data());
    envp.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDERR_FILENO);
    posix_spawn_file_actions_adddup2(&actions, req[1], kRequestFd);
    posix_spawn_file_actions_adddup2(&actions, rep[0], kAnswerFd);
    if (served) {
        posix_spawn_file_actions_adddup2(&actions, runs[0], kServeFd);
        posix_spawn_file_actions_adddup2(&actions, ends[1], kServedFd);
    }
    if (!cwd.empty()) posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    pid_t pid = 0;
    const int failed = posix_spawnp(&pid, args[0].c_str(), &actions, nullptr, argp.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    if (failed) {
        closeAll();
        why = args[0] + " is not installed";
        return nullptr;
    }
    std::unique_ptr<JPScriptProcess> p(new JPScriptProcess());
    p->m_pid = pid;
    p->m_served = served;
    // Its ends of the pipes are its own now.
    closeFd(out[1]);
    closeFd(req[1]);
    closeFd(rep[0]);
    closeFd(runs[0]);
    closeFd(ends[1]);
    p->m_out = out[0];
    p->m_requests = req[0];
    p->m_answers = rep[1];
    p->m_runs = runs[1];
    p->m_ends = ends[0];
    return p;
}

JPScriptProcess::~JPScriptProcess() {
    kill();
}

void JPScriptProcess::kill() {
    for (int* fd : { &m_out, &m_requests, &m_answers, &m_runs, &m_ends }) closeFd(*fd);
    if (m_pid > 0) {
        // Served, closing its runs ends it; at once otherwise (a run that went on too long).
        if (!m_served) ::kill(m_pid, SIGKILL);
        int status = 0;
        if (m_served && ::waitpid(m_pid, &status, WNOHANG) == 0) {
            ::kill(m_pid, SIGKILL);
            ::waitpid(m_pid, &status, 0);
        } else if (!m_served) {
            ::waitpid(m_pid, &status, 0);
        }
    }
    m_pid = -1;
}

void JPScriptProcess::logLines(const std::string& name, std::string& text, std::string& last) {
    for (size_t nl; (nl = text.find('\n')) != std::string::npos;) {
        last = text.substr(0, nl);
        JLOGC(JPlacerLog::kApp, JLogLevel::Info) << name << ": " << last;
        text.erase(0, nl + 1);
    }
}

JPScriptProcess::Result JPScriptProcess::run(const std::string& name, const JJson& request,
                                             const std::function<JJson(const JJson&)>& api, int timeoutMs) {
    Result r;
    if (m_pid <= 0) return r;
    if (m_served) {
        const std::string line = request.dump() + "\n";
        if (::write(m_runs, line.data(), line.size()) != ssize_t(line.size())) {
            kill();
            return r;
        }
    }
    // What it prints, line by line to the log, until it ends (once) or says the run ended (served),
    // or its time is up; what it asks of the machine, a line of JSON each, answered a line each.
    std::string text;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool asking = true, outOpen = true, ended = false;
    while (!ended) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            r.end = Result::End::TimedOut;
            break;
        }
        pollfd p[3] = { { outOpen ? m_out : -1, POLLIN, 0 }, { asking ? m_requests : -1, POLLIN, 0 },
                        { m_served ? m_ends : -1, POLLIN, 0 } };
        if (::poll(p, 3, int(left)) <= 0) continue;
        if (p[1].revents & (POLLIN | POLLHUP)) {
            char buf[4096];
            const ssize_t n = ::read(m_requests, buf, sizeof buf);
            if (n <= 0) asking = false;   // closed its end
            else m_asked.append(buf, size_t(n));
            for (size_t nl; (nl = m_asked.find('\n')) != std::string::npos;) {
                const std::string line = m_asked.substr(0, nl);
                m_asked.erase(0, nl + 1);
                JJson answer = JJson::object();
                const JJson q = JJson::parse(line);
                if (!q.isObject()) answer["error"] = std::string("not a request: ") + line;
                else if (!api) answer["error"] = std::string("no machine to ask");
                else answer = api(q);
                const std::string reply = answer.dump() + "\n";
                if (::write(m_answers, reply.data(), reply.size()) < 0) asking = false;
            }
        }
        if (p[0].revents & (POLLIN | POLLHUP)) {
            char buf[4096];
            const ssize_t n = ::read(m_out, buf, sizeof buf);
            if (n <= 0) {
                outOpen = false;
                if (!m_served) ended = true;   // run once: it has ended
            } else {
                text.append(buf, size_t(n));
                logLines(name, text, r.last);
            }
        }
        if (m_served && (p[2].revents & (POLLIN | POLLHUP))) {
            char buf[256];
            const ssize_t n = ::read(m_ends, buf, sizeof buf);
            if (n <= 0) break;   // it has gone: Lost
            m_told.append(buf, size_t(n));
            if (const size_t nl = m_told.find('\n'); nl != std::string::npos) {
                const JJson told = JJson::parse(m_told.substr(0, nl));
                m_told.erase(0, nl + 1);
                r.end = Result::End::Exited;
                r.code = int(told["exit"].number(1));
                ended = true;
                // What it printed before saying so is waiting already.
                for (pollfd o { m_out, POLLIN, 0 }; ::poll(&o, 1, 0) > 0 && (o.revents & POLLIN);) {
                    const ssize_t k = ::read(m_out, buf, sizeof buf);
                    if (k <= 0) break;
                    text.append(buf, size_t(k));
                }
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
        closeFd(m_out);
        closeFd(m_requests);
        closeFd(m_answers);
        if (r.end == Result::End::TimedOut) ::kill(m_pid, SIGKILL);
        int status = 0;
        ::waitpid(m_pid, &status, 0);
        m_pid = -1;
        if (r.end != Result::End::TimedOut) {
            r.end = Result::End::Exited;
            r.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
    } else if (r.end != Result::End::Exited) {
        // Served, and it did not say the run ended: it is gone, or stopped now.
        ::kill(m_pid, SIGKILL);
        kill();
    }
    return r;
}

} // inline namespace jf
