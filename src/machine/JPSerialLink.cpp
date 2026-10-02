// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSerialLink.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <chrono>

inline namespace jf {

namespace {

// JSerialPort takes a fixed set of rates; anything else is a configuration error.
std::optional<JSerialPort::JBaudRate> toBaud(int baud) {
    using B = JSerialPort::JBaudRate;
    for (B b : { B::B1200_, B::B4800_, B::B9600_, B::B19200_, B::B38400_,
                 B::B57600_, B::B115200_, B::B230400_, B::B921600_ })
        if (int(b) == baud) return b;
    return std::nullopt;
}

} // namespace

JPSerialLink::JPSerialLink(std::string port, int baud, std::string flow)
    : m_port(std::move(port)), m_baud(baud), m_flow(std::move(flow)) {}

JPSerialLink::~JPSerialLink() {
    close();
}

bool JPSerialLink::open(std::string& error) {
    const auto baud = toBaud(m_baud);
    if (!baud) {
        error = std::to_string(m_baud) + " is not a supported baud rate";
        JLOGC(JPlacerLog::kLink, JLogLevel::Error) << m_port << ": " << error;
        return false;
    }
    using F = JSerialPort::JFlowCtrl;
    F flow = F::None;
    if (m_flow == "rtscts")       flow = F::Hardware;
    else if (m_flow == "xonxoff") flow = F::Software;
    else if (!m_flow.empty() && m_flow != "none") {
        error = "'" + m_flow + "' is not a flow control (none, rtscts, xonxoff)";
        return false;
    }
    if (!m_serial.open(m_port, *baud, JSerialPort::JDataBits::Eight, JSerialPort::JStopBits::One,
                       JSerialPort::JParity::None, flow)) {
        error = m_port + ": " + m_serial.lastError();
        JLOGC(JPlacerLog::kLink, JLogLevel::Error) << "open failed: " << error;
        return false;
    }
    if (!m_serial.claim()) {
        error = m_port + ": could not take the port for this connection";
        m_serial.close();
        return false;
    }
    m_partial.clear();
    m_lines.clear();
    JLOGC(JPlacerLog::kLink, JLogLevel::Info) << "opened " << describe() << ", flow control " << (m_flow.empty() ? "none" : m_flow);
    return true;
}

void JPSerialLink::close() {
    if (m_serial.isOpen()) JLOGC(JPlacerLog::kLink, JLogLevel::Info) << "closed " << describe();
    if (m_serial.isClaimed()) m_serial.release();
    if (m_serial.isOpen()) m_serial.close();
}

bool JPSerialLink::isOpen() const {
    return m_serial.isOpen();
}

bool JPSerialLink::write(const std::string& bytes) {
    const bool ok = m_serial.write(std::vector<uint8_t>(bytes.begin(), bytes.end()));
    if (!ok) JLOGC(JPlacerLog::kLink, JLogLevel::Error) << describe() << ": write failed: " << m_serial.lastError();
    return ok;
}

void JPSerialLink::split() {
    size_t eol;
    while ((eol = m_partial.find_first_of("\r\n")) != std::string::npos) {
        std::string line = m_partial.substr(0, eol);
        m_partial.erase(0, eol + 1);
        if (!line.empty()) m_lines.push_back(std::move(line));
    }
}

std::optional<std::string> JPSerialLink::readLine(int timeoutMs) {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeoutMs);
    while (m_lines.empty()) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
        if (left <= 0) return std::nullopt;
        const std::vector<uint8_t> chunk = m_serial.readClaimed(int(left));
        m_partial.append(chunk.begin(), chunk.end());
        split();
    }
    std::string line = std::move(m_lines.front());
    m_lines.pop_front();
    return line;
}

std::string JPSerialLink::describe() const {
    return m_port + " @ " + std::to_string(m_baud);
}

} // inline namespace jf
