// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's ReferenceBottomVisionOffsetTest and ReferenceBottomVisionTest: OpenPnP's default machine's simulated camera looking up, a part on
// the nozzle drawn as its SimulatedUpCamera draws it (off by its Pick Error Offsets), found by the stock bottom
// vision pipeline, its offsets worked out as ReferenceBottomVision.findOffsets does: a symmetric R0805 and an
// asymmetric part (its pads' middle 0.5 mm off its origin), with and without a Vision Offset, pre-rotated or not,
// placed at 0, 45, 90 and 180 degrees, picked true and picked off; within 0.1 mm and 0.07 degrees of what OpenPnP
// expects; and OpenPnP's default configuration's R0805 picked 0.25, 0.75 mm off and turned 13 degrees either way,
// found so within 0.1 mm and 0.03 degrees. And the checks: the nozzle tip's Max. Pick Tolerance and the Part size
// check.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "BottomVisionBench.h"

using namespace jf;
namespace fs = std::filesystem;

namespace {

// What OpenPnP's test allows, and sets the nozzle tip's Max. Pick Tolerance to (for the large offsets).
constexpr double kMaxErrorMm = 0.1, kMaxErrorDeg = 0.07, kPickToleranceMm = 2;

struct Bench : BottomVisionBench {
    explicit Bench(const std::string& dir) : BottomVisionBench(dir) { tip.maxPickToleranceMm = kPickToleranceMm; }
};

// One of OpenPnP's tests: the part, its settings, and the offsets expected for each placement angle.
struct Case {
    const char* name;
    const char* partId;
    double      visionOffsetX, visionOffsetY;
    const char* preRotateUsage;
    bool        noCompositing;
    JPMachineLocation error;   // the camera's Pick Error Offsets
    // The offsets expected at 0, 45, 90 and 180 degrees.
    std::vector<JPLocation> expected;
};

JPLocation mm(double x, double y, double r = 0) { return JPLocation(JPLengthUnit::Millimeters, x, y, 0, r); }

void run(const std::string& dir, const Case& c) {
    Bench bench(dir);
    JPVisionSettings& settings = bench.settingsOf(c.partId);
    settings.setLocationOf("vision-offset", mm(c.visionOffsetX, c.visionOffsetY));
    settings.setText("pre-rotate-usage", c.preRotateUsage);
    if (c.noCompositing) {
        JPPackage& pkg = *bench.config.package(bench.config.part(c.partId)->packageId);
        JPVisionCompositing vc = pkg.visionCompositing.value_or(JPVisionCompositing {});
        vc.compositingMethod = JPVisionCompositing::Method::None;
        pkg.visionCompositing = vc;
    }
    bench.up.errorOffsets = c.error;
    const double angles[] = { 0, 45, 90, 180 };
    for (size_t i = 0; i < 4; ++i) {
        JPBottomVision::Offset offset;
        std::string why;
        const bool found = bench.findOffsets(c.partId, angles[i], offset, why);
        const JPLocation& o = offset.location;
        const JPLocation& e = c.expected[i];
        std::fprintf(stderr, "%s at %g: %s%s offsets %.3f, %.3f, %.3f (expected %.3f, %.3f, %.3f)\n", c.name, angles[i],
                     found ? "" : "FAILED ", why.c_str(), o.x(), o.y(), o.rotation(), e.x(), e.y(), e.rotation());
        assert(found);
        assert(std::abs(o.x() - e.x()) <= kMaxErrorMm && std::abs(o.y() - e.y()) <= kMaxErrorMm);
        assert(std::abs(o.rotation() - e.rotation()) <= kMaxErrorDeg);
    }
}

} // namespace

