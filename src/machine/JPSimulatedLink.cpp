// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSimulatedLink.h"

#include <j/config/Json.h>

#include <chrono>
#include <thread>

inline namespace jf {

JPSimulatedLink::JPSimulatedLink(const JJson& config) : m_gcodeServer(config["kind"].str() == "gcodeServer") {
    if (m_gcodeServer) m_server.configure(config);
    else m_controller.configure(config);
}

bool JPSimulatedLink::hasOutput() const {
    return m_gcodeServer ? m_server.hasOutput() : m_controller.hasOutput();
}

std::string JPSimulatedLink::takeLine() {
    return m_gcodeServer ? m_server.takeLine() : m_controller.takeLine();
}

bool JPSimulatedLink::open(std::string&) {
    m_open = true;
    return true;
}

void JPSimulatedLink::close() {
    m_open = false;
}

bool JPSimulatedLink::isOpen() const {
    return m_open;
}

bool JPSimulatedLink::write(const std::string& bytes) {
    if (!m_open) return false;
    if (m_gcodeServer) m_server.receive(bytes);
    else m_controller.receive(bytes);
    return true;
}

std::optional<std::string> JPSimulatedLink::readLine(int timeoutMs) {
    // The simulator answers inside write(), so an empty queue stays empty for
    // the whole wait; waiting it out keeps the driver's timing as on a wire.
    if (!hasOutput()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return std::nullopt;
    }
    return takeLine();
}

std::string JPSimulatedLink::describe() const {
    return m_gcodeServer ? "GcodeServer" : "simulated";
}

} // inline namespace jf
