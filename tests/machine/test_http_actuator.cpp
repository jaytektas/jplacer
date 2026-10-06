// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's HttpActuator: switched on by a GET of its On URL, off with no Off
// URL by its Param URL with 0, set by the Param URL with {val} the value, the
// same URL not asked twice running; read by its Read URL, the regex's
// "Value" group of each line (OpenPnP's HttpActuatorTest among them).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace jf;

int main() {
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && ::listen(listener, 8) == 0);
    socklen_t len = sizeof a;
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&a), &len);
    const std::string base = "http://127.0.0.1:" + std::to_string(ntohs(a.sin_port));

    std::mutex m;
    std::vector<std::string> asked;   // the paths asked for
    std::atomic<bool> stop { false };
    std::thread server([&] {
        while (!stop) {
            const int c = ::accept(listener, nullptr, nullptr);
            if (c < 0) break;
            char buf[2048];
            const ssize_t n = ::recv(c, buf, sizeof buf, 0);
            const std::string req(buf, size_t(n > 0 ? n : 0));
            const std::string path = req.substr(4, req.find(' ', 4) - 4);
            {
                std::lock_guard lk(m);
                asked.push_back(path);
            }
            const std::string body = path == "/read" ? "temp: 21.5\r\nhumidity: 40\r\n" : path == "/msr" ? "read:42" : "ok";
            const std::string reply = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size())
                                    + "\r\nConnection: close\r\n\r\n" + body;
            ::send(c, reply.data(), reply.size(), 0);
            ::close(c);
        }
    });

    JPCellConfig config;
    config.name = "Http";
    JPActuatorConfig h;
    h.id = "H";
    h.name = "Lamp";
    h.http.on = true;
    h.http.onUrl = base + "/on";
    h.http.paramUrl = base + "/set?v={val}";
    h.http.readUrl = base + "/read";
    h.http.regex = "temp: (?<Value>[0-9.]+)";
    config.actuators.push_back(h);
    assert(config.problems().empty());
    JPCell cell(config, {});
    std::string why, value;
    assert(cell.switchActuatorAndWait("H", true, why));
    assert(cell.switchActuatorAndWait("H", true, why));   // the same URL: not asked again
    assert(cell.switchActuatorAndWait("H", false, why));  // no Off URL: the Param URL with 0
    assert(cell.setActuatorAndWait("H", "7", why));
    assert(cell.readActuatorAndWait("H", std::nullopt, value, why) && value == "21.5");
    // OpenPnP's HttpActuatorTest: its Read URL answering "read:42", its regex read:(?<Value>-?\d+): 42.
    {
        JPCellConfig msr;
        msr.name = "HttpActuatorTest";
        JPActuatorConfig r;
        r.id = "M";
        r.name = "Measure";
        r.http.on = true;
        r.http.readUrl = base + "/msr";
        r.http.regex = "read:(?<Value>-?\\d+)";
        msr.actuators.push_back(r);
        JPCell measuring(msr, {});
        assert(measuring.readActuatorAndWait("M", std::nullopt, value, why) && std::stod(value) == 42.0);
    }
    stop = true;
    ::shutdown(listener, SHUT_RDWR);
    ::close(listener);
    server.join();
    std::lock_guard lk(m);
    assert((asked == std::vector<std::string> { "/on", "/set?v=0", "/set?v=7", "/read", "/msr" }));
    return 0;
}
