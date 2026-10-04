// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerBoard.h"

#include "JPlacerSettings.h"

#include "common/JPlacerLog.h"
#include "library/JPPlacementRotation.h"

#include <j/config/Settings.h>
#include <j/core/Dialog.h>
#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>

inline namespace jf {

namespace {

constexpr int kStatusMs = 8000;
// A reference's learned look reaches this far round it where its footprint
// does not say (mm).
constexpr double kLookReachMm = 1.5;

std::string mm(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    return buf;
}

} // namespace

JPlacerBoard::JPlacerBoard(JAppWindow& window, JPlacerJob& job, JPlacerCameraTasks& tasks, const JPCellConfig& cell,
                           Square square)
    : m_window(window), m_job(job), m_tasks(tasks), m_cell(cell), m_square(std::move(square)) {
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
    *m_alive = false;
    m_job.unwatch(m_watch);
}

std::unique_ptr<JPBoardPanel> JPlacerBoard::makePanel(JSceneGraph& graph) {
    auto panel = std::make_unique<JPBoardPanel>(graph);
    m_panel = panel.get();
    m_panel->onImport            = [this] { m_job.importCpl(); };
    m_panel->onSide              = [this](bool bottom) { setSide(bottom); };
    m_panel->onCapture           = [this](bool byHand) {
        m_job.job().location.capture = byHand ? JPLocateSettings::Capture::Manual : JPLocateSettings::Capture::Automatic;
        m_job.changed(JPlacerJob::Change::Parts);
    };
    m_panel->onNewBoard          = [this](bool reuse) {
        m_job.job().location.newBoard = reuse ? JPLocateSettings::NewBoard::Reuse : JPLocateSettings::NewBoard::ReRecord;
        m_job.changed(JPlacerJob::Change::Parts);
    };
    m_panel->onNewBoardOnBed     = [this] { newBoardOnBed(); };
    m_panel->onGoTo              = [this](const std::string& d) { goTo(d); };
    m_panel->onRecord            = [this](const std::string& d) { record(d); };
    m_panel->onCameraOnReference = [this](const std::string& d) { cameraOn(d); };
    m_panel->onLocate            = [this] { locate(); };
    m_panel->onSquare            = [this] { square(); };
    show();
    return panel;
}

void JPlacerBoard::newBoard() {
    // A new board is nowhere yet; the side with references is a fair first guess.
    m_placed = m_measured = false;
    m_stale.clear();
    m_found.clear();
    m_leans.clear();
    m_place = JPBoardSide();
    m_place.side = board().references(JPPlacement::Side::Top).empty() && !board().references(JPPlacement::Side::Bottom).empty()
                       ? JPPlacement::Side::Bottom
                       : JPPlacement::Side::Top;
    save();
    show();
}

void JPlacerBoard::setSide(bool bottom) {
    const JPPlacement::Side side = bottom ? JPPlacement::Side::Bottom : JPPlacement::Side::Top;
    if (side == m_place.side) return;
    // The other side up is somewhere else: start again.
    m_place = JPBoardSide();
    m_place.side = side;
    m_placed = m_measured = false;
    m_stale.clear();
    m_found.clear();
    m_leans.clear();
    save();
    show();
}

void JPlacerBoard::machineChanged(const std::string& why) {
    if (!m_placed) return;
    m_measured = false;
    m_stale = why;
    save();
    show();
}

void JPlacerBoard::cameraOn(const std::string& designator) {
    const JPPlacement* ref = board().find(designator);
    double x, y;
    std::string why;
    if (!ref || !m_tasks.headCameraView(x, y, why)) {
        m_window.showStatus(why.empty() ? "No placement " + designator : why, kStatusMs);
        return;
    }
    // The board unturned (or as last found), moved so this reference is where the camera looks.
    const double turn = m_placed ? m_place.toMachine.rotationDeg() : 0;
    JPBoardSide b = JPBoardSide::placed(m_place.side, 0, 0, turn);
    double fx, fy;
    b.toMachine.apply(ref->x, ref->y, fx, fy);
    b.toMachine.tx = x - fx;
    b.toMachine.ty = y - fy;
    m_place = b;
    m_placed = true;
    m_measured = false;
    m_stale.clear();
    m_found.clear();
    save();
    show();
}

void JPlacerBoard::record(const std::string& designator) {
    JPPlacement* p = m_job.job().board.find(designator);
    if (!p) return;
    JPGrayImage picture;
    JPCameraCalibration cal;
    double x, y;
    std::string why;
    if (!m_tasks.headPicture(picture, cal, x, y, why)) {
        m_window.showStatus("Record: " + why, kStatusMs);
        return;
    }
    p->recorded = true;
    p->recordedX = x;
    p->recordedY = y;
    // Its learned look: the picture round where the camera looks, as far as
    // its footprint reaches (or a little way where none is known).
    double reach = kLookReachMm;
    const JPlacerCameraTasks::Footprints fs = footprints();
    if (const auto it = fs.find(designator); it != fs.end())
        for (const JPPad& pad : it->second.first.pads)
            reach = std::max(reach, std::hypot(std::abs(pad.x) + pad.width / 2, std::abs(pad.y) + pad.height / 2) + 0.3);
    const double scale = std::sqrt(cal.scaleX() * cal.scaleY());
    double cx, cy;
    if (!cal.pixelFor(x, y, x, y, cx, cy)) {
        cx = picture.width / 2.0;
        cy = picture.height / 2.0;
    }
    const int half = int(std::ceil(reach * scale));
    p->look.clear();
    p->lookWidth = 0;
    if (cx - half >= 0 && cy - half >= 0 && cx + half < picture.width && cy + half < picture.height) {
        for (int j = -half; j <= half; ++j)
            for (int i = -half; i <= half; ++i)
                p->look.push_back(static_cast<unsigned char>(std::clamp(picture.at(int(cx) + i, int(cy) + j), 0.f, 255.f)));
        p->lookWidth = 2 * half + 1;
        p->lookPxPerMm = scale;
    }
    m_window.showStatus(designator + " recorded at " + mm(x) + ", " + mm(y), kStatusMs);
    m_job.changed(JPlacerJob::Change::Parts);
    // On to the next reference not recorded yet, chosen, the camera taken to
    // where the board as known puts it.
    for (const JPPlacement* next : board().references(m_place.side))
        if (!next->recorded) {
            if (m_panel) m_panel->chooseReference(next->designator);
            if (m_placed) goTo(next->designator);
            break;
        }
}

void JPlacerBoard::newBoardOnBed() {
    // A new board sits near where the last one did: its place is the guess, not a measurement.
    m_measured = false;
    m_stale.clear();
    m_found.clear();
    const bool reRecord = m_job.job().location.capture == JPLocateSettings::Capture::Manual
                       && m_job.job().location.newBoard == JPLocateSettings::NewBoard::ReRecord;
    if (reRecord) {
        for (JPPlacement& p : m_job.job().board.placements)
            if (p.reference && p.side == m_place.side) p.recorded = false;
        m_job.changed(JPlacerJob::Change::Parts);
        m_window.showStatus("Record each reference again: Go To one, jog the camera onto it, Record", kStatusMs);
    }
    save();
    show();
}

JPlacerCameraTasks::Footprints JPlacerBoard::footprints() const {
    JPlacerCameraTasks::Footprints out;
    const JPPartsStore& s = m_job.job().parts;
    for (const JPPlacement& p : board().placements) {
        if (!p.reference || p.fiducial) continue;
        const JPPart* part = s.part(p.partId);
        const JPPackage* k = part ? s.package(part->packageId) : nullptr;
        const JPFootprint* f = k ? s.footprint(k->footprintId) : nullptr;
        if (f && !f->pads.empty()) out[p.designator] = { *f, JPPlacementRotation::of(p, s).degrees };
    }
    return out;
}

bool JPlacerBoard::anchorGuess(JPBoardSide& out, std::string& why) const {
    const JPBoardStartConfig& c = m_cell.boardStart;
    const JPBoardStartConfig::Side& side = m_place.side == JPPlacement::Side::Bottom ? c.bottom : c.top;
    // The board's corners: its outline, or with none its placements' extent.
    const JPBoardFrame& frame = m_job.job().frame;
    double x0 = 0, y0 = 0, x1 = frame.width, y1 = frame.height;
    if (!frame.hasOutline()) {
        if (board().placements.empty()) {
            why = "the board has no placements";
            return false;
        }
        x0 = y0 = std::numeric_limits<double>::max();
        x1 = y1 = -x0;
        for (const JPPlacement& p : board().placements) {
            x0 = std::min(x0, p.x);
            y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x);
            y1 = std::max(y1, p.y);
        }
    }
    double cx = x0, cy = y0;
    switch (side.corner) {
        case JPBoardStartConfig::Corner::BottomLeft:  cx = x0; cy = y0; break;
        case JPBoardStartConfig::Corner::BottomRight: cx = x1; cy = y0; break;
        case JPBoardStartConfig::Corner::TopLeft:     cx = x0; cy = y1; break;
        case JPBoardStartConfig::Corner::TopRight:    cx = x1; cy = y1; break;
    }
    out = JPBoardSide::placed(m_place.side, 0, 0, side.turnDeg);
    double fx, fy;
    out.toMachine.apply(cx, cy, fx, fy);
    out.toMachine.tx = c.anchorX - fx;
    out.toMachine.ty = c.anchorY - fy;
    return true;
}

