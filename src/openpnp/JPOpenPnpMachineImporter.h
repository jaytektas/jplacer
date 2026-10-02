// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCellConfig.h"

#include <string>
#include <vector>

inline namespace jf {

// Turns an OpenPnP machine.xml into a jplacer cell configuration: its
// G-code controllers and their links, axes (controller, virtual, mapped),
// heads, nozzles, cameras and actuators, with OpenPnP's command templates
// rewritten into jplacer's.
//
// What has no jplacer equivalent yet (an axis kind, a driver type, a
// template variable) is left out or kept as it was, and said in `notes`, so
// the person importing knows exactly what to check.
class JPOpenPnpMachineImporter {
public:
    static bool import(const std::string& machineXml, JPCellConfig& cell,
                       std::vector<std::string>& notes, std::string& error);
};

} // inline namespace jf
