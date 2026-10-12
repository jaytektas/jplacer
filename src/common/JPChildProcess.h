// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A program jplacer runs and talks to (a script's interpreter, GStreamer), the same on Linux and Windows: its
// output (what it prints, and its errors) read as channel 0, and channels of its own, each one way, numbered from
// 1 in the order given. On Linux a channel is a pipe the child has as a file descriptor of the number asked for; on
// Windows, where a program cannot be handed numbered descriptors, a named pipe the child opens by its name
// (channelNames, given to the child in its environment). Owns the child: one still running when this goes is
// killed. Used from one thread.
class JPChildProcess {
public:
    enum class Way { ToChild, FromChild };
    struct Channel {
        int    fd;    // the child's file descriptor for it (Linux)
        Way    way;
        size_t bytes = 0;   // room in it at least (a whole picture, for one that sends them); 0: the system's own
    };
    // The child's arguments and environment, given what each channel is called (channelNames' text, in their
    // order): a child told where its channels are in either.
    using Arguments = std::function<std::vector<std::string>(const std::vector<std::string>& channelNames)>;
    using Environment = std::function<std::vector<std::string>(const std::vector<std::string>& channelNames)>;

    // The arguments `args` makes run (the first found on the PATH), with the environment `env` makes (none: ours
    // as it is), in `cwd` (empty: ours). Null, and why, when it cannot be.
    static std::unique_ptr<JPChildProcess> start(const Arguments& args, const Environment& env, const std::string& cwd,
                                                 const std::vector<Channel>& channels, std::string& why);
    // The same with arguments that do not name a channel.
    static std::unique_ptr<JPChildProcess> start(const std::vector<std::string>& argv, const Environment& env,
                                                 const std::string& cwd, const std::vector<Channel>& channels,
                                                 std::string& why) {
        return start([argv](const std::vector<std::string>&) { return argv; }, env, cwd, channels, why);
    }
    ~JPChildProcess();
    JPChildProcess(const JPChildProcess&) = delete;
    JPChildProcess& operator=(const JPChildProcess&) = delete;

    // What the child opens each of its channels by, channel 1 first (its output, channel 0, it has as its
    // standard output): Windows, its pipe's name ("\\.\pipe\jplacer-…"); Linux, its descriptor's number.
    const std::vector<std::string>& channelNames() const { return m_names; }

    // What has come on channel `c` (one from the child), waiting at most `timeoutMs` for something: how many bytes
    // (at most `n`); 0 when nothing came in time; -1 when the child closed it (or ended).
    int read(size_t c, char* into, size_t n, int timeoutMs);
    // How many bytes are waiting on channel `c` now, not waiting for more.
    size_t available(size_t c);
    // Waits until one of `channels` has something to read, or is closed, at most `timeoutMs`: which (its number);
    // -1 when none in time.
    int waitAny(const std::vector<size_t>& channels, int timeoutMs);
    // All of `bytes` written to channel `c` (one to the child); false when the child has closed it.
    bool write(size_t c, const std::string& bytes);
    // Channel `c` closed (a child reading it sees its end).
    void close(size_t c);

    // Its exit code once it has ended, without waiting; none while it runs.
    std::optional<int> exited();
    // Waits for it to end: its exit code (-1: ended by a signal, killed).
    int wait();
    // Asked to end (Linux: SIGTERM; Windows, which has no asking: ended), and ended at once (SIGKILL), each not
    // waited for (wait()).
    void stop();
    void kill();

private:
    JPChildProcess();
    struct Impl;   // the system's handles (kept out of this header)
    std::unique_ptr<Impl>    m_impl;
    std::vector<std::string> m_names;
};

} // inline namespace jf
