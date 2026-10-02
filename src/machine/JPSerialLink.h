// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLink.h"

#include <j/io/SerialPort.h>

#include <deque>
#include <string>

inline namespace jf {

// A controller on a serial port (USB CDC or a real UART).
//
// The port is CLAIMED for its whole life (JSerialPort::claim), so every byte
// is taken on the driver's I/O thread at wire speed and nothing goes through
// the main thread.
class JPSerialLink : public JPLink {
public:
    // `flow` is "none", "rtscts" or "xonxoff".
    JPSerialLink(std::string port, int baud, std::string flow);
    ~JPSerialLink() override;

    bool open(std::string& error) override;
    void close() override;
    bool isOpen() const override;
    bool write(const std::string& bytes) override;
    std::optional<std::string> readLine(int timeoutMs) override;
    std::string describe() const override;

private:
    // Move complete lines out of m_partial into m_lines.
    void split();

    std::string             m_port;
    int                     m_baud;
    std::string             m_flow;
    JSerialPort             m_serial;
    std::string             m_partial;
    std::deque<std::string> m_lines;
};

} // inline namespace jf
