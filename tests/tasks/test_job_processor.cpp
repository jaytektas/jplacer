// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A job run as OpenPnP's ReferencePnpJobProcessor runs one, on a machine
// that only records what it is asked: the setup checked, the board located
// by its fiducials, each nozzle given a placement (a tip changed where
// needed), each part fed, picked and placed where the fiducials put the
// board, then the head parked; a pick that fails deferred and tried again.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPBoard.h"
#include "model/JPBoardLocation.h"
#include "model/JPConfiguration.h"
#include "model/JPJob.h"
#include "openpnp/JPXmlReader.h"
#include "tasks/JPJobProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

namespace {

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }

JPLocation at(double x, double y, double z = 0, double r = 0) { return JPLocation(JPLengthUnit::Millimeters, x, y, z, r); }

// A machine that does what it is asked at once, and remembers it.
class FakeMachine : public JPJobMachine {
public:
    std::vector<Nozzle> heads { { "N1", "N1", "T1", { "T1", "T2" } }, { "N2", "N2", "", { "T1", "T2" } } };
    // The board lies this far from where the job says.
    double shiftX = 0.1, shiftY = -0.05;
    int    failPicks = 0;   // the next picks to fail
    std::vector<std::string> log;
    struct Move { std::string nozzle; JPLocation where; };
    std::vector<Move> picks, places;

    std::vector<Nozzle> nozzles() const override { return heads; }
    std::vector<std::pair<std::string, std::string>> tips() const override { return { { "T1", "Tip 1" }, { "T2", "Tip 2" } }; }
    std::optional<JPLocation> cameraLocation() const override { return at(0, 0); }
    bool safeZ(std::string&) override { log.push_back("safeZ"); return true; }
    bool changeTip(const std::string& n, const std::string& t, std::string&) override {
        for (Nozzle& h : heads) if (h.id == n) h.tipId = t;
        log.push_back("tip " + n + " " + t);
        return true;
    }
    bool rotate(const std::string&, double, std::string&) override { return true; }
    bool pick(const std::string& n, const JPLocation& where, std::string& why) override {
        if (failPicks > 0) {
            --failPicks;
            why = "no part detected";
            log.push_back("pick failed " + n);
            return false;
        }
        picks.push_back({ n, where });
        log.push_back("pick " + n);
        return true;
    }
    bool place(const std::string& n, const JPLocation& where, std::string&) override {
        places.push_back({ n, where });
        log.push_back("place " + n);
        return true;
    }
    bool discard(const std::string& n, std::string&) override { log.push_back("discard " + n); return true; }
    bool positionNozzle(const std::string&, const JPLocation&, std::string&) override { return true; }
    bool actuate(const std::string& name, double value, std::string&) override {
        actuated.push_back(name + "=" + std::to_string(int(value)));
        return true;
    }
    std::vector<std::string> actuated;
    bool park(std::string&) override { log.push_back("park"); return true; }
    bool locateFiducial(const JPLocation& nominal, double diameterMm, const FiducialLook&, JPLocation& found,
                        std::string& why) override {
        if (!near(diameterMm, 1.0)) {
            why = "wrong size";
            return false;
        }
        found = nominal.add(at(shiftX, shiftY));
        log.push_back("fiducial");
        return true;
    }
    bool alignPart(const std::string&, const AlignRequest& rq, AlignResult& r, std::string&) override {
        r.nozzleAngle = rq.imageAngle;
        r.dx = alignDx;
        r.dy = 0;
        r.partAngle = rq.imageAngle + alignDa;
        ++aligns;
        return true;
    }
    double alignDx = 0, alignDa = 0;
    int    aligns = 0;
    bool locateHole(const JPLocation& nominal, double, double, double, double, JPLocation& found, std::string&) override {
        found = nominal;
        log.push_back("hole");
        return true;
    }
};

