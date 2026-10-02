// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <vector>

inline namespace jf {

// The serial ports a controller could be on, named so the choice survives a
// reboot.
//
// /dev/ttyACM0 and friends are numbered in the order devices happen to
// enumerate, so a cell that names one finds a different device (or none) the
// next time two USB devices come up the other way round. On Linux every port
// also has a stable name under /dev/serial/by-id, built from the device's
// maker, product and serial number; that is the name offered and stored.
class JPSerialPorts {
public:
    struct Port {
        std::string path;    // what a cell stores: the stable name where there is one
        std::string label;   // what a person reads: the device's own description and its port
    };

    static std::vector<Port> list();

    // The stable name for `port` (/dev/ttyACM1 -> /dev/serial/by-id/usb-...),
    // or `port` itself when it has none.
    static std::string stablePath(const std::string& port);
};

} // inline namespace jf
