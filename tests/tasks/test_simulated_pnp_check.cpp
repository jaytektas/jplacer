// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Simulation Mode's Pick & Place Checking, as OpenPnP's ImageCamera checks
// it, on OpenPnP's own test picture and packages: a 0805 resistor in the
// first lower strip is recognized where it lies, turned as it lies, and not
// a millimetre off or turned across; the pads of R1 on the job's first board
// are recognized near where the job (before its fiducials) has the board,
// and not two millimetres off.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPConfiguration.h"
#include "tasks/JPSimulatedPnpCheck.h"

#include <filesystem>
#include <string>

#include <unistd.h>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const std::string defaults = std::string(JPLACER_TESTDATA_DIR) + "/../../openpnp-defaults";
    // The shipped defaults, read from a copy: loading writes the library's and runs' files beside them.
    const fs::path work = fs::temp_directory_path() / ("jplacer-test-simulated-pnp-check-" + std::to_string(::getpid()));
    fs::remove_all(work);
    fs::copy(defaults + "/config", work, fs::copy_options::recursive);
    JPConfiguration config(work.string());
    std::vector<std::string> problems;
    std::string error;
    assert(config.load(problems, error));
    const JPPart* r0805 = config.part("R0805-1K");
    assert(r0805);
    const JPPackage* pkg = config.package(r0805->packageId);
    assert(pkg);
    JPSimulatedPnpCheck check;
    // OpenPnP's default ImageCamera: the picture at 0.04233 mm a pixel, its bottom left at the origin.
    const JPSimulatedPnpCheck::Picture pic { defaults + "/samples/pnp-test/pnp-test.png", 0.04233, 0.04233, 0, 0, true };
    // OpenPnP's default tolerances.
    const JPSimulatedPnpCheck::Tolerance pick { 0.1, 0.87 }, place { 0.1, 0.58 };
    std::string detail;

    // The first part of the strip whose reference hole is at 147.347, 40.285 (the strip a little off Y).
    const double partX = 150.847, partY = 38.285, partRotation = 1.22;
    assert(check.isPartLocation(pic, pkg->footprint, partX, partY, partRotation, true, pick, detail));
    assert(!check.isPartLocation(pic, pkg->footprint, partX + 1, partY, partRotation, true, pick, detail));
    assert(!check.isPartLocation(pic, pkg->footprint, partX, partY, partRotation + 90, true, pick, detail));

    // R1 (31, 6 on the board) on the first board at 3.995, 4.413: within half a millimetre.
    const double padsX = 34.995, padsY = 10.413;
    assert(!check.isPartLocation(pic, pkg->footprint, padsX, padsY, 0, false, place, detail));   // 0.35 mm off
    assert(check.isPartLocation(pic, pkg->footprint, padsX, padsY, 0, false, { 0.5, 0.58 }, detail));
    assert(!check.isPartLocation(pic, pkg->footprint, padsX + 2, padsY, 0, false, { 0.5, 0.58 }, detail));

    // A picture that is not there: said so.
    JPSimulatedPnpCheck::Picture missing = pic;
    missing.path = defaults + "/samples/none.png";
    assert(!check.isPartLocation(missing, pkg->footprint, partX, partY, partRotation, true, pick, detail) && !detail.empty());
    return 0;
}