void JPlacerBoard::locate() {
    const JPJob& job = m_job.job();
    if (job.location.capture == JPLocateSettings::Capture::Manual) {
        found(JPBoardLocator::fitRecorded(board(), m_place, JPBoardLocator::Options()));
        return;
    }
    if (m_placed) {
        locateFrom(m_place);
        return;
    }
    const JPBoardStartConfig& c = m_cell.boardStart;
    if (c.kind == JPBoardStartConfig::Kind::Anchor) {
        JPBoardSide guess;
        std::string why;
        if (!anchorGuess(guess, why)) {
            m_window.showStatus("Locate Board: " + why, kStatusMs);
            return;
        }
        locateFrom(guess);
        return;
    }
    if (c.kind == JPBoardStartConfig::Kind::Region) {
        double x0 = c.regionX0, y0 = c.regionY0, x1 = c.regionX1, y1 = c.regionY1;
        if (!c.hasRegion()) {
            // The head camera's whole travel within its axes' soft limits.
            const JPCameraPanel* camera = m_tasks.headCamera();
            const JPMountConfig* mount = camera ? &camera->camera().mount : nullptr;
            const JPAxisConfig* ax = mount ? m_cell.axis(mount->axisX) : nullptr;
            const JPAxisConfig* ay = mount ? m_cell.axis(mount->axisY) : nullptr;
            if (!ax || !ay || !ax->softLimitLowEnabled || !ax->softLimitHighEnabled || !ay->softLimitLowEnabled
                || !ay->softLimitHighEnabled) {
                m_window.showStatus("Locate Board: set a search region in Machine Setup (the axes have no soft limits to search within)",
                                    kStatusMs);
                return;
            }
            x0 = ax->softLimitLow + mount->offsetX;
            x1 = ax->softLimitHigh + mount->offsetX;
            y0 = ay->softLimitLow + mount->offsetY;
            y1 = ay->softLimitHigh + mount->offsetY;
        }
        if (m_panel) m_panel->setBusy(true);
        std::weak_ptr<bool> alive = m_alive;
        m_tasks.searchBoardStart(board(), m_place, x0, y0, x1, y1, [this, alive](const JPBoardLocator::Result& r) {
            if (const auto a = alive.lock(); !a || !*a) return;
            if (m_panel) m_panel->setBusy(false);
            if (r.ok) locateFrom(r.board);
            else m_window.showStatus("Locate Board: " + r.why, kStatusMs);
        });
        if (!m_tasks.busy() && m_panel) m_panel->setBusy(false);   // it did not start
        return;
    }
    m_window.showStatus("Put the camera on one of the board's references, choose it, and press Camera Is on It first", kStatusMs);
}

