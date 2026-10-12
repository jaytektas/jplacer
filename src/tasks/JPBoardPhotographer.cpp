// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardPhotographer.h"

#include "camera/JPImageFile.h"
#include "common/JPWhen.h"
#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>

inline namespace jf {

namespace {

constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;

std::string mm(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f mm", v);
    return b;
}

// Picture centres along a span: as few as cover it with `fov` each, overlapping by at least kOverlap, spread evenly.
std::vector<double> centres(double from, double to, double fov) {
    const double span = to - from;
    if (span <= fov) return { (from + to) / 2 };
    const double step = fov * (1 - JPBoardPhotographer::kOverlap);
    const int n = int(std::ceil((span - fov) / step)) + 1;
    const double spread = (span - fov) / (n - 1);
    std::vector<double> out;
    for (int i = 0; i < n; ++i) out.push_back(from + fov / 2 + i * spread);
    return out;
}

} // namespace

JPBoardPhotographer::JPBoardPhotographer(JPJobMachine& machine, OnMain onMain) : m_machine(machine), m_onMain(std::move(onMain)) {}

bool JPBoardPhotographer::begin(JPPlacementsHolderLocation& location, JPBoardPhoto::Kind kind, const std::string& job,
                                const std::string& run, std::string& why) {
    m_photo = JPBoardPhoto();
    m_photo.kind = kind;
    m_photo.job = job;
    m_photo.run = run;
    m_at.clear();
    m_tiles.clear();
    m_next = 0;
    m_unreachable = 0;
    bool known = true;
    auto main = [this](const std::function<void()>& fn) {
        if (m_onMain) m_onMain(fn);
        else fn();
    };
    main([&] {
        const JPPlacementsHolder* board = location.holder ? location.holder->definition() : nullptr;
        if (!board) {
            known = false;
            why = "no board there";
            return;
        }
        m_name = board->name.value_or(location.id);
        m_folder = JPBoardPhoto::folderOf(board->file);
        m_photo.boardLocation = location.uniqueId();
        m_photo.side = location.globalSide();
        m_boardToMachine = location.localToGlobalTransform();
        m_z = location.globalLocation().convertToUnits(kMm).z();
        // Its area: its outline, else its placements' spread.
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
        const JPLocation size = board->dimensions.convertToUnits(kMm);
        if (size.x() > 0 && size.y() > 0) {
            const JPProfile outline = board->outline();
            const double toMm = JPLength(1, outline.units).convertToUnits(kMm).value();
            for (size_t i = 0; i + 1 < outline.segmentPoints.size(); i += 2) {
                minX = std::min(minX, outline.segmentPoints[i] * toMm);
                maxX = std::max(maxX, outline.segmentPoints[i] * toMm);
                minY = std::min(minY, outline.segmentPoints[i + 1] * toMm);
                maxY = std::max(maxY, outline.segmentPoints[i + 1] * toMm);
            }
            minX -= kMarginMm;
            minY -= kMarginMm;
            maxX += kMarginMm;
            maxY += kMarginMm;
        } else {
            for (const JPPlacement& p : board->placements) {
                const JPLocation at = p.location.convertToUnits(kMm);
                minX = std::min(minX, at.x());
                maxX = std::max(maxX, at.x());
                minY = std::min(minY, at.y());
                maxY = std::max(maxY, at.y());
            }
            minX -= kSpreadMarginMm;
            minY -= kSpreadMarginMm;
            maxX += kSpreadMarginMm;
            maxY += kSpreadMarginMm;
        }
        m_area = { minX, minY, maxX, maxY };
    });
    if (!known) return false;
    if (m_folder.empty()) {
        why = m_name + " has no file yet: save it, its photos are kept beside it";
        return false;
    }
    if (!(m_area.maxX > m_area.minX && m_area.maxY > m_area.minY)) {
        why = m_name + " has no size and no placements: give it its width and length to photograph it";
        return false;
    }
    JPJobMachine::Sight sight;
    if (!m_machine.cameraSight(sight, why)) return false;
    const double fovX = sight.width * sight.mmPerPixelX, fovY = sight.height * sight.mmPerPixelY;
    if (fovX <= 0 || fovY <= 0) {
        why = "the head camera's calibration gives no scale";
        return false;
    }
    // At the camera's own scale, coarser where the photo would be too large.
    const double areaMm2 = (m_area.maxX - m_area.minX) * (m_area.maxY - m_area.minY);
    m_mmPerPixel = std::max((sight.mmPerPixelX + sight.mmPerPixelY) / 2, std::sqrt(areaMm2 / kMaxPixels));
    // The machine's rectangle round the board's area (the board may lie at an angle).
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    const std::array<std::pair<double, double>, 4> corners { { { m_area.minX, m_area.minY }, { m_area.maxX, m_area.minY },
                                                               { m_area.minX, m_area.maxY }, { m_area.maxX, m_area.maxY } } };
    for (const auto& [bx, by] : corners) {
        double x = 0, y = 0;
        m_boardToMachine.apply(bx, by, x, y);
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }
    // Row by row from the top, each row the other way from the last.
    std::vector<double> ys = centres(minY, maxY, fovY), xs = centres(minX, maxX, fovX);
    std::reverse(ys.begin(), ys.end());
    for (size_t row = 0; row < ys.size(); ++row) {
        std::vector<double> along = xs;
        if (row % 2) std::reverse(along.begin(), along.end());
        for (const double x : along) {
            const JPLocation at(kMm, x, ys[row], m_z, 0);
            if (m_machine.cameraReaches(at)) m_at.push_back(at);
            else ++m_unreachable;
        }
    }
    if (m_at.empty()) {
        why = "the head camera cannot reach " + m_name;
        return false;
    }
    m_photo.taken = JPWhen::now();
    return true;
}

bool JPBoardPhotographer::takeNext(std::string& why) {
    if (done()) return true;
    JPJobMachine::Snapshot shot;
    if (!m_machine.snapshot(m_at[m_next], m_z, shot, why)) return false;
    // Its pixels' places on the machine, from three of its corners.
    const double w = shot.bgr.cols, h = shot.bgr.rows;
    double x0 = 0, y0 = 0, xw = 0, yw = 0, xh = 0, yh = 0;
    if (w <= 0 || h <= 0 || !shot.toMachine || !shot.toMachine(0, 0, x0, y0) || !shot.toMachine(w, 0, xw, yw)
        || !shot.toMachine(0, h, xh, yh)) {
        why = "the head camera's calibration cannot place its picture on the machine";
        return false;
    }
    JPPhotoStitch::Tile t;
    t.bgr = shot.bgr;
    t.toMachine.m00 = (xw - x0) / w;
    t.toMachine.m10 = (yw - y0) / w;
    t.toMachine.m01 = (xh - x0) / h;
    t.toMachine.m11 = (yh - y0) / h;
    t.toMachine.m02 = x0;
    t.toMachine.m12 = y0;
    m_tiles.push_back(std::move(t));
    ++m_next;
    return true;
}

bool JPBoardPhotographer::finish(JPBoardPhoto& photo, std::string& why) {
    JPPhotoStitch::Result r;
    if (!JPPhotoStitch::stitch(m_tiles, m_boardToMachine, m_area, m_mmPerPixel, r, why)) return false;
    m_tiles.clear();
    photo = m_photo;
    photo.mmPerPixel = r.mmPerPixel;
    photo.originX = r.originX;
    photo.originY = r.originY;
    photo.width = r.bgra.cols;
    photo.height = r.bgra.rows;
    // What is said of it: how far its pictures were from where the machine put them, what could not be lined up or
    // reached.
    if (r.largestCorrectionMm > JPPhotoStitch::kCorrectionToSayMm)
        photo.notes.push_back("A picture was moved " + mm(r.largestCorrectionMm) + " to line up with its neighbours: the "
                              "camera was that far from where the machine said. Check the head camera's calibration "
                              "(units per pixel) and the axes' backlash compensation.");
    if (r.largestDisagreementMm > JPPhotoStitch::kDisagreementToSayMm)
        photo.notes.push_back("Neighbouring pictures still disagree by up to " + mm(r.largestDisagreementMm) +
                              " after lining up: the camera's lens is not fully straightened (calibrate the head camera "
                              "again), or the board moved while it was photographed.");
    int plainNote = -1;
    if (r.pairs > 0 && r.lined < r.pairs) {
        plainNote = int(photo.notes.size());
        photo.notes.push_back(std::to_string(r.pairs - r.lined) + " of " + std::to_string(r.pairs) +
                              " overlaps were too plain to line up by: those pictures are where the machine put them.");
    }
    if (m_unreachable > 0)
        photo.notes.push_back(std::to_string(m_unreachable) + " picture(s) out of the head camera's reach were not taken.");
    if (r.uncoveredShare > JPPhotoStitch::kUncoveredToSay)
        photo.notes.push_back(std::to_string(int(std::lround(r.uncoveredShare * 100))) + "% of the board's area is not in "
                              "the photo (shown clear).");
    std::error_code ec;
    std::filesystem::create_directories(m_folder, ec);
    if (ec) {
        why = m_folder + ": " + ec.message();
        return false;
    }
    photo.file = photo.fileIn(m_folder);
    cv::Mat rgba;
    cv::cvtColor(r.bgra, rgba, cv::COLOR_BGRA2RGBA);
    JPFrame frame;
    frame.width = rgba.cols;
    frame.height = rgba.rows;
    frame.rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
    if (!JPImageFile::writePng(photo.file, frame, why) || !photo.saveRecord(why)) return false;
    JLOGC(JPlacerLog::kJob, JLogLevel::Info) << m_name << " photographed (" << photo.width << " x " << photo.height
                                             << ", " << int(m_at.size()) << " pictures, " << r.lined << " of " << r.pairs
                                             << " overlaps lined up): " << photo.file;
    // Each said; overlaps too plain to line up by (the bare spaces between boards) are no fault.
    for (size_t i = 0; i < photo.notes.size(); ++i)
        JLOGC(JPlacerLog::kJob, int(i) == plainNote ? JLogLevel::Info : JLogLevel::Warn) << m_name << "'s photo: " << photo.notes[i];
    return true;
}

bool JPBoardPhotographer::photograph(JPJobMachine& machine, const OnMain& onMain, JPPlacementsHolderLocation& location,
                                     JPBoardPhoto::Kind kind, const std::string& job, const std::string& run,
                                     const std::function<bool(int, int)>& goOn, JPBoardPhoto& photo, std::string& why) {
    JPBoardPhotographer p(machine, onMain);
    if (!p.begin(location, kind, job, run, why)) return false;
    while (!p.done()) {
        if (goOn && !goOn(p.taken(), p.count())) {
            why = "Stopped";
            return false;
        }
        if (!p.takeNext(why)) return false;
    }
    if (goOn) goOn(p.taken(), p.count());
    return p.finish(photo, why);
}

} // inline namespace jf
