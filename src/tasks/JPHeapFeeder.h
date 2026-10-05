// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>

inline namespace jf {

// OpenPnP's ReferenceHeapFeeder: loose parts in a heap, fetched by the
// nozzle with its vacuum on, stirred (or poked) down into the heap until the
// vacuum rises by the Vacuum Difference, dropped into the feeder's drop box,
// looked at there by the feeder's pipeline (which finds only parts the right
// way up), and picked where found; a part the wrong way up is picked and
// dropped again to turn it, and after Max flip attempts the box's parts are
// thrown away. A drop box holding another heap's parts is cleaned first:
// each part found by the box's pipeline taken back to its heap (through its
// three moves), or of unknown origin to the discard location, with the box's
// dummy part's nozzle tip.
// Runs on a thread of its own; the model is touched only through `onMain`.
class JPHeapFeeder {
public:
    using OnMain = std::function<void(const std::function<void()>&)>;

    // OpenPnP's feed with `nozzleId`: the part found set as the pick location (JPFeeder::foundPick).
    static bool feed(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId, JPJobMachine& machine,
                     const OnMain& onMain, std::string& why);
    // OpenPnP's GetSamples: the box cleaned, parts fetched into it, and the camera over it (for the template pipeline).
    static bool getSamples(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId,
                           JPJobMachine& machine, const OnMain& onMain, std::string& why);
    // OpenPnP's Clean DropBox: the feeder's box emptied.
    static bool cleanDropBox(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId,
                             JPJobMachine& machine, const OnMain& onMain, std::string& why);
};

} // inline namespace jf
