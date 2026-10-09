// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerJob.h"
#include "JPlacerJobRun.h"
#include "JPlacerMachine.h"
#include "JPlacerPipelines.h"

#include "pipeline/JPPipeline.h"
#include "ui/JPCameraView.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

inline namespace jf {

// OpenPnP's strip feeder Auto Setup: the head camera's view shows the
// sprocket holes the feeder's pipeline finds (the lines through them, the
// best yellow, its holes blue, the two nearest green) while it asks to click
// on the centre of the first part in the tape, then of the second. At each
// the camera goes there and finds the holes; from the two, the reference and
// last holes (the Z they had kept) and the part pitch (to the nearest 2 mm)
// are set, and the camera shows where the first part is picked.
class JPlacerStripAutoSetup {
public:
    JPlacerStripAutoSetup(JPlacerJob& job, JPlacerMachine& machine, JPlacerJobRun& run, JPlacerPipelines& pipelines);
    ~JPlacerStripAutoSetup();

    void start(const std::string& feederId);
    void cancel();
    bool running() const { return m_step != Step::Idle; }
    // Started, cancelled or done (the feeder's page shows Auto Setup or Cancel Auto Setup); the feeder changed.
    std::function<void()> onStateChanged;
    std::function<void()> onFeederChanged;

private:
    enum class Step { Idle, FirstPart, CheckingFirst, SecondPart, CheckingSecond };
    void setStep(Step s);
    void waitForClick(const std::string& prompt);
    void picked(double px, double py);
    void check(const JPLocation& at);
    void fail(const std::string& why);
    // The tape's height `where`, from its scale there (said in the log either way): the camera calibrated at two heights.
    std::optional<double> tapeHeight(double tapePxPerMm, const char* where) const;
    void startPreview();
    void stopPreview();

    JPlacerJob&                  m_job;
    JPlacerMachine&              m_machine;
    JPlacerJobRun&               m_run;
    JPlacerPipelines&            m_pipelines;
    Step                         m_step = Step::Idle;
    std::string                  m_feederId;
    JPCameraView*                m_view = nullptr;   // the head camera's, while it runs
    JPLocation                   m_firstPart { JPLengthUnit::Millimeters }, m_secondPart { JPLengthUnit::Millimeters };
    std::vector<JPLocation>      m_firstHoles;
    // The tape's scale by the first part and by the last; the pictures' calibrated one.
    double                       m_tapePxPerMm = 0, m_lastPxPerMm = 0, m_calibratedPxPerMm = 0;
    // The live picture of the holes: a worker looking while a click is awaited.
    std::shared_ptr<const JPPipeline> m_previewPipeline;
    std::thread                  m_preview;
    std::atomic<bool>            m_previewing { false };
    std::shared_ptr<bool>        m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