void JPlacerBoard::locateFrom(const JPBoardSide& guess) {
    if (m_panel) m_panel->setBusy(true);
    std::weak_ptr<bool> alive = m_alive;
    m_tasks.locateBoard(board(), guess, footprints(), [this, alive](const JPBoardLocator::Result& r) {
        if (const auto a = alive.lock(); !a || !*a) return;
        if (m_panel) m_panel->setBusy(false);
        found(r);
    });
    if (!m_tasks.busy() && m_panel) m_panel->setBusy(false);   // it did not start
}

void JPlacerBoard::found(const JPBoardLocator::Result& r) {
    m_found.clear();
    for (const auto& f : r.references)
        m_found[f.designator] = f.found ? (r.ok ? mm(f.residualMm) + " mm from the fit" : "found") : "not found: " + f.why;
    if (r.ok) {
        m_place = r.board;
        m_placed = true;
        m_measured = true;
        m_stale.clear();
        if (r.affine) m_leans.push_back(r.xPerY);
        int used = 0;
        for (const auto& f : r.references) used += f.found;
        if (used < 3)
            m_window.showStatus("Located by two references: mark a third to check the fit and measure the machine's squareness", kStatusMs);
    } else {
        m_window.showStatus("Locate Board: " + r.why, kStatusMs);
    }
    save();
    show();
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
    const JPJob& job = m_job.job();
    const bool bottom = m_place.side == JPPlacement::Side::Bottom;
    const bool byHand = job.location.capture == JPLocateSettings::Capture::Manual;
    m_panel->showCapture(byHand, job.location.newBoard == JPLocateSettings::NewBoard::Reuse);
    std::vector<std::string> refs, refKeys, rows, designators;
    const JPlacerCameraTasks::Footprints fs = footprints();
    for (const JPPlacement* p : board().references(m_place.side)) {
        std::string line = p->designator + "   ";
        if (p->fiducial) line += "round mark";
        else if (fs.count(p->designator)) line += "found by its pads";
        else if (!p->look.empty()) line += "found by its look";
        else line += "not findable by camera: record it";
        if (byHand) line += p->recorded ? ", recorded at " + mm(p->recordedX) + ", " + mm(p->recordedY) : ", not recorded";
        if (const auto it = m_found.find(p->designator); it != m_found.end()) line += "; " + it->second;
        refs.push_back(line);
        refKeys.push_back(p->designator);
    }
    for (const JPPlacement& p : board().placements) {
        if (p.side != m_place.side || p.fiducial) continue;
        rows.push_back(p.designator + "   " + p.footprint + (p.value.empty() ? "" : "   " + p.value));
        designators.push_back(p.designator);
    }
    std::string summary;
    if (!board().placements.empty()) {
        summary = board().name + ": " + std::to_string(rows.size()) + " parts and " + std::to_string(refs.size())
                + " reference(s) on this side";
        if (refs.size() < 2) summary += ": mark at least two as references (Parts panel), three to check the fit";
        else if (refs.size() == 2) summary += ": a third would check the fit and measure the machine's squareness";
    }
    m_panel->showBoard(summary, bottom, refs, refKeys, rows, designators);
    if (!m_placed)
        m_panel->showPlace(board().placements.empty() ? ""
                           : byHand ? "Record each reference: Go To it, jog the camera onto it, press Record; then Locate Board."
                           : m_cell.boardStart.kind == JPBoardStartConfig::Kind::ByHand
                               ? "Put the camera on a reference, choose it, press Camera Is on It."
                               : "Locate Board finds the board from the machine's starting point.");
    else
        m_panel->showPlace(std::string(!m_stale.empty() ? "\xE2\x9A\xA0 Needs locating again (" + m_stale + "). "
                                       : m_measured ? "Located by its references" : "Starting point")
                           + ": origin at " + mm(m_place.toMachine.tx) + ", " + mm(m_place.toMachine.ty) + ", turned "
                           + mm(m_place.toMachine.rotationDeg()) + " deg");
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
        JPViewMark mark { px, py, p.fiducial ? (p.fiducialMm > 0 ? p.fiducialMm : defaultMm) / 2 * scale : 0, p.designator, p.fiducial };
        if (p.designator == m_shownDesignator) {
            // Its footprint's pads: from its own mm, mirrored for the bottom side,
            // turned, onto the board, the machine and the picture.
            const double a = m_shownDegrees * M_PI / 180, c = std::cos(a), sn = std::sin(a);
            const bool bottom = p.side == JPPlacement::Side::Bottom;
            auto toPicture = [&](double lx, double ly, double& ox, double& oy) {
                if (bottom) lx = -lx;
                double mx, my;
                m_place.toMachine.apply(p.x + lx * c - ly * sn, p.y + lx * sn + ly * c, mx, my);
                return cal.pixelFor(mx, my, vx, vy, ox, oy);
            };
            for (const JPPad& pad : m_shownFootprint.pads) {
                const double pa = pad.rotationDeg * M_PI / 180, pc = std::cos(pa), ps = std::sin(pa);
                std::vector<std::pair<double, double>> outline;
                for (const auto& [cx, cy] : { std::pair{ -1, -1 }, std::pair{ 1, -1 }, std::pair{ 1, 1 }, std::pair{ -1, 1 } }) {
                    const double hx = cx * pad.width / 2, hy = cy * pad.height / 2;
                    double ox, oy;
                    if (toPicture(pad.x + hx * pc - hy * ps, pad.y + hx * ps + hy * pc, ox, oy)) outline.emplace_back(ox, oy);
                }
                if (outline.size() == 4) mark.outlines.push_back(std::move(outline));
            }
            if (const JPPad* p1 = m_shownFootprint.pin1Pad())
                if (toPicture(p1->x, p1->y, mark.pin1X, mark.pin1Y)) {
                    mark.hasPin1 = true;
                    mark.pin1Radius = std::min(p1->width, p1->height) / 4 * scale;
                }
        }
        out.push_back(std::move(mark));
    }
    return out;
}

bool JPlacerBoard::located(JPBoardSide& out) const {
    if (!m_placed || !m_measured) return false;
    out = m_place;
    return true;
}

void JPlacerBoard::showFootprint(const std::string& designator, const JPFootprint& footprint, double degrees) {
    m_shownDesignator = designator;
    m_shownFootprint = footprint;
    m_shownDegrees = degrees;
    goTo(designator);
}

void JPlacerBoard::clearFootprint() {
    m_shownDesignator.clear();
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
                     "The board's references show the machine's Y axis leaning " + mm(lean * 100) + " mm in X per 100 mm"
                         + (m_leans.size() > 1 ? " (the mean of " + std::to_string(m_leans.size()) + " findings)" : "")
                         + ". Correct for it from now on?\n\nEvery coordinate changes a little: "
                           "home the machine again, then calibrate the camera and locate the board again.",
                     [this, alive, lean, gantry] {
                         if (const auto a = alive.lock(); !a || !*a) return;
                         m_square(gantry, lean);
                         // Measured in the old coordinates: the board is to be found again.
                         m_leans.clear();
                         m_found.clear();
                         machineChanged("the machine was squared");
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
