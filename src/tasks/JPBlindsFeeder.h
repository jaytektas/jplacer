// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>

inline namespace jf {

// OpenPnP's BlindsFeeder on the machine (its model: JPBlindsFeeders): the
// holder's fiducials found by the head camera (each looked at again until it
// moves less than 0.5 mm; fiducial 2, out of the camera's reach, rebuilt from
// 1 and 3), the tape's pockets found (Auto Setup), the part read by OCR, the
// cover's position seen, and the cover pushed open or closed by a free
// nozzle whose tip allows pushing (another tip loaded when none on does).
// Runs on a thread of its own; the model is touched only through `onMain`.
class JPBlindsFeeder {
public:
    using OnMain = std::function<void(const std::function<void()>&)>;

    // OpenPnP's feed: the next pocket (none left: empty), the cover opened as its actuation says (or checked open).
    static bool feed(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId, JPJobMachine& machine,
                     const OnMain& onMain, std::string& why);
    // OpenPnP's calibrateFeederLocations (Calibrate Fiducials).
    static bool calibrateFiducials(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                                   std::string& why);
    // OpenPnP's showFeatures: what vision finds from where the camera is, shown.
    static bool showFeatures(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                             std::string& why);
    // OpenPnP's findPocketsAndCenterline (Auto Setup), from where the camera is.
    static bool autoSetup(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                          std::string& why);
    // OpenPnP's performOcr: the camera over the label, the part read and acted on ("CheckCorrect", "ChangePart").
    static bool performOcr(JPConfiguration& config, const std::string& feederId, const std::string& action, JPJobMachine& machine,
                           const OnMain& onMain, std::string& why);
    // OpenPnP's calibrateCoverEdges: the cover opened and closed until it
    // lies where it should within half the pocket position tolerance (three times at most).
    static bool calibrateCoverEdges(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                                    std::string& why);
    // OpenPnP's actuateCover: pushed open or closed, with `nozzleId` when it can (else any free nozzle that can push).
    static bool actuateCover(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId, bool open,
                             JPJobMachine& machine, const OnMain& onMain, std::string& why);
    // OpenPnP's actuateAllFeederCovers: every blinds feeder whose cover is not yet so, along the shortest way.
    static bool actuateAllCovers(JPConfiguration& config, const std::string& nozzleId, bool open, JPJobMachine& machine,
                                 const OnMain& onMain, std::string& why);
    // OpenPnP's prepareForJob: visiting (calibrated, opened on job start,
    // its part read), then after all are visited (a tip loaded to push put
    // back; a part OCR changed stopping the job).
    static bool prepareForJob(JPConfiguration& config, const std::string& feederId, bool visit, JPJobMachine& machine,
                              const OnMain& onMain, std::string& why);
};

} // inline namespace jf
