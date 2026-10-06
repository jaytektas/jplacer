// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "tasks/JPJobMachineHost.h"

inline namespace jf {

class JPlacerMachine;

// The app's machine as the job machine's host (JPJobMachineHost): its cell, cameras and views, and what it keeps.
class JPlacerJobHost : public JPJobMachineHost {
public:
    explicit JPlacerJobHost(JPlacerMachine& machine) : m_machine(machine) {}

    JPCell* cell() const override;
    std::optional<JPLocation> cameraLocation() const override;
    JPCameraFeed* headCameraFeed() const override;
    JPCameraFeed* upCameraFeed() const override;
    JPCameraFeed* cameraFeed(const std::string& idOrName) const override;
    void showPicture(const JPCameraFeed* feed, const JPFrame& frame, const std::string& text, int ms) override;
    void showCamera(const std::string& cameraId) override;
    std::string nozzlePart(const std::string& nozzleId) const override;
    void        setNozzlePart(const std::string& nozzleId, const std::string& partId) override;
    std::string chosenNozzleId() const override;
    std::string tipChangeRefusal(const std::string& nozzleId, const std::string& tipId) const override;
    void        setTipOn(const std::string& nozzleId, const std::string& tipId) override;
    std::string cellPath() const override;
    void        slotScored(const std::string& tipId, double score) override;
    std::optional<JPRunout> measureRunout(JPCell& cell, JPCameraFeed& feed, const JPNozzleConfig& nozzle, const JPNozzleTipConfig& tip,
                                          std::string& words, std::optional<JPBackgroundCalibration::Result>& background) override;
    void keepRunout(const std::string& tipId, const std::string& nozzleId, const std::optional<JPRunout>& runout) override;
    void keepBackground(const std::string& tipId, const JPBackgroundCalibration::Result& background) override;

private:
    JPlacerMachine& m_machine;
};

} // inline namespace jf
