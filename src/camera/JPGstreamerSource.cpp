// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPGstreamerSource.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>

extern char** environ;

inline namespace jf {

namespace {

constexpr const char* kProgram = "gst-launch-1.0";
constexpr int kFramesFd = 3;
constexpr int kStartMs = 10000;          // for the pipeline to say what it gives
constexpr int kPipeBytes = 1 << 20;      // room for a picture or so in the pipe

// The integer after `key` ("width=(int)640").
bool field(const std::string& line, const std::string& key, std::string& value) {
    const size_t at = line.find(key + "=(");
    if (at == std::string::npos) return false;
    const size_t start = line.find(')', at) + 1;
    const size_t end = line.find_first_of(",\n", start);
    value = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
    return true;
}

} // namespace

std::vector<std::string> JPGstreamerSource::words(const std::string& pipeline) {
    std::vector<std::string> out;
    std::string word;
    bool quoted = false, any = false;
    for (char ch : pipeline) {
        if (ch == '"') {
            quoted = !quoted;
            any = true;
        } else if (!quoted && (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')) {
            if (any) out.push_back(word);
            word.clear();
            any = false;
        } else {
            word += ch;
            any = true;
        }
    }
    if (any) out.push_back(word);
    return out;
}

JPGstreamerSource::JPGstreamerSource(std::string name, std::string pipeline)
    : m_name(std::move(name)), m_pipeline(std::move(pipeline)) {}

bool JPGstreamerSource::caps(const std::string& line, int& width, int& height, double& fps) {
    if (line.find("jplacercaps.GstPad:src: caps = ") == std::string::npos) return false;
    std::string w, h, f;
    if (!field(line, "width", w) || !field(line, "height", h)) return false;
    width = std::atoi(w.c_str());
    height = std::atoi(h.c_str());
    fps = 0;
    if (field(line, "framerate", f))
        if (const size_t slash = f.find('/'); slash != std::string::npos && std::atof(f.c_str() + slash + 1) > 0)
            fps = std::atof(f.c_str()) / std::atof(f.c_str() + slash + 1);
    return width > 0 && height > 0;
}

bool JPGstreamerSource::open(std::string& error) {
    close();
    if (m_pipeline.empty()) {
        error = m_name + ": no pipeline set";
        return false;
    }
    int frames[2], messages[2];
    if (::pipe2(frames, O_CLOEXEC) != 0 || ::pipe2(messages, O_CLOEXEC) != 0) {
        error = m_name + ": cannot run GStreamer";
        return false;
    }
    ::fcntl(frames[0], F_SETPIPE_SZ, kPipeBytes);
    // The pipeline as given, its output turned to RGBA and written raw to fd 3,
    // synced to the clock as OpenPnP's appsink is (an fdsink is not unless
    // told): a source that is not live (a file, a test pattern) comes at its
    // own rate, not as fast as it can.
    const std::string launch = m_pipeline + " ! videoconvert ! capsfilter name=jplacercaps caps=video/x-raw,format=RGBA ! fdsink fd="
                             + std::to_string(kFramesFd) + " sync=true";
    std::vector<std::string> args = words(launch);
    args.insert(args.begin(), { kProgram, "-v" });
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, frames[1], kFramesFd);
    posix_spawn_file_actions_adddup2(&actions, messages[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, messages[1], STDERR_FILENO);
    const int failed = posix_spawnp(&m_pid, kProgram, &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(frames[1]);
    ::close(messages[1]);
    m_frames = frames[0];
    m_messages = messages[0];
    if (failed) {
        m_pid = -1;
        close();
        error = m_name + ": GStreamer (" + std::string(kProgram) + ") is not installed";
        return false;
    }
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << m_name << ": GStreamer " << launch;
    // Until it says what it gives.
    m_width = m_height = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kStartMs);
    while (m_width <= 0) {
        if (!listen(error)) {
            close();
            return false;
        }
        if (m_width > 0) break;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            error = m_name + ": its pipeline gave no pictures within " + std::to_string(kStartMs / 1000) + " s";
            close();
            return false;
        }
        pollfd p { m_messages, POLLIN, 0 };
        ::poll(&p, 1, int(left));
    }
    return true;
}

bool JPGstreamerSource::listen(std::string& error) {
    for (;;) {
        pollfd p { m_messages, POLLIN, 0 };
        if (::poll(&p, 1, 0) <= 0) return true;
        char buf[4096];
        const ssize_t n = ::read(m_messages, buf, sizeof buf);
        if (n <= 0) {
            error = m_name + ": GStreamer stopped" + (m_said.empty() ? std::string() : ": " + m_said);
            return false;
        }
        m_said.append(buf, size_t(n));
        for (size_t nl; (nl = m_said.find('\n')) != std::string::npos;) {
            const std::string line = m_said.substr(0, nl);
            m_said.erase(0, nl + 1);
            if (m_width <= 0 && caps(line, m_width, m_height, m_fps))
                JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << m_name << ": " << m_width << "x" << m_height << " @ " << m_fps;
            if (line.rfind("ERROR", 0) == 0) {
                error = m_name + ": " + line;
                JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << error;
                return false;
            }
            JLOGC(JPlacerLog::kCamera, JLogLevel::Trace) << m_name << ": " << line;
        }
    }
}

void JPGstreamerSource::close() {
    if (m_pid > 0) {
        ::kill(m_pid, SIGTERM);
        int status = 0;
        ::waitpid(m_pid, &status, 0);
    }
    m_pid = -1;
    if (m_frames >= 0) ::close(m_frames);
    if (m_messages >= 0) ::close(m_messages);
    m_frames = m_messages = -1;
    m_said.clear();
}

std::vector<JPCaptureMode> JPGstreamerSource::modes() const {
    JPCaptureMode m;
    m.format = "GStreamer";
    m.width = m_width;
    m.height = m_height;
    m.fps = m_fps;
    return { m };
}

bool JPGstreamerSource::grab(JPFrame& frame, int timeoutMs, std::string& error) {
    if (m_frames < 0) {
        error = m_name + ": not running";
        return false;
    }
    if (!listen(error)) return false;
    const size_t size = size_t(m_width) * size_t(m_height) * 4;
    frame.rgba.resize(size);
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeoutMs);
    // A whole picture; then, while another whole one is waiting, that one (the newest).
    for (bool first = true;; first = false) {
        if (!first) {
            int waiting = 0;
            if (::ioctl(m_frames, FIONREAD, &waiting) != 0 || size_t(waiting) < size) break;
        }
        for (size_t got = 0; got < size;) {
            // None yet: as long as asked. A picture half come: the rest, given the time a start takes.
            const auto left = got == 0 ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count()
                                       : kStartMs;
            pollfd p { m_frames, POLLIN, 0 };
            if (::poll(&p, 1, int(std::max<long long>(0, left))) <= 0) {
                if (got == 0) return false;
                error = m_name + ": a picture stopped part-way";
                return false;
            }
            const ssize_t n = ::read(m_frames, frame.rgba.data() + got, size - got);
            if (n <= 0) {
                listen(error);
                if (error.empty()) error = m_name + ": GStreamer stopped";
                return false;
            }
            got += size_t(n);
        }
    }
    frame.width = m_width;
    frame.height = m_height;
    frame.captured = clock::now();
    return true;
}

} // inline namespace jf
