// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSerialPorts.h"

#include <j/io/SerialPort.h>

#include <filesystem>
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

std::vector<JPSerialPorts::Port> JPSerialPorts::list() {
    std::vector<Port> out;
    for (const JSerialPortInfo& info : JSerialPort::availablePorts()) {
        const std::string name = std::filesystem::path(info.port).filename().string();
        std::string label = info.description.empty() ? name : info.description + " (" + name + ")";
        out.push_back({ stablePath(info.port), std::move(label) });
    }
    return out;
}

} // inline namespace jf
