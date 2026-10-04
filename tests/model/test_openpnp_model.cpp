// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's model read from OpenPnP's own sample files: parts and packages,
// a board, an older job converted (and backed up), a panelised job with
// pseudo-placements; placements put on the machine through board and panel
// transforms; what a job sets kept by unique id; and everything written
// back and read again the same.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPConfiguration.h"
#include "model/JPLengthUnits.h"
#include "openpnp/JPXmlWriter.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace jf;
namespace fs = std::filesystem;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

// The sample files copied where the test may write beside them.
fs::path copySamples() {
    const fs::path to = fs::temp_directory_path() / "jplacer-test-openpnp-model";
    fs::remove_all(to);
    fs::create_directories(to);
    fs::copy(fs::path(JPLACER_TESTDATA_DIR) / "openpnp", to, fs::copy_options::recursive);
    return to;
}

} // namespace

int main() {
    // Lengths and units, as OpenPnP converts and parses them.
    {
        assert(near(JPLength(1, JPLengthUnit::Inches).convertToUnits(JPLengthUnit::Millimeters).value(), 25.4));
        assert(near(JPLength(1000, JPLengthUnit::Mils).convertToUnits(JPLengthUnit::Inches).value(), 1.0, 1e-12));
        auto l = JPLength::parse("12.5 mm");
        assert(l && l->hasUnits() && l->units() == JPLengthUnit::Millimeters && l->value() == 12.5);
        l = JPLength::parse("20mil");
        assert(l && l->units() == JPLengthUnit::Mils);
        l = JPLength::parse("5um");
        assert(l && l->units() == JPLengthUnit::Microns);
        l = JPLength::parse("3");
        assert(l && !l->hasUnits() && l->changeUnitsIfUnspecified(JPLengthUnit::Inches).units() == JPLengthUnit::Inches);
        assert(!JPLength::parse("3", true) && !JPLength::parse("abc"));
        assert(JPLength(1.5, JPLengthUnit::Millimeters).text() == "1.500mm");
    }
    // Numbers written as Java writes a double.
    {
        assert(JPXmlWriter::number(1) == "1.0" && JPXmlWriter::number(0) == "0.0" && JPXmlWriter::number(0.25) == "0.25");
        assert(JPXmlWriter::number(-90) == "-90.0" && JPXmlWriter::number(1e-4) == "1.0E-4");
        assert(JPXmlWriter::number(12345678) == "1.2345678E7" && JPXmlWriter::number(183.95466039866614) == "183.95466039866614");
    }

    const fs::path dir = copySamples();
    std::string error;

    // Parts and packages: found without regard to case, footprints whole.
    JPConfiguration config((dir / "config").string());
    std::vector<std::string> problems;
    assert(config.load(problems, error) && problems.empty());
    assert(config.parts().size() == 6 && config.packages().size() == 6);
    const JPPart* r = config.part("r0805-1k");
    assert(r && r->packageId == "R0805" && r->height.value() == 1.0 && r->speed == 1.0);
    const JPPackage* k = config.package(r->packageId);
    assert(k && k->footprint.pads.size() == 2 && k->footprint.bodyWidth == 2.0 && k->compatibleNozzleTipIds.size() == 1);
    assert(k->footprint.pads[1].x == 0.825 && k->footprint.pads[0].roundness == 0.0);
    // Saved and read again: the same.
    assert(config.save(error));
    JPConfiguration again((dir / "config").string());
    assert(again.load(problems, error) && again.parts().size() == 6 && again.packages().size() == 6);
    assert(again.package("R0805")->footprint.pads[1].x == 0.825 && again.part("FIDUCIAL-HOME")->packageId == "FIDUCIAL-1X2");
    // A part of the same id replaces it where it was.
    {
        auto p = std::make_shared<JPPart>(*again.part("R0603-1K"));
        p->height = JPLength(0.8, JPLengthUnit::Millimeters);
        again.addPart(p);
        assert(again.parts().size() == 6 && again.parts()[4]->height.value() == 0.8);
    }

    // A footprint's generators.
    {
        JPFootprint f;
        f.padCount = 8;
        f.padPitch = 1.27;
        f.padAcross = 0.6;
        f.outerDimension = 7;
        f.innerDimension = 3;
        assert(f.generate(JPFootprint::Generator::Dual, error) && f.pads.size() == 8);
        assert(near(f.pads[0].x, -2.5) && near(f.pads[0].y, 1.905) && f.pads[0].rotation == 180 && near(f.pads[4].y, -1.905));
        assert(f.bodyWidth == 3 && near(f.bodyHeight, 5.08));
        JPFootprint q;
        q.padCount = 6;
        assert(!q.generate(JPFootprint::Generator::Quad, error) && !error.empty());
    }

    // The board: its placements, the older types made new.
    JPConfiguration jobs((dir / "config").string());
    assert(jobs.load(problems, error));
    const std::string boardFile = (dir / "pnp-test" / "pnp-test.board.xml").string();
    auto board = jobs.board(boardFile, error);
    assert(board && board->placements.size() == 30 && board->dimensions.x() == 37.0 && board->dimensions.y() == 28.5);
    const JPPlacement* r7 = board->find("R7");
    assert(r7 && r7->type == JPPlacement::Type::Placement && !r7->enabled && r7->location.rotation() == 315.0);
    assert(board->find("FID4")->type == JPPlacement::Type::Fiducial && board->find("FID4")->side == JPSide::Bottom);
    assert(jobs.board(boardFile, error) == board);   // known by its file

    // An older job (version 1): eight boards, converted, a backup beside it.
    {
        auto job = jobs.loadJob((dir / "pnp-test" / "pnp-test.job.xml").string(), error);
        if (!job) std::fprintf(stderr, "%s\n", error.c_str());
        assert(job && job->dirty && job->boardLocations().size() == 8);
        assert(fs::exists(dir / "pnp-test" / "pnp-test.legacy.job.xml"));
        const auto bls = job->boardLocations();
        assert(bls[0]->id == "Brd1" && bls[7]->id == "Brd8" && bls[0]->board()->placements.size() == 30);
        assert(bls[0]->board()->definition() == board.get() && bls[0]->checkFiducials);
        // R1 (31, 6) on Brd1, top side, at (3.99…, 4.41…) turned 0.0079°.
        const JPLocation l = bls[0]->location();
        const double a = l.rotation() * M_PI / 180;
        const JPLocation p = bls[0]->placementLocation(board->find("R1")->location);
        assert(near(p.x(), l.x() + 31 * std::cos(a) - 6 * std::sin(a)) && near(p.y(), l.y() + 31 * std::sin(a) + 6 * std::cos(a)));
        assert(near(p.rotation(), l.rotation(), 1e-9));
        // On Brd3, bottom side up: mirrored across the board's width first.
        JPBoardLocation* b3 = bls[2];
        assert(b3->side == JPSide::Bottom);
        const JPLocation l3 = b3->location();
        const double a3 = l3.rotation() * M_PI / 180;
        const JPLocation p3 = b3->placementLocation(board->find("R1")->location);
        const double mx = 37 - 31, my = 6;
        assert(near(p3.x(), l3.x() + mx * std::cos(a3) - my * std::sin(a3), 1e-9));
        assert(near(p3.y(), l3.y() + mx * std::sin(a3) + my * std::cos(a3), 1e-9));
        // And back from the machine onto the board.
        const JPLocation back = b3->placementLocationInverse(p3);
        assert(near(back.x(), 31, 1e-9) && near(back.y(), 6, 1e-9));
        // Placed, and the job's own enabled state, by unique id; saved and read again.
        job->storePlacedStatus(*bls[1], "R1", true);
        bls[1]->board()->find("R1")->enabled = false;
        bls[2]->locallyEnabled = false;
        assert(job->totalActivePlacements(bls[0]) > 0 && job->totalActivePlacements(bls[2]) == 0);
        const std::string saved = (dir / "pnp-test" / "saved.job.xml").string();
        assert(jobs.saveJob(*job, saved, error) && !job->dirty);
        assert(job->enabledStateMap.count("Brd2" + std::string(JPPlacementsHolderLocation::kIdDelimiter) + "R1"));
        // A board straight in the job keeps its own "locally-enabled"; only
        // boards and panels inside panels go in the map, as in OpenPnP.
        assert(!job->enabledStateMap.count("Brd3"));
        auto reread = jobs.loadJob(saved, error);
        assert(reread && !reread->dirty && reread->boardLocations().size() == 8 && reread->version == 2.0);
        const auto rb = reread->boardLocations();
        assert(reread->retrievePlacedStatus(*rb[1], "R1") && !reread->retrievePlacedStatus(*rb[0], "R1"));
        assert(!rb[1]->board()->find("R1")->enabled && rb[0]->board()->find("R1")->enabled && !rb[2]->locallyEnabled);
        // The definition is untouched by what the job set.
        assert(board->find("R1")->enabled);
    }

    // A panelised job: two boards and two panels of three boards each.
    {
        auto job = jobs.loadJob((dir / "pnp-test" / "pnp-test-panelized.job.xml").string(), error);
        if (!job) std::fprintf(stderr, "%s\n", error.c_str());
        assert(job && !job->dirty);
        assert(job->root().children().size() == 4 && job->boardLocations().size() == 8 && job->panelLocations().size() == 3);
        JPPanelLocation* pnl1 = job->panelLocations()[1];
        assert(pnl1->id == "Pnl1" && pnl1->panel()->children.size() == 3 && pnl1->panel()->pseudoPlacementIds.size() == 4);
        const std::string d = JPPlacementsHolderLocation::kIdDelimiter;
        assert(pnl1->children()[0]->uniqueId() == "Pnl1" + d + "Brd1");
        assert(pnl1->panel()->definition() == jobs.panel((dir / "pnp-test" / "pnp-test.panel.xml").string(), error).get());
        // Pseudo-placements, in the panel's coordinates.
        JPPlacement fid;
        assert(pnl1->panel()->pseudoPlacementLocation("Brd1" + d + "FID4", fid));
        assert(near(fid.location.x(), 37.5) && near(fid.location.y(), 5.25) && fid.side == JPSide::Bottom);
        assert(fid.comments == "Pseudo-fiducial, for panel alignment only");
        assert(pnl1->panel()->pseudoPlacementLocation("Brd3" + d + "FID2", fid));
        assert(near(fid.location.x(), 67.75) && near(fid.location.y(), 48.0));
        assert(pnl1->panel()->pseudoPlacements().size() == 4);
        // How many times the board is used in the job; whether it is in use.
        assert(job->instanceCount(*board) == 8 && jobs.isInUse(*board, job.get()));
        // A board on a panel: through the panel's transform too.
        JPPlacementsHolderLocation* b = pnl1->children()[0];
        const JPLocation p = b->placementLocation(board->find("R1")->location);
        const JPLocation onPanel = b->placementLocation(board->find("R1")->location, true);
        const JPLocation viaPanel = pnl1->placementLocation(onPanel);
        assert(near(p.x(), viaPanel.x(), 1e-9) && near(p.y(), viaPanel.y(), 1e-9));
        // Written as OpenPnP writes it, and read the same.
        const std::string saved = (dir / "pnp-test" / "panelized-saved.job.xml").string();
        assert(jobs.saveJob(*job, saved, error));
        auto reread = jobs.loadJob(saved, error);
        assert(reread && reread->boardLocations().size() == 8 && reread->panelLocations()[1]->panel()->pseudoPlacements().size() == 4);
    }

    // An older job with no fiducial check named: EAT001.
    {
        auto job = jobs.loadJob((dir / "EAT001" / "EAT001.job.xml").string(), error);
        assert(job && job->boardLocations().size() == 1 && !job->boardLocations()[0]->checkFiducials);
        assert(job->boardLocations()[0]->board()->placements.size() == 31);
    }

    // A board saved is the file OpenPnP would read: version, placements, error handling.
    {
        assert(jobs.saveBoard(*board, error));
        std::ifstream in(boardFile);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(text.rfind("<openpnp-board version=\"1.1\" name=\"pnp-test.board.xml\">", 0) == 0);
        assert(text.find("<placement version=\"1.4\" side=\"Top\" id=\"R7\" part-id=\"R0201-1K\" type=\"Placement\" enabled=\"false\">") != std::string::npos);
        assert(text.find("<error-handling>Default</error-handling>") != std::string::npos);
        assert(text.find("<board-pad type=\"Paste\" side=\"Top\" name=\"R1-1\">") != std::string::npos);
        assert(text.find("<pad class=\"org.openpnp.model.Pad$RoundRectangle\" units=\"Millimeters\" width=\"1.5\" "
                         "height=\"1.3\" roundness=\"0.0\"/>") != std::string::npos);
    }

    fs::remove_all(dir);
    return 0;
}
