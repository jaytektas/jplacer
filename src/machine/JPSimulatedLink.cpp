// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSimulatedLink.h"

#include <chrono>
#include <thread>

inline namespace jf {

JPSimulatedLink::JPSimulatedLink(const JJson& config) {
    m_controller.configure(config);
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
    m_controller.receive(bytes);
    return true;
}

std::optional<std::string> JPSimulatedLink::readLine(int timeoutMs) {
    // The simulator answers inside write(), so an empty queue stays empty for
    // the whole wait; waiting it out keeps the driver's timing as on a wire.
    if (!m_controller.hasOutput()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return std::nullopt;
    }
    return m_controller.takeLine();
}

std::string JPSimulatedLink::describe() const {
    return "simulated";
}

} // inline namespace jf
