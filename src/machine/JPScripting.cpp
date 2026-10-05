// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPScripting.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

inline namespace jf {

namespace fs = std::filesystem;

JPScripting::JPScripting(std::string scriptsDirectory) : m_directory(std::move(scriptsDirectory)) {
    std::error_code ec;
    fs::create_directories(eventsDirectory(), ec);
}

std::string JPScripting::eventsDirectory() const { return (fs::path(m_directory) / "Events").string(); }

const std::vector<std::pair<std::string, std::string>>& JPScripting::interpreters() {
    static const std::vector<std::pair<std::string, std::string>> k { { ".py", "python3" }, { ".js", "node" }, { ".sh", "sh" } };
    return k;
}

bool JPScripting::runnable(const std::string& path) {
    const std::string ext = fs::path(path).extension().string();
    return std::any_of(interpreters().begin(), interpreters().end(), [&ext](const auto& i) { return i.first == ext; });
}

void JPScripting::refresh() {
    std::lock_guard lk(m_mutex);
    m_eventsWithout.clear();
}

bool JPScripting::execute(const std::string& path, const JJson& globals, std::string& why, const std::string& event) {
    const std::string ext = fs::path(path).extension().string();
    std::string program;
    for (const auto& [e, p] : interpreters())
        if (e == ext) program = p;
    if (program.empty()) {
        why = path + ": no way to run a " + ext + " script";
        return false;
    }
    int out[2];
    if (::pipe2(out, O_CLOEXEC) != 0) {
        why = path + ": cannot be run";
        return false;
    }
    // Its environment: ours, and what it is run for.
    std::vector<std::string> env;
    for (char** e = environ; *e; ++e) env.emplace_back(*e);
    env.push_back("JPLACER_GLOBALS=" + globals.dump());
    env.push_back("JPLACER_EVENT=" + event);
    env.push_back("JPLACER_SCRIPTS=" + m_directory);
    std::vector<char*> envp;
    for (std::string& e : env) envp.push_back(e.data());
    envp.push_back(nullptr);
    std::string prog = program, file = path;
    char* argv[] = { prog.data(), file.data(), nullptr };
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDERR_FILENO);
    posix_spawn_file_actions_addchdir_np(&actions, fs::path(path).parent_path().c_str());
    pid_t pid = 0;
    const int failed = posix_spawnp(&pid, prog.c_str(), &actions, nullptr, argv, envp.data());
    posix_spawn_file_actions_destroy(&actions);
    ::close(out[1]);
    if (failed) {
        ::close(out[0]);
        why = fs::path(path).filename().string() + ": " + program + " is not installed";
        return false;
    }
    // What it prints, line by line to the log, until it ends or its time is up.
    std::string text, last;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTimeoutMs);
    bool timedOut = false;
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            timedOut = true;
            break;
        }
        pollfd p { out[0], POLLIN, 0 };
        if (::poll(&p, 1, int(left)) <= 0) continue;
        char buf[4096];
        const ssize_t n = ::read(out[0], buf, sizeof buf);
        if (n <= 0) break;
        text.append(buf, size_t(n));
        for (size_t nl; (nl = text.find('\n')) != std::string::npos;) {
            last = text.substr(0, nl);
            JLOGC(JPlacerLog::kApp, JLogLevel::Info) << fs::path(path).filename().string() << ": " << last;
            text.erase(0, nl + 1);
        }
    }
    ::close(out[0]);
    if (timedOut) ::kill(pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);
    if (!text.empty()) {
        last = text;
        JLOGC(JPlacerLog::kApp, JLogLevel::Info) << fs::path(path).filename().string() << ": " << text;
    }
    if (timedOut) {
        why = fs::path(path).filename().string() + " did not finish within " + std::to_string(kTimeoutMs / 1000) + " s";
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        why = fs::path(path).filename().string() + " failed" + (WIFEXITED(status) ? " (exit " + std::to_string(WEXITSTATUS(status)) + ")" : "")
            + (last.empty() ? "" : ": " + last);
        return false;
    }
    return true;
}

bool JPScripting::on(const std::string& event, const JJson& globals, std::string& why) {
    {
        std::lock_guard lk(m_mutex);
        if (m_eventsWithout.count(event)) return true;
    }
    std::vector<fs::path> scripts;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(eventsDirectory(), ec)) {
        if (!e.is_regular_file() || !runnable(e.path().string())) continue;
        const std::string base = e.path().stem().string();
        if (base == event || base.rfind(event + ".", 0) == 0) scripts.push_back(e.path());
    }
    if (scripts.empty()) {
        std::lock_guard lk(m_mutex);
        m_eventsWithout.insert(event);
        return true;
    }
    std::sort(scripts.begin(), scripts.end(), [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); });
    for (const fs::path& s : scripts) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "event " << event << ": " << s.filename().string();
        if (!execute(s.string(), globals, why, event)) return false;
    }
    return true;
}

} // inline namespace jf
