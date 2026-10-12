// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A package's height is its parts' where a part has none of its own: what a part is placed by is its own height,
// else its package's. The Parts table shows the package's greyed and takes a part's own when typed (emptied: the
// package's again); the Packages table edits the package's; a package's is kept in the library and left out of
// what is copied for OpenPnP when it has none (a package without one is written as before).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPLibraryJson.h"
#include "openpnp/JPXmlJson.h"
#include "ui/JPPackagesTableModel.h"
#include "ui/JPPartsTableModel.h"
#include "ui/JPPlacementsTableModel.h"

#include <cmath>
#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

namespace {

int columnKeyed(const JPTableModel& m, const std::string& key) {
    for (int c = 0; c < m.columnCount(); ++c)
        if (JPTableModel::keyOf(m.column(c)) == key) return c;
    return -1;
}

double mm(const JPLength& l) { return l.convertToUnits(JPLengthUnit::Millimeters).value(); }

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-package-height";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    auto package = std::make_shared<JPPackage>();
    package->id = "pkg-0805";
    package->label = "C_0805";
    config.addPackage(package);
    auto small = std::make_shared<JPPart>();
    small->id = "cap-100n";
    small->packageId = package->id;
    config.addPart(small);
    auto tall = std::make_shared<JPPart>();
    tall->id = "cap-22u";
    tall->packageId = package->id;
    tall->height = JPLength(1.25, JPLengthUnit::Millimeters);
    tall->throughBoardDepth = JPLength(0.5, JPLengthUnit::Millimeters);
    config.addPart(tall);

    // Neither knows it: not known. The package's given: the part with none of its own takes it; one with its own keeps it.
    assert(config.isPartHeightUnknown(*small));
    package->height = JPLength(0.6, JPLengthUnit::Millimeters);
    assert(!config.isPartHeightUnknown(*small) && std::abs(mm(config.partHeight(*small)) - 0.6) < 1e-9);
    assert(std::abs(mm(config.partHeight(*tall)) - 1.25) < 1e-9);
    assert(std::abs(mm(config.partHeightForSafeZ(*tall)) - 1.75) < 1e-9);

    // Kept with the package in the library; none written for one without (as OpenPnP's).
    const JPPackage again = JPLibraryJson::package(JPLibraryJson::package(*package));
    assert(std::abs(mm(again.height) - 0.6) < 1e-9);
    JPPackage none;
    none.id = "x";
    assert(!JPPackage::fromXml(JPXmlJson::element("package", JPXmlJson::from(none.toXml()))).height.value());
    assert(JPXmlJson::from(none.toXml()).dump().find("height-units") == std::string::npos);

    // The Parts table: the package's shown greyed, a part's own typed, emptied back to the package's.
    JPPartsTableModel parts(config);
    const int h = columnKeyed(parts, "height");
    const int rs = parts.rowOf(small.get()), rt = parts.rowOf(tall.get());
    assert(parts.cellInk(rs, h) && !parts.cellInk(rt, h));
    assert(parts.text(rs, h).find("0.6") != std::string::npos && parts.cellTooltip(rs, h).find("C_0805") != std::string::npos);
    std::string error;
    assert(parts.setText(rs, h, "0.85", error) && std::abs(mm(small->height) - 0.85) < 1e-9 && !parts.cellInk(rs, h));
    assert(parts.setText(rs, h, "", error) && small->height.value() == 0 && parts.cellInk(rs, h));

    // The Packages table edits the package's; emptied: not known.
    JPPackagesTableModel packages(config);
    const int ph = columnKeyed(packages, "height");
    const int rk = packages.rowOf(package.get());
    assert(ph >= 0 && packages.text(rk, ph).find("0.6") != std::string::npos);
    assert(packages.setText(rk, ph, "0.9", error) && std::abs(mm(config.partHeight(*small)) - 0.9) < 1e-9);
    assert(!packages.setText(rk, ph, "tall", error) && !error.empty());
    assert(packages.setText(rk, ph, " ", error) && config.isPartHeightUnknown(*small));

    fs::remove_all(dir);
    return 0;
}
