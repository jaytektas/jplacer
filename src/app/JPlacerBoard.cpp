// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerBoard.h"

#include "JPlacerSettings.h"

#include "common/JPlacerLog.h"

#include <j/config/Settings.h>
#include <j/core/Dialog.h>
#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

inline namespace jf {

namespace {

constexpr int kStatusMs = 8000;

std::string mm(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    return buf;
}

} // namespace

JPlacerBoard::JPlacerBoard(JAppWindow& window, JPlacerJob& job, JPlacerCameraTasks& tasks, Square square)
    : m_window(window), m_job(job), m_tasks(tasks), m_square(std::move(square)) {
    const JSettings& s = JSettings::instance();
    m_place.side = s.get<std::string>(JPlacerSettings::kBoardSide, "top") == "bottom" ? JPPlacement::Side::Bottom
                                                                                     : JPPlacement::Side::Top;
    std::istringstream place(s.get<std::string>(JPlacerSettings::kBoardPlace, ""));
    JPAffine2D& m = m_place.toMachine;
    m_placed = !board().placements.empty() && bool(place >> m.a >> m.b >> m.c >> m.d >> m.tx >> m.ty);
    m_measured = m_placed && s.get<bool>(JPlacerSettings::kBoardMeasured, false);
    m_watch = m_job.watch([this](JPlacerJob::Change what) {
        if (what == JPlacerJob::Change::Board) newBoard();
        else show();
    });
}

JPlacerBoard::~JPlacerBoard() {
    m_job.unwatch(m_watch);
}

std::unique_ptr<JPBoardPanel> JPlacerBoard::makePanel(JSceneGraph& graph) {
    auto panel = std::make_unique<JPBoardPanel>(graph);
    m_panel = panel.get();
    m_panel->onImport           = [this] { m_job.importCpl(); };
    m_panel->onSide             = [this](bool bottom) { setSide(bottom); };
    m_panel->onCameraOnFiducial = [this](const std::string& d) { cameraOn(d); };
    m_panel->onLocate           = [this] { locate(); };
    m_panel->onGoTo             = [this](const std::string& d) { goTo(d); };
    m_panel->onSquare           = [this] { square(); };
    show();
    return panel;
}

void JPlacerBoard::newBoard() {
    // A new board is nowhere yet; the side with the fiducials is a fair first guess.
    m_placed = m_measured = false;
    m_found.clear();
    m_leans.clear();
    m_place = JPBoardSide();
    m_place.side = board().fiducials(JPPlacement::Side::Top).empty() && !board().fiducials(JPPlacement::Side::Bottom).empty()
                       ? JPPlacement::Side::Bottom
                       : JPPlacement::Side::Top;
    save();
    show();
}

void JPlacerBoard::setSide(bool bottom) {
    const JPPlacement::Side side = bottom ? JPPlacement::Side::Bottom : JPPlacement::Side::Top;
    if (side == m_place.side) return;
    // The other side up is somewhere else: start again from a fiducial.
    m_place = JPBoardSide();
    m_place.side = side;
    m_placed = m_measured = false;
    m_found.clear();
    m_leans.clear();
    save();
    show();
}

void JPlacerBoard::cameraOn(const std::string& designator) {
    const JPPlacement* fid = board().find(designator);
    double x, y;
    std::string why;
    if (!fid || !m_tasks.headCameraView(x, y, why)) {
        m_window.showStatus(why.empty() ? "No fiducial " + designator : why, kStatusMs);
        return;
    }
    // The board unturned, moved so this fiducial is where the camera looks.
    const double turn = m_placed ? m_place.toMachine.rotationDeg() : 0;
    JPBoardSide b = JPBoardSide::placed(m_place.side, 0, 0, turn);
    double fx, fy;
    b.toMachine.apply(fid->x, fid->y, fx, fy);
    b.toMachine.tx = x - fx;
    b.toMachine.ty = y - fy;
    m_place = b;
    m_placed = true;
    m_measured = false;
    m_found.clear();
    save();
    show();
}

void JPlacerBoard::locate() {
    if (!m_placed) {
        m_window.showStatus("Put the camera on one of the board's fiducials and press Camera Is on It first", kStatusMs);
        return;
    }
    if (m_panel) m_panel->setBusy(true);
    std::weak_ptr<bool> alive = m_alive;
    m_tasks.locateBoard(board(), m_place, [this, alive](const JPBoardLocator::Result& r) {
        if (const auto a = alive.lock(); !a || !*a) return;
        if (m_panel) m_panel->setBusy(false);
        m_found.clear();
        for (const auto& f : r.fiducials)
            m_found.push_back(f.designator + (f.found ? "  found, " + mm(f.residualMm) + " mm from the fit" : "  not found: " + f.why));
        if (r.ok) {
            m_place = r.board;
            m_measured = true;
            if (r.affine) m_leans.push_back(r.xPerY);
        }
        save();
        show();
    });
    if (!m_tasks.busy() && m_panel) m_panel->setBusy(false);   // it did not start
}

void JPlacerBoard::goTo(const std::string& designator) {
    const JPPlacement* p = board().find(designator);
    if (!p || !m_placed) return;
    double x, y;
    m_place.toMachine.apply(p->x, p->y, x, y);
    if (m_tasks.lookAt(x, y))
        m_window.showStatus(designator + " (" + p->footprint + (p->value.empty() ? "" : ", " + p->value) + ") at "
                            + mm(x) + ", " + mm(y), kStatusMs);
}

void JPlacerBoard::show() {
    if (!m_panel) return;
    const bool bottom = m_place.side == JPPlacement::Side::Bottom;
    std::vector<std::string> fiducials, rows, designators;
    for (const JPPlacement* f : board().fiducials(m_place.side)) fiducials.push_back(f->designator);
    for (const JPPlacement& p : board().placements) {
        if (p.side != m_place.side || p.fiducial) continue;
        rows.push_back(p.designator + "   " + p.footprint + (p.value.empty() ? "" : "   " + p.value));
        designators.push_back(p.designator);
    }
    std::string summary;
    if (!board().placements.empty())
        summary = board().name + ": " + std::to_string(rows.size()) + " parts and " + std::to_string(fiducials.size())
                + " fiducials on this side";
    m_panel->showBoard(summary, bottom, fiducials, rows, designators);
    if (!m_placed)
        m_panel->showPlace(board().placements.empty() ? "" : "Put the camera on a fiducial, choose it, press Camera Is on It.");
    else
        m_panel->showPlace(std::string(m_measured ? "Found by its fiducials" : "Starting point") + ": origin at "
                           + mm(m_place.toMachine.tx) + ", " + mm(m_place.toMachine.ty) + ", turned "
                           + mm(m_place.toMachine.rotationDeg()) + " deg");
    m_panel->showFound(m_found);
    std::string lean;
    if (!m_leans.empty()) {
        const double mean = meanLean();
        lean = "The machine's axes lean " + mm(mean * 100) + " mm in X per 100 mm of Y ("
             + mm(std::atan(mean) * 57.29578) + " deg out of square)";
        if (m_leans.size() > 1) {
            const auto [lo, hi] = std::minmax_element(m_leans.begin(), m_leans.end());
            lean += ": the mean of " + std::to_string(m_leans.size()) + " findings, from " + mm(*lo * 100) + " to "
                  + mm(*hi * 100);
        } else {
            lean += "; locating it again measures it better";
        }
    }
    m_panel->showLean(lean);
}

std::vector<JPViewMark> JPlacerBoard::marks(const std::string& cameraId) const {
    std::vector<JPViewMark> out;
    JPCameraCalibration cal;
    double vx, vy;
    if (!m_placed || !m_tasks.cameraLook(cameraId, cal, vx, vy)) return out;
    const double scale = std::sqrt(cal.scaleX() * cal.scaleY());
    const double defaultMm = JPBoardLocator::Options().fiducialDiameterMm;   // as the locator looks for them
    for (const JPPlacement& p : board().placements) {
        if (p.side != m_place.side) continue;
        double x, y, px, py;
        m_place.toMachine.apply(p.x, p.y, x, y);
        if (!cal.pixelFor(x, y, vx, vy, px, py) || px < 0 || py < 0 || px >= cal.width || py >= cal.height) continue;
        out.push_back({ px, py, p.fiducial ? (p.fiducialMm > 0 ? p.fiducialMm : defaultMm) / 2 * scale : 0, p.designator, p.fiducial });
    }
    return out;
}

double JPlacerBoard::meanLean() const {
    double sum = 0;
    for (double l : m_leans) sum += l;
    return m_leans.empty() ? 0 : sum / double(m_leans.size());
}

void JPlacerBoard::square() {
    const JPCameraPanel* camera = m_tasks.headCamera();
    const JPMountConfig* mount = camera ? &camera->camera().mount : nullptr;
    if (m_leans.empty() || !mount || mount->axisX.empty() || mount->axisY.empty()) return;
    const double lean = meanLean();
    const JPMountConfig gantry = *mount;
    std::weak_ptr<bool> alive = m_alive;
    JDialog::confirm("Square the Machine",
                     "The board's fiducials show the machine's Y axis leaning " + mm(lean * 100) + " mm in X per 100 mm"
                         + (m_leans.size() > 1 ? " (the mean of " + std::to_string(m_leans.size()) + " findings)" : "")
                         + ". Correct for it from now on?\n\nEvery coordinate changes a little: "
                           "home the machine again, then calibrate the camera and locate the board again.",
                     [this, alive, lean, gantry] {
                         if (const auto a = alive.lock(); !a || !*a) return;
                         m_square(gantry, lean);
                         // Measured in the old coordinates: the board is to be found again.
                         m_leans.clear();
                         m_measured = false;
                         m_found.clear();
                         save();
                         show();
                     });
}

void JPlacerBoard::save() const {
    JSettings& s = JSettings::instance();
    s.set(JPlacerSettings::kBoardSide, std::string(m_place.side == JPPlacement::Side::Bottom ? "bottom" : "top"));
    std::string place;
    if (m_placed) {
        const JPAffine2D& m = m_place.toMachine;
        char buf[200];
        std::snprintf(buf, sizeof buf, "%.9g %.9g %.9g %.9g %.9g %.9g", m.a, m.b, m.c, m.d, m.tx, m.ty);
        place = buf;
    }
    s.set(JPlacerSettings::kBoardPlace, place);
    s.set(JPlacerSettings::kBoardMeasured, m_measured);
    JPlacerSettings::save();
}

} // inline namespace jf
