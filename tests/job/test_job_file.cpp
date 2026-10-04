// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A job saved and read back keeps every placement field and its own parts;
// a file from another version, or not JSON, is refused with a reason.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "job/JPJob.h"

#include <filesystem>
#include <fstream>

#include <unistd.h>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    JPJob job;
    job.board.name = "controller";
    job.sources = { { JPSource::Kind::Placements, "/boards/controller-cpl.csv", JPCadTool::Kind::KiCad, "0123" },
                    { JPSource::Kind::Bom, "/boards/controller-bom.csv", JPCadTool::Kind::Other, "4567" } };
    job.frame = { 1.5, -2, 100, 80 };
    JPPackage k;
    k.name = "SOIC-8";
    k.turnDeg = 180;
    const std::string kid = job.parts.add(std::move(k));
    job.parts.addName(kid, "SOIC-8_3.9x4.9mm_P1.27mm");
    JPPart part;
    part.mpn = "LM358DT";
    part.packageId = kid;
    const std::string pid = job.parts.add(std::move(part));

    JPPlacement u1;
    u1.designator = "U1";
    u1.x = 10.5;
    u1.y = -3.25;
    u1.rotationDeg = 90;
    u1.side = JPPlacement::Side::Bottom;
    u1.hasPin1 = true;
    u1.pin1X = 8;
    u1.pin1Y = -2;
    u1.supplierNumbers = { { "LCSC", "C7950" } };
    u1.mpn = "LM358DT";
    u1.voltage = "30V";
    u1.mounting = JPPlacement::Mounting::Smd;
    u1.footprint = "SOIC-8_3.9x4.9mm_P1.27mm";
    u1.pins = 8;
    u1.other = { { "3D Model", "SOIC-8" } };
    u1.partId = pid;
    u1.partGuessed = true;
    job.board.placements.push_back(u1);
    JPPlacement fid;
    fid.designator = "FID1";
    fid.fiducial = true;
    fid.fiducialMm = 1;
    fid.doNotPlace = true;
    job.board.placements.push_back(fid);

    const fs::path path = fs::temp_directory_path() / ("jplacer-test-" + std::to_string(::getpid()) + ".jpjob");
    std::string error;
    assert(job.save(path.string(), error));
    JPJob back;
    assert(JPJob::load(path.string(), back, error));
    assert(back.board.name == "controller" && back.sources.size() == 2 && back.sources[0].tool == JPCadTool::Kind::KiCad);
    assert(back.sources[1].kind == JPSource::Kind::Bom && back.sources[1].fingerprint == "4567");
    assert(back.frame.originX == 1.5 && back.frame.originY == -2 && back.frame.hasOutline() && back.frame.height == 80);
    assert(back.board.placements.size() == 2);
    const JPPlacement* b = back.board.find("U1");
    assert(b && b->x == 10.5 && b->y == -3.25 && b->rotationDeg == 90 && b->side == JPPlacement::Side::Bottom);
    assert(b->hasPin1 && b->pin1X == 8 && b->pin1Y == -2);
    assert(b->supplierNumbers == u1.supplierNumbers && b->mpn == "LM358DT" && b->voltage == "30V");
    assert(b->mounting == JPPlacement::Mounting::Smd && b->footprint == u1.footprint && b->pins == 8);
    assert(b->other == u1.other && b->partId == pid && b->partGuessed);
    const JPPlacement* f = back.board.find("FID1");
    assert(f && f->fiducial && f->fiducialMm == 1 && f->doNotPlace && !f->hasPin1 && f->partId.empty());
    assert(back.parts.part(pid)->packageId == kid && back.parts.packageNamed("SOIC-8_3.9x4.9mm_P1.27mm")->turnDeg == 180);

    { std::ofstream(path, std::ios::trunc) << "{ \"version\": 9 }"; }
    assert(!JPJob::load(path.string(), back, error) && error.find("another version") != std::string::npos);
    { std::ofstream(path, std::ios::trunc) << "not json"; }
    assert(!JPJob::load(path.string(), back, error));
    fs::remove(path);
    return 0;
}
