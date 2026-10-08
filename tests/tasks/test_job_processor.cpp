// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A job run as OpenPnP's ReferencePnpJobProcessor runs one, on a machine
// that only records what it is asked: the setup checked, the board located
// by its fiducials, each nozzle given a placement (a tip changed where
// needed), each part fed, picked and placed where the fiducials put the
// board, then the head parked; a pick that fails deferred and tried again; a part no feeder holds asked
// for when the run reaches it (load as you go), loaded or skipped.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <tuple>

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
#include <map>
#include <set>

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
    bool cameraReaches(const JPLocation&) const override { return true; }
    TipPush tipPush(const std::string&) const override { return { true, 1.0 }; }
    std::string holdingPart(const std::string&) const override { return {}; }
    std::string chosenNozzle() const override { return {}; }
    std::optional<JPLocation> cameraLocation() const override { return at(0, 0); }
    bool safeZ(std::string&) override { log.push_back("safeZ"); return true; }
    bool changeTip(const std::string& n, const std::string& t, std::string&) override {
        for (Nozzle& h : heads) if (h.id == n) h.tipId = t;
        log.push_back("tip " + n + " " + t);
        return true;
    }
    bool rotate(const std::string&, double, std::string&) override { return true; }
    // As the cell: a nozzle's rotation its axis's plus its rotation mode
    // offset; what was picked and placed kept as the axis turned.
    std::map<std::string, double> rotationOffsets;
    std::vector<double>           placedOffsets;   // each place's
    void setRotationModeOffset(const std::string& n, std::optional<double> offset) override {
        if (offset) rotationOffsets[n] = *offset;
        else rotationOffsets.erase(n);
    }
    JPLocation axisTurned(const std::string& n, const JPLocation& where) const {
        const auto o = rotationOffsets.find(n);
        return o == rotationOffsets.end() ? where : where.derive(std::nullopt, std::nullopt, std::nullopt, where.rotation() - o->second);
    }
    bool pick(const std::string& n, const JPLocation& where, std::string& why) override {
        if (failPicks > 0) {
            --failPicks;
            why = "no part detected";
            log.push_back("pick failed " + n);
            return false;
        }
        picks.push_back({ n, axisTurned(n, where) });
        log.push_back("pick " + n);
        return true;
    }
    bool place(const std::string& n, const JPLocation& where, std::string&) override {
        places.push_back({ n, axisTurned(n, where) });
        placedOffsets.push_back(rotationOffsets.count(n) ? rotationOffsets.at(n) : 0.0);
        log.push_back("place " + n);
        return true;
    }
    bool discard(const std::string& n, std::string&) override { log.push_back("discard " + n); return true; }
    bool positionNozzle(const std::string&, const JPLocation&, std::string&) override { return true; }
    bool positionCamera(const JPLocation&, std::string&) override { return true; }
    bool seeCircles(const JPLocation&, JPPipeline&, SeenCircles&, std::string& why) override {
        why = "no camera";
        return false;
    }
    bool lookThrough(const JPLocation&, JPPipeline&, Sight&, std::string& why) override {
        why = "no camera";
        return false;
    }
    bool cameraSight(Sight&, std::string& why) override {
        why = "no camera";
        return false;
    }
    void showOnCamera(const cv::Mat&, int) override {}
    // The QR codes seen at each place looked at, by its X (none: none seen).
    std::map<int, std::vector<QrCode>> qr;
    bool readQrCodes(const JPLocation& at, std::vector<QrCode>& codes, std::string&) override {
        const auto it = qr.find(int(std::lround(at.x())));
        codes = it == qr.end() ? std::vector<QrCode> {} : it->second;
        return true;
    }
    bool moveNozzle(const std::string&, std::array<std::optional<double>, 4> to, double, bool, std::string&) override {
        if (to[2]) lastZ = *to[2];
        return true;
    }
    // Nozzle tips not yet calibrated (their runout to measure), by nozzle.
    std::set<std::string> uncalibrated { "N1" };
    bool tipCalibrated(const std::string& n) const override { return !uncalibrated.count(n); }
    bool calibrateTip(const std::string& n, std::string&) override {
        log.push_back("calibrate " + n);
        uncalibrated.erase(n);
        return true;
    }
    // Contact probing: met 1.2 below where the nozzle was taken (its start offset, 1, above where it should): 0.2 low.
    double lastZ = 0;
    int    probes = 0;
    std::map<std::string, double> offsets;
    bool contactProbe(const std::string&, bool forward, double, double& z, std::string&) override {
        if (forward) ++probes;
        z = forward ? lastZ - 1.2 : lastZ;
        return true;
    }
    std::optional<double> probedOffset(const std::string& n, bool feeder, const std::string& key) const override {
        const auto it = offsets.find(n + (feeder ? "/feeder/" : "/part/") + key);
        if (it == offsets.end()) return std::nullopt;
        return it->second;
    }
    void setProbedOffset(const std::string& n, bool feeder, const std::string& key, double offset) override {
        offsets[n + (feeder ? "/feeder/" : "/part/") + key] = offset;
    }
    bool vacuumOn(const std::string&, std::string&) override { return true; }
    bool zeroActuatorRotation(const std::string&, std::string&) override { return true; }
    bool positionActuator(const std::string&, std::array<std::optional<double>, 4>, double, bool, std::string&) override { return true; }
    bool pickHere(const std::string&, std::string&) override { return true; }
    bool readVacuum(const std::string&, double& level, std::string&) override {
        level = 0;
        return true;
    }
    bool seeRects(const JPLocation&, JPPipeline&, int, SeenRects&, std::string& why) override {
        why = "no camera";
        return false;
    }
    bool actuate(const std::string& name, double value, std::string&) override {
        actuated.push_back(name + "=" + std::to_string(int(value)));
        return true;
    }
    std::vector<std::string> actuated;
    bool homed = true;
    bool isHomed() const override { return homed; }
    // What a read gives: by actuator name.
    std::map<std::string, std::string> readings;
    bool readActuator(const std::string& name, const std::string& parameter, std::string& value, std::string& why) override {
        actuated.push_back("read " + name + "(" + parameter + ")");
        const auto r = readings.find(name);
        if (r == readings.end()) {
            why = "Unable to find an actuator named " + name;
            return false;
        }
        value = r->second;
        return true;
    }
    bool actuateText(const std::string& name, const std::string& value, std::string&) override {
        actuated.push_back(name + "=" + value);
        return true;
    }
    bool moveActuator(const std::string& name, const JPLocation& at, bool withZ, double speed, std::string&) override {
        char text[160];
        std::snprintf(text, sizeof text, "%s to %.2f,%.2f%s at %.2f", name.c_str(), at.x(), at.y(),
                      withZ ? (" z " + std::to_string(int(at.z()))).c_str() : "", speed);
        actuated.push_back(text);
        return true;
    }
    // Where the template is found: `at` less this.
    JPLocation templateOffset { JPLengthUnit::Millimeters };
    int        templateLooks = 0;
    bool matchTemplate(const JPLocation&, const std::string&, const JPTemplateFinder::Area&, JPLocation& offset,
                       std::string&) override {
        ++templateLooks;
        offset = templateOffset;
        return true;
    }
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
    // N1's tip not calibrated: calibrated once, before its first pick (OpenPnP's CalibrateNozzleTips).
    assert(std::count(machine.log.begin(), machine.log.end(), "calibrate N1") == 1);
    assert(std::find(machine.log.begin(), machine.log.end(), "calibrate N1")
           < std::find(machine.log.begin(), machine.log.end(), "pick N1"));
    // Contact probing, each feeder and part probed once (OpenPnP's ContactProbeNozzle, "Once"): every pick
    // and place 0.2 lower than it would be; four probes (two feeders, two parts), the rest by the offsets kept.
    {
        // (The trays' counts, the log and what was picked and placed put back after.)
        const int fr = config.feeder("FR")->number("feed-count"), fc = config.feeder("FC")->number("feed-count");
        const auto log = machine.log;
        const auto picks = machine.picks, places = machine.places;
        job.removeAllPlacedStatus();
        for (auto& h : machine.heads) {
            h.contactProbe.nozzle = true;
            h.contactProbe.method = "ContactSenseActuator";
            h.contactProbe.feederHeightProbing = "Once";
            h.contactProbe.partHeightProbing = "Once";
        }
        machine.picks.clear();
        machine.places.clear();
        JPJobProcessor run(config, job, machine, settings, hooks);
        JPJobProcessor::Failure f;
        JPJobProcessor::Result r;
        while ((r = run.next(f)) == JPJobProcessor::Result::More) {}
        assert(r == JPJobProcessor::Result::Finished);
        assert(machine.probes == 4 && machine.picks.size() == 3 && machine.places.size() == 3);
        for (const auto& p : machine.picks) assert(std::abs(p.where.z() - (-5.2)) < 1e-9);
        for (const auto& p : machine.places) assert(std::abs(p.where.z() - (1.6 + 0.5 - 0.2)) < 1e-9);
        for (auto& h : machine.heads) h.contactProbe = JPNozzleConfig::ContactProbe {};
        config.feeder("FR")->setNumber("feed-count", fr);
        config.feeder("FC")->setNumber("feed-count", fc);
        machine.log = log;
        machine.picks = picks;
        machine.places = places;
        for (const char* id : { "R1a", "R1b", "C1a" }) job.storePlacedStatus(*bl, id, true);
    }

    // OpenPnP's Rotation Modes (no bottom vision here): PlacementAngle picks
    // the part already turned against its placement, so the nozzle places at
    // 0; LimitedArticulation turns about the middle of its axis's range (here
    // 90 degrees from pick to place, with 15 + 30 to spare: from -67.5 to 22.5).
    // (The trays' counts, the log and what was picked and placed kept for the checks after.)
    const int frCount = config.feeder("FR")->number("feed-count"), fcCount = config.feeder("FC")->number("feed-count");
    const auto keptLog = machine.log;
    const auto keptPicks = machine.picks, keptPlaces = machine.places;
    for (const auto& [mode, pickTurn, placeTurn] : std::vector<std::tuple<std::string, double, double>> {
             { "PlacementAngle", -90, 0 }, { "LimitedArticulation", -67.5, 22.5 }, { "AbsolutePartAngle", 0, 90 } }) {
        for (auto& h : machine.heads) h.rotationMode = mode;
        job.removeAllPlacedStatus();
        machine.picks.clear();
        machine.places.clear();
        JPJobProcessor run(config, job, machine, settings, hooks);
        JPJobProcessor::Failure f;
        JPJobProcessor::Result r = JPJobProcessor::Result::More;
        int steps = 0;
        while ((r = run.next(f)) == JPJobProcessor::Result::More) assert(++steps < 200);
        assert(r == JPJobProcessor::Result::Finished && machine.places.size() == 3);
        for (const auto& p : machine.picks) assert(near(p.where.rotation(), pickTurn, 1e-3));
        for (const auto& p : machine.places) assert(near(p.where.rotation(), placeTurn, 1e-3));
    }
    for (auto& h : machine.heads) h.rotationMode = "AbsolutePartAngle";
    config.feeder("FR")->setNumber("feed-count", frCount);
    config.feeder("FC")->setNumber("feed-count", fcCount);
    machine.log = keptLog;
    machine.picks = keptPicks;
    machine.places = keptPlaces;
    job.removeAllPlacedStatus();
    for (const char* id : { "R1a", "R1b", "C1a" }) job.storePlacedStatus(*bl, id, true);
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
    // OpenPnP's Align with Part: bottom vision's turn taken into the rotation
    // mode offset, the nozzle placing at the placement's angle as it reads it:
    // the axis turned just the same.
    for (auto& h : machine.heads) h.alignRotationWithPart = true;
    job.removeAllPlacedStatus();
    for (const char* id : { "FR", "FC" }) config.feeder(id)->setEnabled(true);
    config.feeder("FR")->setNumber("feed-count", frCount);
    config.feeder("FC")->setNumber("feed-count", fcCount);
    machine.places.clear();
    machine.placedOffsets.clear();
    {
        JPJobProcessor run(config, job, machine, settings, hooks);
        run.setVision(JPVisionConfig {});
        JPJobProcessor::Failure f;
        JPJobProcessor::Result r;
        while ((r = run.next(f)) == JPJobProcessor::Result::More) {}
        assert(r == JPJobProcessor::Result::Finished);
    }
    bool sawAlignedOffset = false;
    for (size_t i = 0; i < machine.places.size(); ++i)
        if (machine.places[i].nozzle == "N2" && std::abs(machine.places[i].where.rotation() - 89) < 1e-9) {
            assert(std::abs(machine.placedOffsets[i] - 1) < 1e-9);   // the part's 1 degree turn
            sawAlignedOffset = true;
        }
    assert(sawAlignedOffset);
    for (auto& h : machine.heads) h.alignRotationWithPart = false;

    // Load as you go: a part with no feeder is not a setup failure. What is loaded is placed first, then the
    // run pauses asking for the part; loaded, it goes on from there.
    job.removeAllPlacedStatus();
    machine.shiftX = 0.1;
    config.removeFeeder("FC");
    {
        JPJobProcessor run(config, job, machine, settings, hooks);
        JPJobProcessor::Failure f;
        JPJobProcessor::Result r = JPJobProcessor::Result::More;
        int steps = 0;
        while ((r = run.next(f)) == JPJobProcessor::Result::More) assert(++steps < 200);
        assert(r == JPJobProcessor::Result::Failed && f.loadPartId == "C1" && f.message == "Load C1: no feeder holds it.");
        assert(run.totalPartsPlaced() == 2);   // both R1s
        config.addFeeder(tray("FC", "C1", 10, 30));
        while ((r = run.next(f)) == JPJobProcessor::Result::More) assert(++steps < 400);
        assert(r == JPJobProcessor::Result::Finished && run.totalPartsPlaced() == 3);
    }
    // Skipped: its placements left in error, the run finished without them.
    job.removeAllPlacedStatus();
    config.removeFeeder("FC");
    {
        JPJobProcessor run(config, job, machine, settings, hooks);
        JPJobProcessor::Failure f;
        JPJobProcessor::Result r = JPJobProcessor::Result::More;
        int steps = 0;
        while ((r = run.next(f)) == JPJobProcessor::Result::More) assert(++steps < 200);
        assert(r == JPJobProcessor::Result::Failed && f.loadPartId == "C1");
        run.skipPart("C1");
        while ((r = run.next(f)) == JPJobProcessor::Result::More) assert(++steps < 400);
        assert(r == JPJobProcessor::Result::Finished && run.totalPartsPlaced() == 2);
        bool skipped = false;
        for (const auto& j : run.jobPlacements())
            if (j.placementId == "C1a") skipped = j.status == JPJobProcessor::Status::Errored && j.error == "Skipped: not loaded";
        assert(skipped);
    }
    return 0;
}
