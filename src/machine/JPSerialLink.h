// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLink.h"

#include <j/io/SerialPort.h>

#include <deque>
#include <string>
#include <vector>

inline namespace jf {

// A controller on a serial port (USB CDC or a real UART).
//
// The port is CLAIMED for its whole life (JSerialPort::claim), so every byte
// is taken on the driver's I/O thread at wire speed and nothing goes through
// the main thread.
class JPSerialLink : public JPLink {
public:
    // How the port is set up, as OpenPnP's serial settings are.
    struct Settings {
        std::string port;
        int         baud = 115200;
        std::string flow;            // "none" (or empty), "rtscts", "xonxoff"
        int         dataBits = 8;    // 5 to 8
        int         stopBits = 1;    // 1 or 2
        std::string parity;          // "none" (or empty), "even", "odd"
        bool        setDtr = false;  // raise DTR once open (else left as the system has it)
        bool        setRts = false;  // raise RTS once open
    };

    explicit JPSerialLink(Settings settings);
    ~JPSerialLink() override;

    bool open(std::string& error) override;
    void close() override;
    bool isOpen() const override;
    bool write(const std::string& bytes) override;
    std::optional<std::string> readLine(int timeoutMs) override;
    std::string describe() const override;
    // The bytes received within `timeoutMs` as they come, for a controller
    // that speaks in bytes, not lines (JPNeoden4Link); not mixed with readLine.
    std::vector<uint8_t> readBytes(int timeoutMs);

private:
    // Move complete lines out of m_partial into m_lines.
    void split();

    Settings                m_settings;
    JSerialPort             m_serial;
    std::string             m_partial;
    std::deque<std::string> m_lines;
};

} // inline namespace jf
