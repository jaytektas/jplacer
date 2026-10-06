// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's EagleMountsmdUlpImporterTest and EagleLoaderTest on OpenPnP's own samples: its Demo Board's and EAT001's
// mountsmd.ulp files (top and bottom) and one with whole numbers read, every line a placement; and its eagle.brd
// read, its first element R1; and its eagle.sch read, nothing imported from it (OpenPnP's Eagle importer reads a
// schematic and does nothing with it).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPConfiguration.h"
#include "model/JPEagleBoardImporter.h"
#include "model/JPEagleMountsmdUlpImporter.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace jf;
namespace fs = std::filesystem;

namespace {

const fs::path kSamples = fs::path(JPLACER_TESTDATA_DIR) / "openpnp/eagle";

size_t lines(const fs::path& p) {
    std::ifstream in(p);
    std::string line;
    size_t n = 0;
    while (std::getline(in, line))
        if (line.find_first_not_of(" \t\r") != std::string::npos) ++n;
    return n;
}

JPBoard read(const JPBoardImporter& importer, const std::vector<std::string>& files, JPConfiguration& config) {
    std::vector<bool> options;
    for (const auto& o : importer.options()) options.push_back(o.initial);
    JPBoard board;
    std::string error;
    const bool ok = importer.read(files, options, config, board, error);
    if (!ok) std::fprintf(stderr, "%s: %s\n", importer.name().c_str(), error.c_str());
    assert(ok);
    return board;
}

// Top and bottom read, Create Missing Parts on: a placement for every line, the bottom's on the bottom.
void mountsmd(const fs::path& dir, const std::string& top, const std::string& bottom) {
    JPConfiguration config(dir.string());
    JPEagleMountsmdUlpImporter m;
    const std::vector<std::string> files { (kSamples / top).string(), bottom.empty() ? std::string() : (kSamples / bottom).string() };
    const JPBoard b = read(m, files, config);
    const size_t topCount = lines(kSamples / top), bottomCount = bottom.empty() ? 0 : lines(kSamples / bottom);
    assert(b.placements.size() == topCount + bottomCount);
    for (size_t i = 0; i < b.placements.size(); ++i) assert((b.placements[i].side == JPSide::Bottom) == (i >= topCount));
    assert(!config.parts().empty());
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("jplacer-eagle-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    // testDemoBoard: the Demo Board's, then EAT001's.
    mountsmd(dir, "Demo Board v2.mnt", "Demo Board v2.mnb");
    // testEAT001
    mountsmd(dir, "EAT001.mnt", "EAT001.mnb");
    // testWholeNumbers: places given as whole numbers ("33.02 17").
    {
        mountsmd(dir, "mountsmd_whole_numbers.mnt", "");
        JPConfiguration config(dir.string());
        JPEagleMountsmdUlpImporter m;
        const JPBoard b = read(m, { (kSamples / "mountsmd_whole_numbers.mnt").string(), "" }, config);
        assert(b.placements[1].id == "E$2" && b.placements[1].location.x() == 33.02 && b.placements[1].location.y() == 17);
        assert(b.placements[2].location.x() == 17 && b.placements[2].location.y() == 2.54);
    }
    // EagleLoaderTest.testLoadBoard: the board's first element.
    {
        JPConfiguration config(dir.string());
        JPEagleBoardImporter e;
        const JPBoard b = read(e, { (kSamples / "eagle.brd").string() }, config);
        assert(!b.placements.empty() && b.placements.front().id == "R1");
    }
    // EagleLoaderTest.testLoadSchematic: read, and nothing in it to import.
    {
        JPConfiguration config(dir.string());
        JPEagleBoardImporter e;
        const JPBoard b = read(e, { (kSamples / "eagle.sch").string() }, config);
        assert(b.placements.empty());
    }
    fs::remove_all(dir);
    return 0;
}
