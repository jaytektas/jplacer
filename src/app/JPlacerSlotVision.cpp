// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerSlotVision.h"

#include "JPlacerJobMachine.h"

#include "common/JPlacerLog.h"
#include "machine/JPCell.h"
#include "pipeline/JPStageUtil.h"
#include "tasks/JPChangerSlotVision.h"

#include <j/core/Log.h>


#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>

inline namespace jf {

namespace {

std::string nameOf(const JPNozzleTipConfig& tip) { return tip.name.empty() ? tip.id : tip.name; }

std::string format(const char* f, double a, double b) {
    char buf[160];
    std::snprintf(buf, sizeof buf, f, a, b);
    return buf;
}

} // namespace

JPlacerSlotVision::JPlacerSlotVision(JPlacerJobMachine& machine, JPCell& cell, std::string cellPath)
    : m_machine(machine), m_cell(cell), m_cellPath(std::move(cellPath)) {}

std::string JPlacerSlotVision::templatePath(const std::string& cellPath, const std::string& fileName) {
    // A whole path (OpenPnP's own file, imported) as it is.
    if (std::filesystem::path(fileName).is_absolute()) return fileName;
    return (std::filesystem::path(cellPath).parent_path() / kFolder / fileName).string();
}

bool JPlacerSlotVision::load(const std::string& fileName, const char* what, const std::string& tipName, cv::Mat& picture,
                             std::string& why) const {
    if (fileName.empty()) {
        why = "Nozzle tip " + tipName + " changer slot vision calibration: " + what + " missing.";
        return false;
    }
    try {
        picture = JPStageUtil::readPicture(templatePath(m_cellPath, fileName));
    } catch (const std::exception& e) {
        why = "Nozzle tip " + tipName + " changer slot vision calibration: " + what + " could not be read: " + e.what();
        return false;
    }
    return true;
}

bool JPlacerSlotVision::calibrate(const JPNozzleTipConfig& tip, bool tipChange, bool occupied, std::array<double, 2>& offset,
                                  std::optional<double>& score, std::string& why) {
    const JPNozzleTipConfig::VisionCalibration& v = tip.visionCalibration;
    offset = { 0, 0 };
    const auto nominal = tip.visionCalibrationPlace();
    if (!nominal) {
        m_cell.setSlotOffset(tip.id, std::nullopt);
        return true;
    }
    if (const auto kept = m_cell.slotOffset(tip.id); kept && !(v.trigger == "NozzleTipChange" && tipChange)) {
        offset = *kept;
        return true;
    }
    const std::string name = nameOf(tip);
    cv::Mat empty, full;
    if (!load(v.templateEmpty, "Template Empty", name, empty, why) || !load(v.templateOccupied, "Template Occupied", name, full, why))
        return false;
    double x = nominal->x, y = nominal->y;
    for (int pass = 0; pass < std::max(1, v.maxPasses); ++pass) {
        cv::Mat bgr;
        JPCameraCalibration cal;
        if (!m_machine.lookAt(x, y, nominal->z, bgr, cal, why)) return false;
        const int w = JPChangerSlotVision::evenPixels(v.templateWidthMm + v.toleranceMm, 1 / cal.scaleX());
        const int h = JPChangerSlotVision::evenPixels(v.templateHeightMm + v.toleranceMm, 1 / cal.scaleY());
        JPChangerSlotVision::Match e, o;
        if (!JPChangerSlotVision::find(bgr, empty, w, h, e, why) || !JPChangerSlotVision::find(bgr, full, w, h, o, why)) return false;
        JLOGC(JPlacerLog::kCell, JLogLevel::Debug) << "Nozzle tip " << name << " changer slot empty template at pixel offset "
                                                     << e.dxPx << ", " << e.dyPx << ", score " << e.score << "; occupied at "
                                                     << o.dxPx << ", " << o.dyPx << ", score " << o.score;
        if (std::max(e.score, o.score) < v.minimumScore) {
            why = "Nozzle tip " + name + format(" changer slot vision calibration failed. Score %f lower than minmum %f.",
                                                std::max(e.score, o.score), v.minimumScore);
            return false;
        }
        const bool foundOccupied = e.score < o.score;
        if (foundOccupied != occupied) {
            why = "Nozzle tip " + name + " changer slot was expected " + (occupied ? "occupied" : "empty") + ", but found "
                + (foundOccupied ? "occupied" : "empty") + ".";
            return false;
        }
        const JPChangerSlotVision::Match& m = occupied ? o : e;
        score = m.score;
        double fx = 0, fy = 0;
        if (!cal.machinePoint(bgr.cols / 2.0 + m.dxPx, bgr.rows / 2.0 + m.dyPx, x, y, fx, fy)) {
            why = "the head camera's calibration cannot place what it sees";
            return false;
        }
        const double moved = std::hypot(fx - x, fy - y);
        x = fx;
        y = fy;
        // Good enough, done.
        if (moved < v.precisionMm) break;
        // Runaway?
        if (std::hypot(x - nominal->x, y - nominal->y) > v.toleranceMm) {
            why = "Nozzle tip " + name + " slot was found too far away.";
            return false;
        }
    }
    offset = { x - nominal->x, y - nominal->y };
    m_cell.setSlotOffset(tip.id, offset);
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << "Nozzle tip " << name << " changer slot calibration offset " << offset[0] << ", "
                                                << offset[1] << ", distance " << std::hypot(offset[0], offset[1]);
    return true;
}

bool JPlacerSlotVision::captureTemplate(const JPNozzleTipConfig& tip, std::string& fileName, std::string& why) {
    const auto at = tip.visionCalibrationPlace();
    if (!at) {
        why = "Select a vision calibration location first.";
        return false;
    }
    cv::Mat bgr;
    JPCameraCalibration cal;
    if (!m_machine.lookAt(at->x, at->y, at->z, bgr, cal, why)) return false;
    const JPNozzleTipConfig::VisionCalibration& v = tip.visionCalibration;
    const int w = JPChangerSlotVision::evenPixels(v.templateWidthMm, 1 / cal.scaleX());
    const int h = JPChangerSlotVision::evenPixels(v.templateHeightMm, 1 / cal.scaleY());
    const cv::Rect middle = JPChangerSlotVision::middle(bgr, w, h);
    if (middle.width < 2 || middle.height < 2) {
        why = "the template is too small to take";
        return false;
    }
    // Named by what it holds, as OpenPnP's TemplateImage is (the same picture, one file).
    const cv::Mat taken = bgr(middle).clone();
    std::string bytes(reinterpret_cast<const char*>(taken.data), taken.total() * taken.elemSize());
    bytes += std::to_string(taken.cols) + "x" + std::to_string(taken.rows);
    char hash[17];
    std::snprintf(hash, sizeof hash, "%016zx", std::hash<std::string>()(bytes));
    fileName = std::string(hash) + ".png";
    const std::string path = templatePath(m_cellPath, fileName);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    try {
        JPStageUtil::writePicture(path, taken);
    } catch (const std::exception& e) {
        why = e.what();
        return false;
    }
    return true;
}

} // inline namespace jf
