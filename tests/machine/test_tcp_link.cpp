// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A controller reached over TCP (OpenPnP's TcpCommunications): connected to
// a listener on this machine, a line sent arrives, and lines sent back come
// in one at a time, split however they arrive; a port nobody listens on is
// refused, saying so; and the link knows when the far end has gone.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPLinkFactory.h"
#include "machine/JPTcpLink.h"

#include <j/config/Json.h>

#include <string>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace jf;

namespace {

// A listener on 127.0.0.1, on a port the system chooses.
int listenOn(int& port) {
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    assert(::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && ::listen(s, 1) == 0);
    socklen_t len = sizeof a;
    ::getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    port = ntohs(a.sin_port);
    return s;
}

} // namespace

int main() {
    int port = 0;
    const int listener = listenOn(port);
    std::string received;
    std::thread controller([&] {
        const int c = ::accept(listener, nullptr, nullptr);
        char buf[64];
        const ssize_t n = ::recv(c, buf, sizeof buf, 0);
        received.assign(buf, size_t(n > 0 ? n : 0));
        // Two lines, the second split across two sends.
        const std::string reply = "Grbl 1.1f ['$' for help]\r\nok";
        ::send(c, reply.data(), reply.size(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ::send(c, "\r\n", 2, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ::close(c);
    });

    JJson settings = JJson::object();
    settings["type"] = "tcp";
    settings["host"] = "127.0.0.1";
    settings["port"] = port;
    std::string error;
    auto link = JPLinkFactory::create(settings, error);
    assert(link && link->describe() == "127.0.0.1:" + std::to_string(port));
    assert(link->open(error) && link->isOpen());
    assert(link->write("$I\n"));
    assert(link->readLine(2000) == std::optional<std::string>("Grbl 1.1f ['$' for help]"));
    assert(link->readLine(2000) == std::optional<std::string>("ok"));
    // The controller has gone: nothing more, and the link is closed.
    assert(!link->readLine(1000));
    assert(!link->isOpen());
    controller.join();
    assert(received == "$I\n");
    ::close(listener);

    // Nobody there: refused, in the system's words.
    JPTcpLink closed("127.0.0.1", port);
    assert(!closed.open(error) && error.find("127.0.0.1:") == 0);
    // No host: not even tried.
    JJson none = JJson::object();
    none["type"] = "tcp";
    assert(!JPLinkFactory::create(none, error) && error.find("IP address") != std::string::npos);
    return 0;
}
