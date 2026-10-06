// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's VisionCompositingTest: each of its compositing test parts (a cross, headphones, a stranger shape, a FET,
// a BGA, an SOIC, a small FET, LQFPs, a rectifier) picked off by 0.25, 0.75 mm and -2 degrees (a small part by 0.05,
// 0.1 mm and 7 degrees), aligned by bottom vision pre-rotating to 0.1 mm and 0.1 degrees, in the shots its vision
// compositing plans on OpenPnP's simulated camera looking up (a 30 mm roaming radius, a 1 mm pick tolerance), its
// size checked by its pads' extent: its offsets found within 0.05 mm and 0.07 degrees (0.025 mm and 1.5 degrees for a
// small part).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "BottomVisionBench.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / ("jplacer-vision-compositing-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    for (const char* f : { "packages.xml", "parts.xml" })
        fs::copy_file(fs::path(JPLACER_TESTDATA_DIR) / "openpnp/compositing" / f, dir / f, fs::copy_options::overwrite_existing);
    BottomVisionBench bench(dir.string());
    bench.camera.roamingRadiusMm = 30;
    bench.tip.maxPickToleranceMm = 1;
    // Some of these irregular parts need pre-rotate vision, i.e. passes.
    bench.vision.preRotate = true;
    bench.vision.maxAngularOffset = 0.1;
    bench.vision.maxLinearOffsetMm = 0.1;
    int tested = 0;
    for (const auto& p : bench.config.parts()) {
        const JPPart& part = *p;
        if (part.id.rfind("FID", 0) == 0) continue;
        // The size checked, to check that composite vision measures it right.
        bench.settingsOf(part.id).setText("check-part-size-method", "PadExtents");
        const bool small = part.id.rfind("SMALL", 0) == 0;
        const JPMachineLocation error = small ? JPMachineLocation { 0.05, 0.1, 0, 7 } : JPMachineLocation { 0.25, 0.75, 0, -2 };
        const double maxMm = small ? 0.025 : 0.05, maxDeg = small ? 1.5 : 0.07;
        bench.up.errorOffsets = error;
        JPBottomVision::Offset offset;
        std::string why;
        const bool found = bench.findOffsets(part.id, 0, offset, why);
        const JPLocation& o = offset.location;
        std::fprintf(stderr, "%s: %s%s offsets %.3f, %.3f, %.3f (picked off %.3f, %.3f, %.3f)\n", part.id.c_str(), found ? "" : "FAILED ",
                     why.c_str(), o.x(), o.y(), o.rotation(), error.x, error.y, error.rotation);
        assert(found);
        assert(std::abs(o.x() - error.x) <= maxMm && std::abs(o.y() - error.y) <= maxMm && std::abs(o.rotation() - error.rotation) <= maxDeg);
        ++tested;
    }
    assert(tested == 10);
    fs::remove_all(dir);
    return 0;
}
