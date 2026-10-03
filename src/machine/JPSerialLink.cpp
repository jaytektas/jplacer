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

JPSerialLink::JPSerialLink(Settings settings) : m_settings(std::move(settings)) {}

JPSerialLink::~JPSerialLink() {
    close();
}

bool JPSerialLink::open(std::string& error) {
    const Settings& st = m_settings;
    auto fail = [&](const std::string& why) {
        error = why;
        JLOGC(JPlacerLog::kLink, JLogLevel::Error) << st.port << ": " << error;
        return false;
    };
    const auto baud = toBaud(st.baud);
    if (!baud) return fail(std::to_string(st.baud) + " is not a supported baud rate");
    using F = JSerialPort::JFlowCtrl;
    F flow = F::None;
    if (st.flow == "rtscts")       flow = F::Hardware;
    else if (st.flow == "xonxoff") flow = F::Software;
    else if (!st.flow.empty() && st.flow != "none") return fail("'" + st.flow + "' is not a flow control (none, rtscts, xonxoff)");
    using P = JSerialPort::JParity;
    P parity = P::None;
    if (st.parity == "even")      parity = P::Even;
    else if (st.parity == "odd")  parity = P::Odd;
    else if (!st.parity.empty() && st.parity != "none") return fail("'" + st.parity + "' is not a parity (none, even, odd)");
    if (st.dataBits < 5 || st.dataBits > 8) return fail(std::to_string(st.dataBits) + " is not a number of data bits (5 to 8)");
    if (st.stopBits != 1 && st.stopBits != 2) return fail(std::to_string(st.stopBits) + " is not a number of stop bits (1 or 2)");
    if (!m_serial.open(st.port, *baud, JSerialPort::JDataBits(st.dataBits),
                       st.stopBits == 2 ? JSerialPort::JStopBits::Two : JSerialPort::JStopBits::One, parity, flow)) {
        error = st.port + ": " + m_serial.lastError();
        JLOGC(JPlacerLog::kLink, JLogLevel::Error) << "open failed: " << error;
        return false;
    }
    // Lines raised as the board wants them (some reset on DTR, some need RTS held).
    if (st.setDtr && !m_serial.setDtr(true)) JLOGC(JPlacerLog::kLink, JLogLevel::Warn) << st.port << ": could not raise DTR";
    if (st.setRts && !m_serial.setRts(true)) JLOGC(JPlacerLog::kLink, JLogLevel::Warn) << st.port << ": could not raise RTS";
    if (!m_serial.claim()) {
        error = st.port + ": could not take the port for this connection";
        m_serial.close();
        return false;
    }
    m_partial.clear();
    m_lines.clear();
    JLOGC(JPlacerLog::kLink, JLogLevel::Info) << "opened " << describe() << ", flow control "
        << (st.flow.empty() ? "none" : st.flow);
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
    const Settings& st = m_settings;
    const std::string parity = st.parity == "even" ? "E" : st.parity == "odd" ? "O" : "N";
    return st.port + " @ " + std::to_string(st.baud) + " " + std::to_string(st.dataBits) + parity + std::to_string(st.stopBits);
}

} // inline namespace jf
