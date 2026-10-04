// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Vision settings as OpenPnP keeps them (vision-settings.xml): written back
// as read, pipeline and all; edited; what uses each (Assigned To); and the
// settings a part inherits (its own, its package's, the machine's).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPConfiguration.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace jf;
namespace fs = std::filesystem;

namespace {

const char* kFile = R"(<openpnp-vision-settings>
   <vision-settings class="org.openpnp.model.BottomVisionSettings" id="BVS_Stock" name="- Stock Bottom Vision Settings -" enabled="true" pre-rotate-usage="Default" check-part-size-method="Disabled" check-size-tolerance-percent="20" max-rotation="Adjust" asymmetric="false">
      <cv-pipeline>
         <stages>
            <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" default-light="true" settle-option="Settle" count="1"/>
         </stages>
      </cv-pipeline>
      <vision-offset units="Millimeters" x="0.0" y="0.0" z="0.0" rotation="0.0"/>
   </vision-settings>
   <vision-settings class="org.openpnp.model.BottomVisionSettings" id="BVS_Default" name="- Default Machine Bottom Vision -" enabled="true" pre-rotate-usage="AlwaysOn" check-part-size-method="Disabled" check-size-tolerance-percent="20" max-rotation="Adjust" asymmetric="false"/>
   <vision-settings class="org.openpnp.model.BottomVisionSettings" id="BVS1" name="R0603" enabled="true" pre-rotate-usage="AlwaysOn" check-part-size-method="BodySize" check-size-tolerance-percent="20" max-rotation="Adjust" asymmetric="false"/>
   <vision-settings class="org.openpnp.model.FiducialVisionSettings" id="FVS_Default" name="- Default Machine Fiducial Locator -" enabled="true" parallax-angle="0.0" max-vision-passes="3">
      <parallax-diameter value="0.0" units="Millimeters"/>
      <max-linear-offset value="0.1" units="Millimeters"/>
   </vision-settings>
</openpnp-vision-settings>
)";

std::string read(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-vision-settings";
    fs::remove_all(dir);
    fs::create_directories(dir);
    { std::ofstream(dir / "vision-settings.xml") << kFile; }
    JPConfiguration config(dir.string());
    std::vector<std::string> problems;
    std::string error;
    assert(config.load(problems, error));
    assert(config.visionSettings().size() == 4);
    // Written back as read.
    assert(config.save(error));
    assert(read(dir / "vision-settings.xml") == kFile);

    // Edited; the stock settings are known as such.
    JPVisionSettings* v = config.visionSettings("BVS1");
    assert(v && v->kind == JPVisionSettings::Kind::Bottom && !v->isStock() && config.visionSettings("BVS_Stock")->isStock());
    v->setText("check-size-tolerance-percent", "35");
    assert(v->number("check-size-tolerance-percent") == 35);
    const JPVisionSettings* f = config.visionSettings("FVS_Default");
    assert(f->number("max-vision-passes") == 3 && f->lengthMm("max-linear-offset", 0.2) == 0.1);

    // A package and a part using them; what each is used in; what a part inherits.
    auto pkg = std::make_shared<JPPackage>();
    pkg->id = "R0603";
    pkg->bottomVisionId = "BVS1";
    config.addPackage(pkg);
    auto part = std::make_shared<JPPart>();
    part->id = "R1K";
    part->packageId = "R0603";
    config.addPart(part);
    auto bare = std::make_shared<JPPart>();
    bare->id = "C1";
    config.addPart(bare);
    assert((config.visionUsedIn(*v, "BVS_Default", "Bottom Vision") == std::vector<std::string> { "R0603" }));
    assert((config.visionUsedIn(*config.visionSettings("BVS_Default"), "BVS_Default", "Bottom Vision") ==
            std::vector<std::string> { "Bottom Vision" }));
    assert(config.inheritedVision(*part, JPVisionSettings::Kind::Bottom, "BVS_Default") == v);
    assert(config.inheritedVision(*bare, JPVisionSettings::Kind::Bottom, "BVS_Default")->id == "BVS_Default");
    part->bottomVisionId = "BVS_Stock";
    assert(config.inheritedVision(*part, JPVisionSettings::Kind::Bottom, "BVS_Default")->id == "BVS_Stock");

    // New and removed.
    config.addVisionSettings(JPVisionSettings::create(JPVisionSettings::Kind::Fiducial, "FVS2"));
    assert(config.visionSettings("FVS2")->name == "FiducialVisionSettings");
    config.removeVisionSettings("FVS2");
    assert(!config.visionSettings("FVS2"));
    return 0;
}
