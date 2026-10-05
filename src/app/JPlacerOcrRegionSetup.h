// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerJob.h"
#include "JPlacerJobRun.h"
#include "JPlacerMachine.h"

#include "model/JPLocation.h"
#include "ui/JPCameraView.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

inline namespace jf {

// OpenPnP's Setup OCR Region (its RegionOfInterestProcess) for a push-pull
// feeder: the head camera over the feeder's vision location, then step by
// step on the camera's view: the camera may be moved (jogged, or a place in
// its view looked at), then a click marks the region's upper left, upper
// right and lower left corner (a click again takes it back), then each click
// switches between a rectangle and a parallelogram; Finish keeps the region
// (its corners about the camera's centre, and how far the camera was moved,
// when more than 0.1 mm) as the feeder's OCR region. The step is said over
// the picture; the feeder's page shows Next (Finish) and Cancel meanwhile.
class JPlacerOcrRegionSetup {
public:
    JPlacerOcrRegionSetup(JPlacerJob& job, JPlacerMachine& machine, JPlacerJobRun& run);
    ~JPlacerOcrRegionSetup();

    void start(const std::string& feederId);
    void next();
    void cancel();
    bool running() const { return m_step != Step::Idle; }
    // What the page's button that goes on says: "Next", or "Finish" at the last step.
    std::string proceedLabel() const;
    std::function<void()> onStateChanged;
    std::function<void()> onFeederChanged;

private:
    enum class Step { Idle, CameraOffset, UpperLeft, UpperRight, LowerLeft, SelectMode, Complete };
    void begin();
    void setStep(Step s);
    void picked(double px, double py);
    void save();

    JPlacerJob&                 m_job;
    JPlacerMachine&             m_machine;
    JPlacerJobRun&              m_run;
    Step                        m_step = Step::Idle;
    std::string                 m_feederId;
    JPCameraView*               m_view = nullptr;   // the head camera's, while it runs
    std::optional<JPLocation>   m_startCamera;
    // The corners marked, about the camera's centre (mm), by step; clicks at this step.
    std::map<Step, JPLocation>  m_corners;
    bool                        m_rectify = false;
    int                         m_clicks = 0;
    std::shared_ptr<bool>       m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
