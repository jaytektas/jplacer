// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A program run and talked to (JPChildProcess): what it prints read as channel 0, a channel to it and one from it
// (its descriptors 3 and 4 here; named pipes on Windows), each read waited for no longer than asked; how many bytes
// wait; its exit code; one that does not end, killed; one that cannot be started, said so.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "common/JPChildProcess.h"

#include <chrono>

using namespace jf;

int main() {
    std::string why;
    // Prints a line, then sends back what it is sent, then ends with 7.
    auto c = JPChildProcess::start({ "sh", "-c", "echo hello; read line <&3; echo \"got $line\" >&4; exit 7" }, nullptr, "",
                                   { { 3, JPChildProcess::Way::ToChild }, { 4, JPChildProcess::Way::FromChild } }, why);
    assert(c && why.empty());
    assert(c->channelNames() == (std::vector<std::string> { "3", "4" }));
    char buf[256];
    std::string out;
    while (out.find('\n') == std::string::npos) {
        const int n = c->read(0, buf, sizeof buf, 2000);
        assert(n > 0);
        out.append(buf, size_t(n));
    }
    assert(out == "hello\n");
    assert(c->write(1, "there\n"));
    assert(c->waitAny({ 0, 2 }, 2000) == 2);
    std::string back;
    while (back.find('\n') == std::string::npos) {
        const int n = c->read(2, buf, sizeof buf, 2000);
        assert(n > 0);
        back.append(buf, size_t(n));
    }
    assert(back == "got there\n");
    assert(c->wait() == 7 && c->exited() == 7);
    assert(c->read(0, buf, sizeof buf, 100) == -1);   // its output closed with it

    // Nothing comes in time: 0, not waited for longer.
    auto quiet = JPChildProcess::start({ "sleep", "30" }, nullptr, "", {}, why);
    assert(quiet && !quiet->exited());
    const auto t0 = std::chrono::steady_clock::now();
    assert(quiet->read(0, buf, sizeof buf, 150) == 0);
    assert(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1000));
    assert(quiet->available(0) == 0);
    quiet->kill();
    assert(quiet->wait() == -1);

    // Its environment as made, given the channels' names.
    auto told = JPChildProcess::start({ "sh", "-c", "echo $WHERE" }, [](const std::vector<std::string>& names) {
        return std::vector<std::string> { "WHERE=" + names.at(0), "PATH=/usr/bin:/bin" };
    }, "", { { 5, JPChildProcess::Way::FromChild } }, why);
    assert(told);
    std::string said;
    for (int n; (n = told->read(0, buf, sizeof buf, 2000)) > 0;) said.append(buf, size_t(n));
    assert(said == "5\n" && told->wait() == 0);

    // Not there: not started, and why.
    assert(!JPChildProcess::start({ "jplacer-no-such-program" }, nullptr, "", {}, why) && !why.empty());
    return 0;
}
