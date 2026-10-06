// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPNozzleTipConfig.h"

#include <opencv2/core.hpp>

#include <array>
#include <optional>
#include <string>

inline namespace jf {

class JPCell;
class JPCellJobMachine;

// OpenPnP's nozzle tip changer slot vision calibration
// (JPNozzleTipConfig::VisionCalibration), on the head camera: a tip's
// template pictures taken, and its slot found by them, how far off kept by
// the cell (JPCell::slotOffset). Called on a thread of the caller's own.
class JPTipSlotVision {
public:
    // `cellPath`: the cell's file; the template pictures are beside it.
    JPTipSlotVision(JPCellJobMachine& machine, JPCell& cell, std::string cellPath);

    // OpenPnP's ensureVisionCalibration: the offset kept, or (none kept, or
    // `tipChange` with the NozzleTipChange trigger) the slot found, expected
    // `occupied` or empty, pass after pass from the place Vision Calibration
    // names. With Vision Calibration None: no offset (any kept forgotten).
    // `score`: the last match's (none without a look).
    bool calibrate(const JPNozzleTipConfig& tip, bool tipChange, bool occupied, std::array<double, 2>& offset,
                   std::optional<double>& score, std::string& why);
    // OpenPnP's Capture: the camera over the place Vision Calibration names,
    // the middle of its picture Template Width x Height written beside the
    // cell; `fileName` its name.
    bool captureTemplate(const JPNozzleTipConfig& tip, std::string& fileName, std::string& why);

    // Where a template picture named `fileName` is, for the cell at `cellPath`
    // (a whole path, as an OpenPnP machine's are imported, as it is).
    static std::string templatePath(const std::string& cellPath, const std::string& fileName);
    // The folder the template pictures are in (OpenPnP's, beside its configuration).
    static constexpr const char* kFolder = "org.openpnp.vision.TemplateImage";

private:
    bool load(const std::string& fileName, const char* what, const std::string& tipName, cv::Mat& picture, std::string& why) const;

    JPCellJobMachine& m_machine;
    JPCell&            m_cell;
    std::string        m_cellPath;
};

} // inline namespace jf
