// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPFrame.h"
#include "machine/JPNozzleConfig.h"
#include "machine/JPNozzleTipConfig.h"
#include "machine/JPRunout.h"
#include "model/JPLocation.h"
#include "tasks/JPBackgroundCalibration.h"

#include <optional>
#include <string>

inline namespace jf {

class JPCameraFeed;
class JPCell;

// What the machine a job runs on (JPCellJobMachine) takes from where it runs: the open cell, its cameras and their
// views, and what the machine keeps of its nozzles and tips. The app's machine gives it; a test gives its own. Called
// where the cell's settings live (the caller's onMain).
class JPJobMachineHost {
public:
    virtual ~JPJobMachineHost() = default;

    // The open cell (none: no machine open).
    virtual JPCell* cell() const = 0;
    // Where the head camera is now (none: not known).
    virtual std::optional<JPLocation> cameraLocation() const = 0;
    // The head camera's feed, the one looking up's, a camera's by its id or name (none: no such camera).
    virtual JPCameraFeed* headCameraFeed() const = 0;
    virtual JPCameraFeed* upCameraFeed() const = 0;
    virtual JPCameraFeed* cameraFeed(const std::string& idOrName) const = 0;
    // A picture shown on a camera's view for `ms`, `text` over it; a camera's view brought to the front.
    virtual void showPicture(const JPCameraFeed* feed, const JPFrame& frame, const std::string& text, int ms) = 0;
    virtual void showCamera(const std::string& cameraId) = 0;
    // The part a nozzle holds, as the machine shows it; the nozzle chosen on the Jog panel.
    virtual std::string nozzlePart(const std::string& nozzleId) const = 0;
    virtual void        setNozzlePart(const std::string& nozzleId, const std::string& partId) = 0;
    virtual std::string chosenNozzleId() const = 0;
    // Why a tip may not be changed to (empty: it may), and the tip now on a nozzle kept.
    virtual std::string tipChangeRefusal(const std::string& nozzleId, const std::string& tipId) const = 0;
    virtual void        setTipOn(const std::string& nozzleId, const std::string& tipId) = 0;
    // The cell's file (a tip changer's template pictures are beside it); a slot's last match score kept.
    virtual std::string cellPath() const = 0;
    virtual void        slotScored(const std::string& tipId, double score) = 0;
    // A tip's runout measured on the camera looking up (`words`: what it says), and kept, with what it found of
    // the background.
    virtual std::optional<JPRunout> measureRunout(JPCell& cell, JPCameraFeed& feed, const JPNozzleConfig& nozzle,
                                                  const JPNozzleTipConfig& tip, std::string& words,
                                                  std::optional<JPBackgroundCalibration::Result>& background) = 0;
    virtual void keepRunout(const std::string& tipId, const std::string& nozzleId, const std::optional<JPRunout>& runout) = 0;
    virtual void keepBackground(const std::string& tipId, const JPBackgroundCalibration::Result& background) = 0;
};

} // inline namespace jf