int main() {
    // OpenPnP's test configuration (its parts and packages) in a configuration of its own.
    const fs::path dir = fs::temp_directory_path() / ("jplacer-bottom-vision-offset-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    for (const char* f : { "packages.xml", "parts.xml" })
        fs::copy_file(fs::path(JPLACER_TESTDATA_DIR) / "openpnp/bottom-vision-offset" / f, dir / f, fs::copy_options::overwrite_existing);

    const JPMachineLocation none {};
    const std::vector<JPLocation> zero(4, mm(0, 0));
    const double r2 = std::sqrt(2.0);
    std::vector<Case> cases {
        { "testSymetricPartNoOffsetNoPreRotation", "R0805-1K", 0, 0, "AlwaysOff", false, none, zero },
        { "testSymetricPartNoOffsetWithPreRotation", "R0805-1K", 0, 0, "AlwaysOn", false, none, zero },
        { "testSymetricPartWithOffsetWithPreRotation", "R0805-1K", 1, 1, "AlwaysOn", false, none,
          { mm(-1, -1), mm(0, -r2), mm(1, -1), mm(1, 1) } },
        { "testAsymetricPartNoOffsetNoPreRotation", "DoubleAsymPart", 0, 0, "AlwaysOff", true, none, std::vector<JPLocation>(4, mm(0.5, 0.5)) },
        { "testAsymetricPartNoOffsetWithPreRotation", "DoubleAsymPart", 0, 0, "AlwaysOn", true, none,
          { mm(0.5, 0.5), mm(0.5, 0.5).rotateXy(45), mm(0.5, 0.5).rotateXy(90), mm(0.5, 0.5).rotateXy(180) } },
        { "testAsymetricPartWithOffsetNoPreRotation", "DoubleAsymPart", 0.5, 0.5, "AlwaysOff", false, none, zero },
        { "testAsymetricPartWithOffsetWithPreRotation", "DoubleAsymPart", 0.5, 0.5, "AlwaysOn", false, none, zero },
    };
    // Picked off: the camera's Pick Error Offsets, found again as the offsets.
    const JPLocation error1 = mm(1.0, -0.5, 18.0);
    cases.push_back({ "testAsymetricPartWithOffsetNoPreRotationWithError", "DoubleAsymPart", 0.5, 0.5, "AlwaysOff", false,
                      JPMachineLocation { 1.0, -0.5, 0, 18.0 }, std::vector<JPLocation>(4, error1) });
    const JPLocation error2 = mm(-0.6, 1.2, -12.0);
    std::vector<JPLocation> turned;
    for (double a : { 0.0, 45.0, 90.0, 180.0 }) turned.push_back(error2.rotateXy(-error2.rotation() + a));
    cases.push_back({ "testAsymetricPartWithOffsetWithPreRotationWithError", "DoubleAsymPart", 0.5, 0.5, "AlwaysOn", false,
                      JPMachineLocation { -0.6, 1.2, 0, -12.0 }, turned });
    for (const Case& c : cases) run(dir.string(), c);

    // ReferenceBottomVisionTest: OpenPnP's default configuration, a 1 mm pick tolerance, not pre-rotated.
    for (const double angle : { 13.0, -13.0 }) {
        Bench bench(std::string(JPLACER_TESTDATA_DIR) + "/../../openpnp-defaults/config");
        bench.tip.maxPickToleranceMm = 1;
        bench.up.errorOffsets = JPMachineLocation { 0.25, 0.75, 0, angle };
        JPBottomVision::Offset offset;
        std::string why;
        assert(bench.findOffsets("R0805-1K", 0, offset, why));
        const JPLocation& o = offset.location;
        std::fprintf(stderr, "testError %g: offsets %.3f, %.3f, %.3f\n", angle, o.x(), o.y(), o.rotation());
        assert(!offset.preRotated);
        assert(std::abs(o.x() - 0.25) <= 0.1 && std::abs(o.y() - 0.75) <= 0.1 && std::abs(o.rotation() - angle) <= 0.03);
    }

    // Offsets beyond the nozzle tip's Max. Pick Tolerance: refused.
    {
        Bench bench(dir.string());
        bench.settingsOf("DoubleAsymPart").setText("pre-rotate-usage", "AlwaysOff");
        bench.tip.maxPickToleranceMm = 0.5;
        JPBottomVision::Offset offset;
        std::string why;
        assert(!bench.findOffsets("DoubleAsymPart", 0, offset, why));
        std::fprintf(stderr, "%s\n", why.c_str());
        assert(why.find("larger than the allowed Max. Pick Tolerance 0.500mm set on nozzle tip NT1") != std::string::npos);
    }
    // The Part size check: the R0805's pads seen as wide and long as its body; the asymmetric part's pads (1.25 mm
    // across) far smaller than its 3 mm body.
    {
        Bench bench(dir.string());
        bench.settingsOf("R0805-1K").setText("check-part-size-method", "BodySize");
        JPBottomVision::Offset offset;
        std::string why;
        assert(bench.findOffsets("R0805-1K", 0, offset, why));
        bench.settingsOf("DoubleAsymPart").setText("check-part-size-method", "BodySize");
        bench.settingsOf("DoubleAsymPart").setLocationOf("vision-offset", mm(0.5, 0.5));
        assert(!bench.findOffsets("DoubleAsymPart", 0, offset, why));
        std::fprintf(stderr, "%s\n", why.c_str());
        assert(why.find("Part DoubleAsymPart width too small: nominal 3.000mm, limit 2.400mm") == 0);
    }
    fs::remove_all(dir);
    return 0;
}
