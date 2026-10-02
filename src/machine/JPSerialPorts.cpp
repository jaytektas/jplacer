// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSerialPorts.h"

#include <j/io/SerialPort.h>

#include <filesystem>
#include <fstream>
#include <system_error>

inline namespace jf {

std::string JPSerialPorts::stablePath(const std::string& port) {
#if defined(_WIN32)
    return port;   // COM names are already what Windows keeps for a device
#else
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path target = fs::weakly_canonical(port, ec);
    if (ec) return port;
    for (const auto& e : fs::directory_iterator("/dev/serial/by-id", ec)) {
        std::error_code lec;
        if (fs::weakly_canonical(e.path(), lec) == target && !lec) return e.path().string();
    }
    return port;
#endif
}

namespace {

// Linux makes a ttyS device for every legacy serial port the kernel was built
// to allow (32 of them, typically), whether or not a UART is there; the ones
// with nothing behind them report UART type 0. They can never reach a
// controller, and listing them buries the ports that can.
bool isPlaceholder(const std::string& name) {
#if defined(_WIN32)
    (void)name;
    return false;
#else
    std::ifstream type("/sys/class/tty/" + name + "/type");
    std::string t;
    return type && (type >> t) && t == "0";
#endif
}

} // namespace

std::vector<JPSerialPorts::Port> JPSerialPorts::list() {
    std::vector<Port> out;
    for (const JSerialPortInfo& info : JSerialPort::availablePorts()) {
        const std::string name = std::filesystem::path(info.port).filename().string();
        if (isPlaceholder(name)) continue;
        std::string label = info.description.empty() ? name : info.description + " (" + name + ")";
        out.push_back({ stablePath(info.port), std::move(label) });
    }
    return out;
}

} // inline namespace jf
