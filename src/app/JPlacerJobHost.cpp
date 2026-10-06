// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerJobHost.h"

#include "JPlacerMachine.h"

#include "tasks/JPRunoutCalibrator.h"
#include "ui/JPCameraView.h"

inline namespace jf {

JPCell* JPlacerJobHost::cell() const { return m_machine.cell(); }
std::optional<JPLocation> JPlacerJobHost::cameraLocation() const { return m_machine.toolLocation(JPSetupForm::Tool::Camera); }
JPCameraFeed* JPlacerJobHost::headCameraFeed() const { return m_machine.headCameraFeed(); }
JPCameraFeed* JPlacerJobHost::upCameraFeed() const { return m_machine.upCameraFeed(); }
JPCameraFeed* JPlacerJobHost::cameraFeed(const std::string& idOrName) const { return m_machine.cameraFeed(idOrName); }

void JPlacerJobHost::showPicture(const JPCameraFeed* feed, const JPFrame& frame, const std::string& text, int ms) {
    if (JPCameraView* view = m_machine.cameraViewOf(feed)) view->showPicture(frame, text, ms);
}

void JPlacerJobHost::showCamera(const std::string& cameraId) { m_machine.showCamera(cameraId); }
std::string JPlacerJobHost::nozzlePart(const std::string& nozzleId) const { return m_machine.nozzlePart(nozzleId); }
void JPlacerJobHost::setNozzlePart(const std::string& nozzleId, const std::string& partId) { m_machine.setNozzlePart(nozzleId, partId); }
std::string JPlacerJobHost::chosenNozzleId() const { return m_machine.chosenNozzleId(); }

std::string JPlacerJobHost::tipChangeRefusal(const std::string& nozzleId, const std::string& tipId) const {
    return m_machine.tipChangeRefusal(nozzleId, tipId);
}

void JPlacerJobHost::setTipOn(const std::string& nozzleId, const std::string& tipId) { m_machine.setTipOn(nozzleId, tipId); }
std::string JPlacerJobHost::cellPath() const { return m_machine.cellPath(); }
void JPlacerJobHost::slotScored(const std::string& tipId, double score) { m_machine.slotScored(tipId, score); }

std::optional<JPRunout> JPlacerJobHost::measureRunout(JPCell& cell, JPCameraFeed& feed, const JPNozzleConfig& nozzle,
                                                      const JPNozzleTipConfig& tip, std::string& words,
                                                      std::optional<JPBackgroundCalibration::Result>& background) {
    return JPRunoutCalibrator::measure(cell, feed, nozzle, tip, &m_machine.scripting(), words, nullptr, background);
}

void JPlacerJobHost::keepRunout(const std::string& tipId, const std::string& nozzleId, const std::optional<JPRunout>& runout) {
    m_machine.keepRunout(tipId, nozzleId, runout);
}

void JPlacerJobHost::keepBackground(const std::string& tipId, const JPBackgroundCalibration::Result& background) {
    m_machine.keepBackground(tipId, background);
}

} // inline namespace jf
