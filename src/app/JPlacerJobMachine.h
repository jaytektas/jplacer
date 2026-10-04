// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerMachine.h"

#include "tasks/JPJobMachine.h"

#include <atomic>
#include <functional>
#include <string>

inline namespace jf {

// The machine a job runs on (JPJobProcessor's JPJobMachine): the open
// cell's head and its nozzles, moved and asked through the cell's waiting
// calls on the job's thread. A tip is changed by its changer steps as the
// Jog panel's tip menu changes it (refused as that refuses), a fiducial
// found by the head camera as the visual test finds the homing mark:
// centred on, then measured again until it moves less than 0.2 mm.
// Whatever is read of the cell's settings is read through `onMain`.
class JPlacerJobMachine : public JPJobMachine {
public:
    using OnMain = std::function<void(const std::function<void()>&)>;
    // `ask`: a tip changer's question for the person, waiting for the answer.
    JPlacerJobMachine(JPlacerMachine& machine, OnMain onMain, std::function<bool(const std::string&)> ask,
                      std::function<void(const std::string&)> progress);

    std::vector<Nozzle> nozzles() const override;
    std::vector<std::pair<std::string, std::string>> tips() const override;
    std::optional<JPLocation> cameraLocation() const override;
    bool safeZ(std::string& why) override;
    bool changeTip(const std::string& nozzleId, const std::string& tipId, std::string& why) override;
    bool rotate(const std::string& nozzleId, double angle, std::string& why) override;
    bool pick(const std::string& nozzleId, const JPLocation& at, std::string& why) override;
    bool place(const std::string& nozzleId, const JPLocation& at, std::string& why) override;
    bool discard(const std::string& nozzleId, std::string& why) override;
    bool positionNozzle(const std::string& nozzleId, const JPLocation& at, std::string& why) override;
    bool actuate(const std::string& actuatorName, double value, std::string& why) override;
    bool moveActuator(const std::string& actuatorName, const JPLocation& at, bool withZ, double speed,
                      std::string& why) override;
    bool matchTemplate(const JPLocation& at, const std::string& templatePath, const JPTemplateFinder::Area& area,
                       JPLocation& offset, std::string& why) override;
    bool park(std::string& why) override;
    bool locateFiducial(const JPLocation& nominal, double diameterMm, const FiducialLook& look, JPLocation& found,
                        std::string& why) override;
    bool alignPart(const std::string& nozzleId, const AlignRequest& request, AlignResult& result, std::string& why) override;
    bool locateHole(const JPLocation& nominal, double diameterMm, double searchMm, double parallaxDiameterMm,
                    double parallaxAngle, JPLocation& found, std::string& why) override;

    // How many times the machine has been moved for the job (OpenPnP's motion
    // history, for Step Next Motion).
    int motions() const { return m_motions; }

private:
    // The cell's settings as they are now, and its head (the camera's).
    JPCellConfig config() const;
    std::string  headId(const JPCellConfig& c) const;
    JPCell*      cell(std::string& why) const;
    // A camera made ready to look: its picture in front, its light on as its settings say.
    void prepare(JPCell& cell, JPCameraFeed& feed);
    // The camera to (viewX, viewY), one settled look for a round mark of
    // `diameterMm` expected at (x, y) within `searchMm`: where it is.
    bool look(double viewX, double viewY, double x, double y, double diameterMm, double searchMm, double& foundX,
              double& foundY, std::string& why);

    JPlacerMachine&                          m_machine;
    OnMain                                   m_onMain;
    std::function<bool(const std::string&)>  m_ask;
    std::function<void(const std::string&)>  m_progress;
    std::atomic<int>                         m_motions { 0 };
};

} // inline namespace jf
