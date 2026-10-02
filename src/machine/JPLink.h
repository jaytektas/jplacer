// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <optional>
#include <string>

inline namespace jf {

// The connection to one controller: bytes out, lines in.
//
// Used only from the owning driver's I/O thread. Implementations: a serial
// port (JPSerialLink), a simulated controller (JPSimulatedLink).
class JPLink {
public:
    virtual ~JPLink() = default;

    // False with `error` in the system's words when the link cannot open.
    virtual bool open(std::string& error) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // Send bytes as given: a command line with its end-of-line, or a single
    // real-time byte. False when the link has failed.
    virtual bool write(const std::string& bytes) = 0;

    // The next complete line received, without its end-of-line; nothing if
    // none arrives within `timeoutMs`.
    virtual std::optional<std::string> readLine(int timeoutMs) = 0;

    // For logs and the UI: "/dev/ttyACM0 @ 115200", "simulated".
    virtual std::string describe() const = 0;
};

} // inline namespace jf