JPFeeder tray(const std::string& id, const std::string& part, double x, double y) {
    JPXmlElement e;
    std::string error;
    const std::string xml = "<feeder class=\"org.openpnp.machine.reference.feeder.ReferenceTrayFeeder\" id=\"" + id +
                            "\" name=\"" + id + "\" enabled=\"true\" part-id=\"" + part +
                            "\" feed-retry-count=\"0\" pick-retry-count=\"0\" tray-count-x=\"10\" tray-count-y=\"1\" "
                            "feed-count=\"0\"><location units=\"Millimeters\" x=\"" + std::to_string(x) + "\" y=\"" +
                            std::to_string(y) + "\" z=\"-5\" rotation=\"0\"/><offsets units=\"Millimeters\" x=\"4\" "
                            "y=\"0\" z=\"0\" rotation=\"0\"/></feeder>";
    const bool ok = JPXmlReader::parse(xml, e, error);
    assert(ok);
    return JPFeeder::fromXml(e);
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-job-processor";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());

    // Packages: one for each tip, and a fiducial's (a 1 mm pad).
    for (const auto& [id, tip] : std::vector<std::pair<std::string, std::string>> { { "P1", "T1" }, { "P2", "T2" }, { "PF", "T1" } }) {
        auto pkg = std::make_shared<JPPackage>();
        pkg->id = id;
        pkg->compatibleNozzleTipIds = { tip };
        if (id == "PF") pkg->footprint.pads.push_back({ "1", 0, 0, 1.0, 1.0 });
        config.addPackage(pkg);
    }
    for (const auto& [id, pkg] : std::vector<std::pair<std::string, std::string>> { { "R1", "P1" }, { "C1", "P2" }, { "FID", "PF" } }) {
        auto part = std::make_shared<JPPart>();
        part->id = id;
        part->packageId = pkg;
        part->height = JPLength(0.5, JPLengthUnit::Millimeters);
        config.addPart(part);
    }
    config.addFeeder(tray("FR", "R1", 10, 10));
    config.addFeeder(tray("FC", "C1", 10, 30));

    // A board with two fiducials and three placements, at 100, 50.
    auto board = std::make_shared<JPBoard>();
    board->file = (dir / "b.board.xml").string();
    auto placement = [](const std::string& id, const std::string& part, double x, double y, JPPlacement::Type t) {
        JPPlacement p;
        p.id = id;
        p.partId = part;
        p.location = at(x, y, 0, 90);
        p.type = t;
        return p;
    };
    board->placements = { placement("F1", "FID", 0, 0, JPPlacement::Type::Fiducial),
                          placement("F2", "FID", 30, 20, JPPlacement::Type::Fiducial),
                          placement("R1a", "R1", 5, 5, JPPlacement::Type::Placement),
                          placement("R1b", "R1", 10, 5, JPPlacement::Type::Placement),
                          placement("C1a", "C1", 15, 5, JPPlacement::Type::Placement) };
    JPJob job;
    auto l = std::make_unique<JPBoardLocation>();
    l->id = "Brd1";
    l->holder = board;
    l->checkFiducials = true;
    l->setLocation(at(100, 50, 1.6, 0));
    JPBoardLocation* bl = static_cast<JPBoardLocation*>(job.addBoardOrPanelLocation(std::move(l)));

    FakeMachine machine;
    JPJobProcessor::Settings settings;
    std::vector<std::string> statuses;
    JPJobProcessor::Hooks hooks;
    hooks.status = [&](const std::string& s) { statuses.push_back(s); };
    {
        JPJobProcessor run(config, job, machine, settings, hooks);
        JPJobProcessor::Failure f;
        JPJobProcessor::Result r = JPJobProcessor::Result::More;
        int steps = 0;
        while ((r = run.next(f)) == JPJobProcessor::Result::More) assert(++steps < 200);
        assert(r == JPJobProcessor::Result::Finished);
        assert(run.totalPartsPlaced() == 3);
    }
    // Every placement placed, where the fiducials put the board, as high as the part.
    for (const char* id : { "R1a", "R1b", "C1a" }) assert(job.retrievePlacedStatus(*bl, id));
    assert(machine.places.size() == 3 && machine.picks.size() == 3);
    bool sawC1 = false;
    for (const auto& p : machine.places) {
        assert(near(p.where.z(), 1.6 + 0.5));
        assert(near(p.where.rotation(), 90));
        if (near(p.where.x(), 115.1) && near(p.where.y(), 54.95)) sawC1 = (p.nozzle == "N2");
    }
    assert(sawC1);
    // N2 had no tip: T2 loaded for C1; R1's on N1 with its T1, no change.
    assert(std::find(machine.log.begin(), machine.log.end(), "tip N2 T2") != machine.log.end());
    assert(std::find(machine.log.begin(), machine.log.end(), "tip N1 T1") == machine.log.end());
    assert(std::count(machine.log.begin(), machine.log.end(), "fiducial") == 2);
    assert(machine.log.back() == "park");
    assert(statuses.back().rfind("Job finished without error, placed 3 parts", 0) == 0);
    // The trays moved on: the second R1 from the second pocket.
    assert(config.feeder("FR")->number("feed-count") == 2);

    // Again, nothing left to place: it finishes at once.
    {
        machine.places.clear();
        JPJobProcessor run(config, job, machine, settings, hooks);
        JPJobProcessor::Failure f;
        while (run.next(f) == JPJobProcessor::Result::More) {}
        assert(machine.places.empty());
    }

    // A pick that fails: Alert stops the job there, telling of the feeder;
    // going on picks again.
    job.removeAllPlacedStatus();
    machine.places.clear();
    machine.failPicks = 1;
    {
        JPJobProcessor run(config, job, machine, settings, hooks);
        JPJobProcessor::Failure f;
        JPJobProcessor::Result r;
        while ((r = run.next(f)) == JPJobProcessor::Result::More) {}
        assert(r == JPJobProcessor::Result::Failed && f.source == JPJobProcessor::Failure::Source::Feeder);
        assert(f.message == "no part detected");
        while ((r = run.next(f)) == JPJobProcessor::Result::More) {}
        assert(r == JPJobProcessor::Result::Finished && run.totalPartsPlaced() == 3);
    }

    // Deferred: the feeder's fault counted, the placement tried again later, the job not stopped.
    job.removeAllPlacedStatus();
    job.errorHandling = JPJob::ErrorHandling::Defer;
    machine.failPicks = 1;
    {
        JPJobProcessor run(config, job, machine, settings, hooks);
        JPJobProcessor::Failure f;
        JPJobProcessor::Result r;
        while ((r = run.next(f)) == JPJobProcessor::Result::More) {}
        assert(r == JPJobProcessor::Result::Finished && run.totalPartsPlaced() == 3);
    }
    assert(config.feeder("FR")->summariseJobFaults().find('X') != std::string::npos ||
           config.feeder("FC")->summariseJobFaults().find('X') != std::string::npos);

    // A fiducial that is not where the board says by far: refused, the board left as it was.
    job.removeAllPlacedStatus();
    job.errorHandling = JPJob::ErrorHandling::Alert;
    machine.shiftX = 9;
    {
        JPJobProcessor run(config, job, machine, settings, hooks);
        JPJobProcessor::Failure f;
        JPJobProcessor::Result r;
        while ((r = run.next(f)) == JPJobProcessor::Result::More) {}
        assert(r == JPJobProcessor::Result::Failed && f.source == JPJobProcessor::Failure::Source::Board);
        assert(f.message.find("the board origin moved 9.0001mm") != std::string::npos);
    }

    // Bottom vision: the part found 0.2 mm off the nozzle and turned a degree
    // too far; placed with both taken off.
    machine.shiftX = 0.1;
    job.removeAllPlacedStatus();
    config.addVisionSettings(JPVisionSettings::create(JPVisionSettings::Kind::Bottom, "BVS_Default"));
    for (const char* pkg : { "P1", "P2" }) config.package(pkg)->footprint.pads.push_back({ "1", 0, 0, 1.0, 1.0 });
    machine.alignDx = 0.2;
    machine.alignDa = 1;
    machine.places.clear();
    {
        JPJobProcessor run(config, job, machine, settings, hooks);
        run.setVision(JPVisionConfig {});
        JPJobProcessor::Failure f;
        JPJobProcessor::Result r;
        while ((r = run.next(f)) == JPJobProcessor::Result::More) {}
        assert(r == JPJobProcessor::Result::Finished && machine.aligns == 3);
    }
    bool sawAligned = false;
    for (const auto& p : machine.places)
        if (std::abs(p.where.y() - (54.95 - 0.2 * std::sin(-M_PI / 180))) < 1e-6 && p.nozzle == "N2") {
            // C1a at 115.1, 54.95, turned to 90: the nozzle at 89 and 0.2 mm (turned by -1 deg) back.
            assert(std::abs(p.where.x() - (115.1 - 0.2 * std::cos(-M_PI / 180))) < 1e-6 && std::abs(p.where.rotation() - 89) < 1e-9);
            sawAligned = true;
        }
    assert(sawAligned);

    // A part with no feeder: the setup check says so before anything moves.
    job.removeAllPlacedStatus();
    machine.shiftX = 0.1;
    config.removeFeeder("FC");
    machine.log.clear();
    {
        JPJobProcessor run(config, job, machine, settings, hooks);
        JPJobProcessor::Failure f;
        assert(run.next(f) == JPJobProcessor::Result::Failed);
        assert(f.message == "No compatible, enabled feeder found for part C1" && machine.log.empty());
    }
    return 0;
}
